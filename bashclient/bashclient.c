/* bashclient.c -- CLI client for bash-server */

/* Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of GNU Bash, the Bourne Again SHell.

   Bash is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Bash is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Bash.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <ctype.h>

#define MAX_LINE    8192
#define MAX_CMD     65536

/* Client configuration */
typedef struct {
    char *socket_path;
    char *auth_token;
    char *command;
    char *script_file;
    int   interactive;
    int   verbose;
} client_config_t;

/* Base64 decoding table */
static unsigned char base64_decode_table[256];
static int base64_table_initialized = 0;

/* Initialize base64 decode table */
static void
init_base64_decode_table(void)
{
    static const char base64_table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int i;
    
    if (base64_table_initialized)
        return;
    
    memset(base64_decode_table, 0xff, sizeof(base64_decode_table));
    for (i = 0; i < 64; i++) {
        base64_decode_table[(unsigned char)base64_table[i]] = i;
    }
    base64_decode_table['='] = 0;
    base64_table_initialized = 1;
}

/* Base64 decode */
static char *
base64_decode(const char *data, size_t *outlen)
{
    char *result;
    size_t len, rlen;
    size_t i, j;
    unsigned char a, b, c, d;
    
    init_base64_decode_table();
    
    len = strlen(data);
    if (len % 4 != 0) {
        *outlen = 0;
        return NULL;
    }
    
    rlen = (len / 4) * 3;
    if (len > 0 && data[len - 1] == '=')
        rlen--;
    if (len > 1 && data[len - 2] == '=')
        rlen--;
    
    result = malloc(rlen + 1);
    if (!result) {
        *outlen = 0;
        return NULL;
    }
    
    j = 0;
    for (i = 0; i < len; i += 4) {
        a = base64_decode_table[(unsigned char)data[i]];
        b = base64_decode_table[(unsigned char)data[i + 1]];
        c = base64_decode_table[(unsigned char)data[i + 2]];
        d = base64_decode_table[(unsigned char)data[i + 3]];
        
        if (a == 0xff || b == 0xff) {
            free(result);
            *outlen = 0;
            return NULL;
        }
        
        result[j++] = (a << 2) | (b >> 4);
        if (data[i + 2] != '=')
            result[j++] = ((b & 0x0f) << 4) | (c >> 2);
        if (data[i + 3] != '=')
            result[j++] = ((c & 0x03) << 6) | d;
    }
    
    result[j] = '\0';
    *outlen = j;
    return result;
}

/* Connect to server */
static int
connect_to_server(const char *socket_path)
{
    int fd;
    struct sockaddr_un addr;
    
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
    
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }
    
    return fd;
}

