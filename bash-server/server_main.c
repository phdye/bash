/* server_main.c -- Main entry point for bash-server */

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

#include "server.h"
#include <getopt.h>
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>

/* Global state */
static volatile sig_atomic_t server_running = 1;
static volatile sig_atomic_t got_sigchld = 0;
static int server_fd = -1;
static server_config_t config;

/* Forward declarations */
static void print_usage(const char *progname);
static void print_version(void);
static int parse_arguments(int argc, char **argv, server_config_t *cfg);
static void reap_children(void);
static void write_pid_file(const char *path);
static int generate_auth_token(server_config_t *cfg);
static int resolve_socket_path(server_config_t *cfg);
static int read_config_file(server_config_t *cfg);

/* Signal handler */
void
server_signal_handler(int sig)
{
    switch (sig) {
        case SIGINT:
        case SIGTERM:
            server_running = 0;
            break;
        case SIGCHLD:
            got_sigchld = 1;
            break;
        case SIGPIPE:
            /* Ignore */
            break;
    }
}

/* Set up signal handlers */
static void
setup_signals(void)
{
    struct sigaction sa;

    /* SIGINT/SIGTERM: do NOT use SA_RESTART so that accept() returns
       EINTR and the main loop can check server_running. */
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sa.sa_handler = server_signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* SIGCHLD: use SA_RESTART so child reaping doesn't interrupt I/O */
    sa.sa_flags = SA_RESTART;
    sa.sa_handler = server_signal_handler;
    sigaction(SIGCHLD, &sa, NULL);

    /* Ignore SIGPIPE - handle errors in write() */
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, NULL);
}

/* Server shutdown cleanup */
void
server_shutdown(void)
{
    if (server_fd >= 0) {
        server_socket_close(server_fd, config.socket_path);
        server_fd = -1;
    }

    if (config.auth_file) {
        unlink(config.auth_file);
    }

    if (config.pid_file) {
        unlink(config.pid_file);
    }

    /* Clean up children */
    reap_children();
}

/* Daemonize the process */
int
server_daemonize(void)
{
    pid_t pid;
    int fd;
    
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(0);  /* Parent exits */
    
    /* Child becomes session leader */
    if (setsid() < 0)
        return -1;
    
    /* Fork again to prevent acquiring a controlling terminal */
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(0);
    
    /* Change to root directory */
    chdir("/");
    
    /* Close standard file descriptors */
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    /* Redirect to /dev/null */
    fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > 2)
            close(fd);
    }
    
    return 0;
}

/* Reap zombie child processes */
static void
reap_children(void)
{
    int status;
    pid_t pid;
    
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (config.verbose) {
            fprintf(stderr, "bash-server: child %d exited with status %d\n",
                    (int)pid, WEXITSTATUS(status));
        }
    }
    got_sigchld = 0;
}

/* Write PID file */
static void
write_pid_file(const char *path)
{
    FILE *fp;
    
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%d\n", (int)getpid());
        fclose(fp);
    } else {
        fprintf(stderr, "bash-server: cannot write pid file %s: %s\n",
                path, strerror(errno));
    }
}

/* Print usage message */
static void
print_usage(const char *progname)
{
    printf("Usage: %s [OPTIONS]\n\n", progname);
    printf("Options:\n");
    printf("  -s, --socket PATH   Unix socket path (optional, see below)\n");
    printf("  -d, --daemon        Run as daemon\n");
    printf("  -p, --pidfile PATH  Write PID to file\n");
    printf("  -m, --max-clients N Maximum simultaneous clients (default: 10)\n");
    printf("  -P, --no-peercred   Disable Cygwin credential handshake (Python compat)\n");
    printf("  -v, --verbose       Verbose output\n");
    printf("  -h, --help          Show this help\n");
    printf("  -V, --version       Show version\n");
    printf("\n");
    printf("Socket path resolution (first match wins):\n");
    printf("  1. --socket PATH              Command line\n");
    printf("  2. $BASH_SERVER_SOCKET        Environment variable\n");
    printf("  3. socket directive            ~/.bash-serverrc\n");
    printf("  4. $XDG_RUNTIME_DIR/bash-server/sock   (if set)\n");
    printf("  5. /tmp/bash-server-<uid>/sock          (fallback)\n");
    printf("\n");
    printf("Authentication:\n");
    printf("  A random token is generated at startup and written to\n");
    printf("  <socket-path>.token (mode 0600). Clients read this file\n");
    printf("  to obtain the token for the AUTH command.\n");
    printf("\n");
    printf("Examples:\n");
    printf("  %s\n", progname);
    printf("  %s --socket ~/.local/run/bash-server/sock\n", progname);
    printf("  %s --daemon --pidfile /run/bash-server.pid\n", progname);
}

/* Print version */
static void
print_version(void)
{
    printf("bash-server 1.0 (GNU Bash 5.1)\n");
    printf("Copyright (C) 2026 Free Software Foundation, Inc.\n");
    printf("License GPLv3+: GNU GPL version 3 or later <https://gnu.org/licenses/gpl.html>\n");
}

