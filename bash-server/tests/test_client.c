/* test_client.c -- Minimal raw-protocol client for integration testing
 *
 * Usage: test_client <socket_path> <command> [<arg>]
 *
 * Connects to the server, sends "command [arg]\n", reads one response line,
 * prints it to stdout, and exits.  For multi-exchange tests, use multiple
 * invocations or the special "multi" mode:
 *
 *   test_client <socket_path> multi "CMD1\nCMD2\nCMD3"
 *
 * In multi mode, sends each line and reads one response per line sent.
 *
 * Compile:
 *   gcc -Wall -o test_client test_client.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

static int
sock_connect(const char *path)
{
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "test_client: socket: %s\n", strerror(errno));
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "test_client: connect(%s): %s\n", path, strerror(errno));
        close(fd);
        return -1;
    }

    return fd;
}

/* Read one newline-terminated line; strip trailing \r\n */
static int
read_line(int fd, char *buf, size_t bufsz)
{
    size_t pos = 0;
    while (pos < bufsz - 1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0)
            break;
        if (c == '\n') {
            /* Strip trailing \r */
            if (pos > 0 && buf[pos - 1] == '\r')
                pos--;
            break;
        }
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    return (int)pos;
}

static void
send_line(int fd, const char *line)
{
    size_t len = strlen(line);
    write(fd, line, len);
    if (len == 0 || line[len - 1] != '\n')
        write(fd, "\n", 1);
}

/* Single command mode: send one command, read one response */
static int
mode_single(const char *path, const char *cmd, const char *arg)
{
    int fd;
    char sendbuf[8192];
    char recvbuf[8192];

    fd = sock_connect(path);
    if (fd < 0)
        return 1;

    if (arg && *arg)
        snprintf(sendbuf, sizeof(sendbuf), "%s %s", cmd, arg);
    else
        snprintf(sendbuf, sizeof(sendbuf), "%s", cmd);

    send_line(fd, sendbuf);
    read_line(fd, recvbuf, sizeof(recvbuf));
    printf("%s\n", recvbuf);

    close(fd);
    return 0;
}

/* Multi mode: takes newline-escaped string, sends each as a command,
 * reads one response per command.  Literal "\n" in the argument becomes
 * a command separator. */
static int
mode_multi(const char *path, const char *script)
{
    int fd;
    char recvbuf[8192];
    char line[8192];
    const char *p = script;

    fd = sock_connect(path);
    if (fd < 0)
        return 1;

    while (*p) {
        /* Extract next line (split on literal \n sequence) */
        size_t i = 0;
        while (*p && i < sizeof(line) - 1) {
            if (p[0] == '\\' && p[1] == 'n') {
                p += 2;
                break;
            }
            line[i++] = *p++;
        }
        line[i] = '\0';

        if (i == 0 && *p == '\0')
            break;  /* trailing \n */

        send_line(fd, line);
        read_line(fd, recvbuf, sizeof(recvbuf));
        printf("%s\n", recvbuf);
    }

    close(fd);
    return 0;
}

/* Multi-response mode: send one command, read ALL response lines until
 * connection closes or a line starting with "EXIT" is seen */
static int
mode_eval(const char *path, const char *auth_and_cmd)
{
    int fd;
    char recvbuf[8192];
    char line[8192];
    const char *p = auth_and_cmd;

    fd = sock_connect(path);
    if (fd < 0)
        return 1;

    /* Send all lines (AUTH + EVAL + QUIT) */
    while (*p) {
        size_t i = 0;
        while (*p && i < sizeof(line) - 1) {
            if (p[0] == '\\' && p[1] == 'n') {
                p += 2;
                break;
            }
            line[i++] = *p++;
        }
        line[i] = '\0';
        if (i == 0 && *p == '\0')
            break;
        send_line(fd, line);
    }

    /* Read all responses until EOF */
    while (read_line(fd, recvbuf, sizeof(recvbuf)) > 0) {
        printf("%s\n", recvbuf);
    }

    close(fd);
    return 0;
}

int
main(int argc, char **argv)
{
    alarm(30);  /* Safety timeout */

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <socket> <command> [arg]\n", argv[0]);
        fprintf(stderr, "       %s <socket> multi \"CMD1\\nCMD2\\nCMD3\"\n", argv[0]);
        fprintf(stderr, "       %s <socket> eval \"AUTH tok\\nEVAL cmd\\nQUIT\"\n", argv[0]);
        return 2;
    }

    if (strcmp(argv[2], "multi") == 0) {
        if (argc < 4) {
            fprintf(stderr, "multi mode requires script argument\n");
            return 2;
        }
        return mode_multi(argv[1], argv[3]);
    }

    if (strcmp(argv[2], "eval") == 0) {
        if (argc < 4) {
            fprintf(stderr, "eval mode requires script argument\n");
            return 2;
        }
        return mode_eval(argv[1], argv[3]);
    }

    return mode_single(argv[1], argv[2], argc > 3 ? argv[3] : NULL);
}