/* Read a line from socket */
static int
read_line(int fd, char *buf, size_t bufsize)
{
    size_t pos = 0;
    ssize_t n;
    char c;
    
    while (pos < bufsize - 1) {
        n = read(fd, &c, 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;
        
        if (c == '\n')
            break;
        if (c == '\r')
            continue;
        
        buf[pos++] = c;
    }
    
    buf[pos] = '\0';
    return (int)pos;
}

/* Write a line to socket */
static int
write_line(int fd, const char *line)
{
    size_t len = strlen(line);
    ssize_t written;
    char nl = '\n';
    
    while (len > 0) {
        written = write(fd, line, len);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        line += written;
        len -= written;
    }
    
    /* Add newline */
    while ((written = write(fd, &nl, 1)) < 0 && errno == EINTR)
        ;
    
    return written < 0 ? -1 : 0;
}

/* Send command and receive response */
static int
send_command(int fd, const char *cmd, char *response, size_t respsize)
{
    if (write_line(fd, cmd) < 0) {
        fprintf(stderr, "bashclient: write failed: %s\n", strerror(errno));
        return -1;
    }
    
    if (read_line(fd, response, respsize) < 0) {
        fprintf(stderr, "bashclient: read failed: %s\n", strerror(errno));
        return -1;
    }
    
    return 0;
}

/* Authenticate with server */
static int
authenticate(int fd, const char *token)
{
    char cmd[MAX_LINE];
    char response[MAX_LINE];
    
    snprintf(cmd, sizeof(cmd), "AUTH %s", token);
    
    if (send_command(fd, cmd, response, sizeof(response)) < 0)
        return -1;
    
    if (strncmp(response, "OK", 2) != 0) {
        fprintf(stderr, "bashclient: authentication failed: %s\n", response);
        return -1;
    }
    
    return 0;
}

/* Execute a command */
static int
execute_command(int fd, const char *command, int verbose)
{
    char cmd[MAX_CMD];
    char response[MAX_LINE];
    char *decoded;
    size_t decoded_len;
    int exit_code = 0;
    
    snprintf(cmd, sizeof(cmd), "EVAL %s", command);
    
    if (write_line(fd, cmd) < 0) {
        fprintf(stderr, "bashclient: write failed: %s\n", strerror(errno));
        return 127;
    }
    
    /* Read responses until EXIT */
    while (1) {
        if (read_line(fd, response, sizeof(response)) < 0) {
            fprintf(stderr, "bashclient: read failed: %s\n", strerror(errno));
            return 127;
        }
        
        if (strncmp(response, "STDOUT ", 7) == 0) {
            /* Decode and print stdout */
            if (response[7]) {
                decoded = base64_decode(response + 7, &decoded_len);
                if (decoded) {
                    fwrite(decoded, 1, decoded_len, stdout);
                    free(decoded);
                }
            }
        } else if (strncmp(response, "STDERR ", 7) == 0) {
            /* Decode and print stderr */
            if (response[7]) {
                decoded = base64_decode(response + 7, &decoded_len);
                if (decoded) {
                    fwrite(decoded, 1, decoded_len, stderr);
                    free(decoded);
                }
            }
        } else if (strncmp(response, "EXIT ", 5) == 0) {
            exit_code = atoi(response + 5);
            break;
        } else if (strncmp(response, "ERR ", 4) == 0) {
            fprintf(stderr, "bashclient: server error: %s\n", response + 4);
            return 127;
        } else if (strcmp(response, "STDOUT") == 0 || strcmp(response, "STDERR") == 0) {
            /* Empty output - ignore */
        } else {
            if (verbose) {
                fprintf(stderr, "bashclient: unexpected response: %s\n", response);
            }
        }
    }
    
    return exit_code;
}

/* Run interactive mode */
static int
run_interactive(int fd, int verbose)
{
    char line[MAX_CMD];
    int exit_code = 0;
    
    printf("bashclient interactive mode. Type 'exit' or Ctrl-D to quit.\n");
    
    while (1) {
        printf("bashclient> ");
        fflush(stdout);
        
        if (!fgets(line, sizeof(line), stdin))
            break;
        
        /* Remove trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        
        if (len == 0)
            continue;
        
        /* Check for exit command */
        if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0)
            break;
        
        exit_code = execute_command(fd, line, verbose);
    }
    
    return exit_code;
}

/* Run script from file */
static int
run_script(int fd, const char *filename, int verbose)
{
    FILE *fp;
    char line[MAX_CMD];
    int exit_code = 0;
    
    fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "bashclient: cannot open %s: %s\n", filename, strerror(errno));
        return 127;
    }
    
    while (fgets(line, sizeof(line), fp)) {
        /* Remove trailing newline */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        
        /* Skip empty lines and comments */
        if (len == 0 || line[0] == '#')
            continue;
        
        exit_code = execute_command(fd, line, verbose);
    }
    
    fclose(fp);
    return exit_code;
}