/* Parse command-line arguments */
static int
parse_arguments(int argc, char **argv, server_config_t *cfg)
{
    static struct option long_options[] = {
        {"socket",      required_argument, 0, 's'},
        {"daemon",      no_argument,       0, 'd'},
        {"pidfile",     required_argument, 0, 'p'},
        {"max-clients", required_argument, 0, 'm'},
        {"no-peercred", no_argument,       0, 'P'},
        {"verbose",     no_argument,       0, 'v'},
        {"help",        no_argument,       0, 'h'},
        {"version",     no_argument,       0, 'V'},
        {0, 0, 0, 0}
    };

    int c, option_index;

    /* Defaults */
    memset(cfg, 0, sizeof(*cfg));
    cfg->max_clients = 10;

    while ((c = getopt_long(argc, argv, "s:dp:m:PvhV", long_options, &option_index)) != -1) {
        switch (c) {
            case 's':
                cfg->socket_path = optarg;
                break;
            case 'd':
                cfg->daemon_mode = 1;
                break;
            case 'p':
                cfg->pid_file = optarg;
                break;
            case 'm':
                cfg->max_clients = atoi(optarg);
                if (cfg->max_clients < 1)
                    cfg->max_clients = 1;
                break;
            case 'P':
                cfg->no_peercred = 1;
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

    /* socket_path is optional — resolve_socket_path() fills it in later */

    return 0;
}

/* Ensure a directory exists with the given mode.  Creates parent if needed. */
static int
ensure_directory(const char *path, mode_t mode)
{
    struct stat st;

    if (stat(path, &st) == 0) {
        if (S_ISDIR(st.st_mode))
            return 0;
        errno = ENOTDIR;
        return -1;
    }

    if (mkdir(path, mode) < 0 && errno != EEXIST)
        return -1;

    return 0;
}

/* Read config file (~/.bash-serverrc).
   Format: one directive per line.  '#' comments, blank lines ignored.
     socket PATH
   Only sets fields that are still NULL/0 (CLI takes precedence). */
static int
read_config_file(server_config_t *cfg)
{
    const char *home;
    char path[PATH_MAX];
    FILE *fp;
    char line[1024];
    char *p, *key, *val;

    home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw)
            home = pw->pw_dir;
    }
    if (!home)
        return 0;  /* No home directory — skip silently */

    snprintf(path, sizeof(path), "%s/.bash-serverrc", home);
    fp = fopen(path, "r");
    if (!fp)
        return 0;  /* No config file — not an error */

    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing whitespace */
        p = line + strlen(line);
        while (p > line && (p[-1] == '\n' || p[-1] == '\r' || p[-1] == ' ' || p[-1] == '\t'))
            *--p = '\0';

        /* Skip leading whitespace */
        p = line;
        while (*p == ' ' || *p == '\t')
            p++;

        /* Skip comments and blank lines */
        if (*p == '#' || *p == '\0')
            continue;

        /* Split into key and value */
        key = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p) {
            *p++ = '\0';
            while (*p == ' ' || *p == '\t')
                p++;
        }
        val = p;

        if (strcmp(key, "socket") == 0 && *val && !cfg->socket_path) {
            cfg->socket_path = strdup(val);
        }
        /* Future directives can be added here */
    }

    fclose(fp);
    return 0;
}

/* Resolve socket path using configuration hierarchy:
   1. Command line (--socket) — already set by parse_arguments
   2. Environment (BASH_SERVER_SOCKET)
   3. Config file (~/.bash-serverrc)
   4. Default: $XDG_RUNTIME_DIR/bash-server/sock
              or /tmp/bash-server-<uid>/sock */
static int
resolve_socket_path(server_config_t *cfg)
{
    const char *envval;
    char *dir, *path;
    char buf[PATH_MAX];

    /* Already set by CLI */
    if (cfg->socket_path)
        return 0;

    /* Try environment */
    envval = getenv("BASH_SERVER_SOCKET");
    if (envval && *envval) {
        cfg->socket_path = strdup(envval);
        return cfg->socket_path ? 0 : -1;
    }

    /* Try config file */
    if (read_config_file(cfg) < 0)
        return -1;
    if (cfg->socket_path)
        return 0;

    /* Build default path */
    envval = getenv("XDG_RUNTIME_DIR");
    if (envval && *envval) {
        snprintf(buf, sizeof(buf), "%s/bash-server", envval);
        dir = buf;
    } else {
        snprintf(buf, sizeof(buf), "/tmp/bash-server-%d", (int)getuid());
        dir = buf;
    }

    if (ensure_directory(dir, 0700) < 0) {
        fprintf(stderr, "bash-server: cannot create %s: %s\n",
                dir, strerror(errno));
        return -1;
    }

    path = malloc(strlen(dir) + 6);  /* "/sock\0" */
    if (!path)
        return -1;
    sprintf(path, "%s/sock", dir);
    cfg->socket_path = path;

    return 0;
}

/* Generate a random auth token and write it to <socket_path>.token */
static int
generate_auth_token(server_config_t *cfg)
{
    unsigned char raw[SERVER_TOKEN_BYTES];
    char hex[SERVER_TOKEN_HEXLEN + 1];
    int urandom_fd, token_fd;
    ssize_t n;
    size_t pathlen;
    int i;

    /* Read random bytes from /dev/urandom */
    urandom_fd = open("/dev/urandom", O_RDONLY);
    if (urandom_fd < 0) {
        fprintf(stderr, "bash-server: cannot open /dev/urandom: %s\n",
                strerror(errno));
        return -1;
    }

    n = read(urandom_fd, raw, sizeof(raw));
    close(urandom_fd);
    if (n != sizeof(raw)) {
        fprintf(stderr, "bash-server: short read from /dev/urandom\n");
        return -1;
    }

    /* Hex-encode */
    for (i = 0; i < SERVER_TOKEN_BYTES; i++) {
        hex[i * 2]     = "0123456789abcdef"[raw[i] >> 4];
        hex[i * 2 + 1] = "0123456789abcdef"[raw[i] & 0x0f];
    }
    hex[SERVER_TOKEN_HEXLEN] = '\0';

    /* Store token */
    cfg->auth_token = strdup(hex);
    if (!cfg->auth_token)
        return -1;

    /* Build token file path: <socket_path>.token */
    pathlen = strlen(cfg->socket_path) + 7; /* ".token\0" */
    cfg->auth_file = malloc(pathlen);
    if (!cfg->auth_file)
        return -1;
    snprintf(cfg->auth_file, pathlen, "%s.token", cfg->socket_path);

    /* Remove stale token file if present */
    unlink(cfg->auth_file);

    /* Write token to file with restrictive permissions.
       Use open()+write() instead of fopen() to set mode atomically. */
    token_fd = open(cfg->auth_file, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (token_fd < 0) {
        fprintf(stderr, "bash-server: cannot create %s: %s\n",
                cfg->auth_file, strerror(errno));
        return -1;
    }

    n = write(token_fd, hex, SERVER_TOKEN_HEXLEN);
    if (n != SERVER_TOKEN_HEXLEN) {
        fprintf(stderr, "bash-server: short write to %s\n", cfg->auth_file);
        close(token_fd);
        unlink(cfg->auth_file);
        return -1;
    }
    /* Write trailing newline for easy `cat` / shell read */
    write(token_fd, "\n", 1);
    close(token_fd);

    /* Clear raw bytes from stack */
    memset(raw, 0, sizeof(raw));

    return 0;
}

/* Handle a client connection */
static void
handle_client(int client_fd, server_config_t *cfg)
{
    client_session_t session;
    
    if (session_init(&session, client_fd) < 0) {
        close(client_fd);
        return;
    }
    
    session_handle(&session, cfg);
    session_cleanup(&session);
}

/* Main entry point */
int
main(int argc, char **argv)
{
    int client_fd;
    
    /* Parse arguments */
    if (parse_arguments(argc, argv, &config) < 0) {
        print_usage(argv[0]);
        exit(1);
    }
    
    /* Daemonize if requested */
    if (config.daemon_mode) {
        if (server_daemonize() < 0) {
            fprintf(stderr, "bash-server: failed to daemonize: %s\n", strerror(errno));
            exit(1);
        }
    }
    
    /* Resolve socket path: CLI > env > config file > default */
    if (resolve_socket_path(&config) < 0) {
        fprintf(stderr, "bash-server: failed to resolve socket path\n");
        exit(1);
    }

    /* Set up signal handlers */
    setup_signals();

    /* Create Unix domain socket */
    server_fd = server_socket_create(config.socket_path, config.no_peercred);
    if (server_fd < 0) {
        fprintf(stderr, "bash-server: failed to create socket %s: %s\n",
                config.socket_path, strerror(errno));
        exit(1);
    }

    /* Generate auth token and write to <socket>.token */
    if (generate_auth_token(&config) < 0) {
        fprintf(stderr, "bash-server: failed to generate auth token\n");
        server_socket_close(server_fd, config.socket_path);
        exit(1);
    }

    if (config.verbose) {
        fprintf(stderr, "bash-server: listening on %s\n", config.socket_path);
        fprintf(stderr, "bash-server: auth token in %s\n", config.auth_file);
    }

    /* Write PID file */
    if (config.pid_file) {
        write_pid_file(config.pid_file);
    }
    
    /* Main accept loop */
    while (server_running) {
        /* Reap any zombie children */
        if (got_sigchld) {
            reap_children();
        }
        
        /* Accept new connection */
        client_fd = server_accept_client(server_fd);
        if (client_fd < 0) {
            if (errno == EINTR)
                continue;
            if (!server_running)
                break;
            fprintf(stderr, "bash-server: accept failed: %s (errno=%d)\n", strerror(errno), errno);
            fflush(stderr);
            continue;
        }

        fprintf(stderr, "bash-server: client connected (fd=%d)\n", client_fd);
        fflush(stderr);
        
        /* Handle client directly (single-threaded for now) */
        handle_client(client_fd, &config);
    }
    
    /* Cleanup */
    server_shutdown();
    
    if (config.verbose) {
        fprintf(stderr, "bash-server: shutdown complete\n");
    }
    
    return 0;
}