/* Print usage */
static void
print_usage(const char *progname)
{
    printf("Usage: %s [OPTIONS]\n\n", progname);
    printf("Options:\n");
    printf("  -s, --socket PATH   Unix socket path (required)\n");
    printf("  -a, --auth TOKEN    Authentication token (required)\n");
    printf("  -e, --eval CMD      Execute command\n");
    printf("  -f, --file SCRIPT   Execute script file\n");
    printf("  -i, --interactive   Interactive mode\n");
    printf("  -v, --verbose       Verbose output\n");
    printf("  -h, --help          Show this help\n");
    printf("  -V, --version       Show version\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s -s /tmp/bash.sock -a secret -e 'echo hello'\n", progname);
    printf("  %s -s /tmp/bash.sock -a secret -i\n", progname);
    printf("  %s -s /tmp/bash.sock -a secret -f script.sh\n", progname);
}

/* Print version */
static void
print_version(void)
{
    printf("bashclient 1.0 (GNU Bash 5.1)\n");
    printf("Copyright (C) 2026 Free Software Foundation, Inc.\n");
    printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>\n");
}

/* Parse arguments */
static int
parse_arguments(int argc, char **argv, client_config_t *cfg)
{
    static struct option long_options[] = {
        {"socket",      required_argument, 0, 's'},
        {"auth",        required_argument, 0, 'a'},
        {"eval",        required_argument, 0, 'e'},
        {"file",        required_argument, 0, 'f'},
        {"interactive", no_argument,       0, 'i'},
        {"verbose",     no_argument,       0, 'v'},
        {"help",        no_argument,       0, 'h'},
        {"version",     no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };
    
    int c, option_index;
    
    memset(cfg, 0, sizeof(*cfg));
    
    while ((c = getopt_long(argc, argv, "s:a:e:f:ivhV", long_options, &option_index)) != -1) {
        switch (c) {
            case 's':
                cfg->socket_path = optarg;
                break;
            case 'a':
                cfg->auth_token = optarg;
                break;
            case 'e':
                cfg->command = optarg;
                break;
            case 'f':
                cfg->script_file = optarg;
                break;
            case 'i':
                cfg->interactive = 1;
                break;
            case 'v':
                cfg->verbose = 1;
                break;
            case 'h':
                print_usage(argv[0]);
                exit(0);
            case 'V':
                print_version();
                exit(0);
            default:
                return -1;
        }
    }
    
    if (!cfg->socket_path) {
        fprintf(stderr, "bashclient: --socket is required\n");
        return -1;
    }
    if (!cfg->auth_token) {
        fprintf(stderr, "bashclient: --auth is required\n");
        return -1;
    }
    
    /* Need at least one of: command, script, or interactive */
    if (!cfg->command && !cfg->script_file && !cfg->interactive) {
        /* Default to interactive if stdin is a tty */
        if (isatty(STDIN_FILENO)) {
            cfg->interactive = 1;
        } else {
            fprintf(stderr, "bashclient: no command, script, or interactive mode specified\n");
            return -1;
        }
    }
    
    return 0;
}

int
main(int argc, char **argv)
{
    client_config_t config;
    int fd;
    int exit_code = 0;
    char response[MAX_LINE];
    
    if (parse_arguments(argc, argv, &config) < 0) {
        print_usage(argv[0]);
        return 1;
    }
    
    /* Connect to server */
    fd = connect_to_server(config.socket_path);
    if (fd < 0)
        return 1;
    
    /* Authenticate */
    if (authenticate(fd, config.auth_token) < 0) {
        close(fd);
        return 1;
    }
    
    /* Execute based on mode */
    if (config.command) {
        exit_code = execute_command(fd, config.command, config.verbose);
    } else if (config.script_file) {
        exit_code = run_script(fd, config.script_file, config.verbose);
    } else if (config.interactive) {
        exit_code = run_interactive(fd, config.verbose);
    }
    
    /* Send QUIT */
    write_line(fd, "QUIT");
    read_line(fd, response, sizeof(response));  /* Read BYE */
    
    close(fd);
    return exit_code;
}
