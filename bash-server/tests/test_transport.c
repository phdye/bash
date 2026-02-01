/* test_transport.c -- Integration tests for stdio and socketpair transports
 *
 * Tests that bash-server works with --fd (socketpair) and --stdio modes.
 * Launches bash-server as a child process with the appropriate transport,
 * sends protocol commands, and verifies responses.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <errno.h>

/* Base64 decode (minimal, for test use) */
static const unsigned char b64d[256] = {
    ['A']=0,['B']=1,['C']=2,['D']=3,['E']=4,['F']=5,['G']=6,['H']=7,
    ['I']=8,['J']=9,['K']=10,['L']=11,['M']=12,['N']=13,['O']=14,['P']=15,
    ['Q']=16,['R']=17,['S']=18,['T']=19,['U']=20,['V']=21,['W']=22,['X']=23,
    ['Y']=24,['Z']=25,['a']=26,['b']=27,['c']=28,['d']=29,['e']=30,['f']=31,
    ['g']=32,['h']=33,['i']=34,['j']=35,['k']=36,['l']=37,['m']=38,['n']=39,
    ['o']=40,['p']=41,['q']=42,['r']=43,['s']=44,['t']=45,['u']=46,['v']=47,
    ['w']=48,['x']=49,['y']=50,['z']=51,['0']=52,['1']=53,['2']=54,['3']=55,
    ['4']=56,['5']=57,['6']=58,['7']=59,['8']=60,['9']=61,['+']=62,['/']=63,
};

static int b64_decode(const char *in, char *out, size_t outsize) {
    size_t len = strlen(in), j = 0;
    for (size_t i = 0; i + 3 < len && j + 2 < outsize; i += 4) {
        unsigned char a=b64d[(unsigned char)in[i]], b=b64d[(unsigned char)in[i+1]];
        unsigned char c=b64d[(unsigned char)in[i+2]], d=b64d[(unsigned char)in[i+3]];
        out[j++] = (a<<2)|(b>>4);
        if (in[i+2]!='=') out[j++] = ((b&0xf)<<4)|(c>>2);
        if (in[i+3]!='=') out[j++] = ((c&3)<<6)|d;
    }
    out[j] = '\0';
    return (int)j;
}

/* Read a line from fd into buf, return length or -1 */
static int read_line(int fd, char *buf, size_t size) {
    size_t pos = 0;
    char c;
    ssize_t n;
    while (pos < size - 1) {
        n = read(fd, &c, 1);
        if (n <= 0) return -1;
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    return (int)pos;
}

/* Write a string to fd */
static int write_str(int fd, const char *s) {
    size_t len = strlen(s);
    ssize_t n;
    while (len > 0) {
        n = write(fd, s, len);
        if (n <= 0) return -1;
        s += n;
        len -= n;
    }
    return 0;
}

static int passed = 0, failed = 0;
#define PASS(msg) do { printf("  %-55s PASS\n", msg); passed++; } while(0)
#define FAIL(msg, reason) do { printf("  %-55s FAIL: %s\n", msg, reason); failed++; } while(0)

/* Find the bash-server binary relative to this test */
static const char *find_server(void) {
    static char path[4096];
    /* Try ../bash-server.exe (when run from tests/) */
    if (access("../bash-server.exe", X_OK) == 0)
        return "../bash-server.exe";
    /* Try absolute path */
    snprintf(path, sizeof(path), "%s/../bash-server.exe", getenv("PWD") ?: ".");
    if (access(path, X_OK) == 0)
        return path;
    return NULL;
}

/* Test --fd mode with socketpair */
static void test_fd_mode(const char *server_path)
{
    int sv[2];  /* socketpair */
    int auth_pipe[2];  /* pipe for auth token delivery */
    pid_t pid;
    char line[8192], decoded[4096], token[128];
    char auth_cmd[256];
    int status;

    /* Create socketpair for data, pipe for auth token */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        FAIL("fd mode: socketpair creation", strerror(errno));
        return;
    }
    if (pipe(auth_pipe) < 0) {
        FAIL("fd mode: auth pipe creation", strerror(errno));
        close(sv[0]); close(sv[1]);
        return;
    }

    pid = fork();
    if (pid < 0) {
        FAIL("fd mode: fork", strerror(errno));
        return;
    }

    if (pid == 0) {
        /* Child: exec bash-server --fd sv[1] --auth-fd auth_pipe[1] */
        close(sv[0]);
        close(auth_pipe[0]);

        char fd_str[16], auth_fd_str[16];
        snprintf(fd_str, sizeof(fd_str), "%d", sv[1]);
        snprintf(auth_fd_str, sizeof(auth_fd_str), "%d", auth_pipe[1]);

        execl(server_path, "bash-server", "--fd", fd_str,
              "--auth-fd", auth_fd_str, "--norc", NULL);
        _exit(127);
    }

    /* Parent */
    close(sv[1]);
    close(auth_pipe[1]);

    /* Read token from auth pipe */
    int n = read_line(auth_pipe[0], line, sizeof(line));
    close(auth_pipe[0]);

    if (n < 0 || strncmp(line, "TOKEN ", 6) != 0) {
        FAIL("fd mode: token delivery", "no TOKEN line");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(sv[0]);
        return;
    }
    strncpy(token, line + 6, sizeof(token) - 1);
    token[sizeof(token) - 1] = '\0';

    /* AUTH */
    snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s\n", token);
    write_str(sv[0], auth_cmd);
    read_line(sv[0], line, sizeof(line));
    if (strcmp(line, "OK") != 0) {
        FAIL("fd mode: AUTH", line);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(sv[0]);
        return;
    }

    /* EVAL echo hello-fd */
    write_str(sv[0], "EVAL echo hello-fd\n");
    read_line(sv[0], line, sizeof(line));  /* STDOUT ... */
    if (strncmp(line, "STDOUT ", 7) == 0) {
        b64_decode(line + 7, decoded, sizeof(decoded));
        /* trim trailing newline */
        size_t dlen = strlen(decoded);
        while (dlen > 0 && (decoded[dlen-1] == '\n' || decoded[dlen-1] == '\r'))
            decoded[--dlen] = '\0';
    } else {
        decoded[0] = '\0';
    }
    read_line(sv[0], line, sizeof(line));  /* STDERR */
    read_line(sv[0], line, sizeof(line));  /* EXIT N */

    if (strcmp(decoded, "hello-fd") == 0 && strncmp(line, "EXIT 0", 6) == 0) {
        PASS("fd mode: EVAL echo hello-fd");
    } else {
        char reason[256];
        snprintf(reason, sizeof(reason), "got '%s', exit='%s'", decoded, line);
        FAIL("fd mode: EVAL echo hello-fd", reason);
    }

    /* QUIT */
    write_str(sv[0], "QUIT\n");
    read_line(sv[0], line, sizeof(line));
    if (strcmp(line, "BYE") == 0) {
        PASS("fd mode: QUIT → BYE");
    } else {
        FAIL("fd mode: QUIT → BYE", line);
    }

    close(sv[0]);
    waitpid(pid, &status, 0);
}

/* Test --stdio mode with pipes */
static void test_stdio_mode(const char *server_path)
{
    int to_server[2];   /* parent writes → server reads (stdin) */
    int from_server[2]; /* server writes (stdout) → parent reads */
    int auth_pipe[2];   /* server writes token → parent reads */
    pid_t pid;
    char line[8192], decoded[4096], token[128];
    char auth_cmd[256];
    int status;

    if (pipe(to_server) < 0 || pipe(from_server) < 0 || pipe(auth_pipe) < 0) {
        FAIL("stdio mode: pipe creation", strerror(errno));
        return;
    }

    pid = fork();
    if (pid < 0) {
        FAIL("stdio mode: fork", strerror(errno));
        return;
    }

    if (pid == 0) {
        /* Child: set up stdin/stdout, exec bash-server --stdio */
        close(to_server[1]);
        close(from_server[0]);
        close(auth_pipe[0]);

        dup2(to_server[0], STDIN_FILENO);
        dup2(from_server[1], STDOUT_FILENO);
        close(to_server[0]);
        close(from_server[1]);

        char auth_fd_str[16];
        snprintf(auth_fd_str, sizeof(auth_fd_str), "%d", auth_pipe[1]);

        execl(server_path, "bash-server", "--stdio",
              "--auth-fd", auth_fd_str, "--norc", NULL);
        _exit(127);
    }

    /* Parent */
    close(to_server[0]);
    close(from_server[1]);
    close(auth_pipe[1]);

    /* Read token from auth pipe */
    int n = read_line(auth_pipe[0], line, sizeof(line));
    close(auth_pipe[0]);

    if (n < 0 || strncmp(line, "TOKEN ", 6) != 0) {
        FAIL("stdio mode: token delivery", "no TOKEN line");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(to_server[1]);
        close(from_server[0]);
        return;
    }
    strncpy(token, line + 6, sizeof(token) - 1);
    token[sizeof(token) - 1] = '\0';

    /* AUTH */
    snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s\n", token);
    write_str(to_server[1], auth_cmd);
    read_line(from_server[0], line, sizeof(line));
    if (strcmp(line, "OK") != 0) {
        FAIL("stdio mode: AUTH", line);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(to_server[1]);
        close(from_server[0]);
        return;
    }

    /* EVAL echo hello-stdio */
    write_str(to_server[1], "EVAL echo hello-stdio\n");
    read_line(from_server[0], line, sizeof(line));  /* STDOUT ... */
    if (strncmp(line, "STDOUT ", 7) == 0) {
        b64_decode(line + 7, decoded, sizeof(decoded));
        size_t dlen = strlen(decoded);
        while (dlen > 0 && (decoded[dlen-1] == '\n' || decoded[dlen-1] == '\r'))
            decoded[--dlen] = '\0';
    } else {
        decoded[0] = '\0';
    }
    read_line(from_server[0], line, sizeof(line));  /* STDERR */
    read_line(from_server[0], line, sizeof(line));  /* EXIT N */

    if (strcmp(decoded, "hello-stdio") == 0 && strncmp(line, "EXIT 0", 6) == 0) {
        PASS("stdio mode: EVAL echo hello-stdio");
    } else {
        char reason[256];
        snprintf(reason, sizeof(reason), "got '%s', exit='%s'", decoded, line);
        FAIL("stdio mode: EVAL echo hello-stdio", reason);
    }

    /* EVAL with variable persistence */
    write_str(to_server[1], "EVAL x=99\n");
    read_line(from_server[0], line, sizeof(line));  /* STDOUT */
    read_line(from_server[0], line, sizeof(line));  /* STDERR */
    read_line(from_server[0], line, sizeof(line));  /* EXIT */

    write_str(to_server[1], "EVAL echo $x\n");
    read_line(from_server[0], line, sizeof(line));  /* STDOUT ... */
    if (strncmp(line, "STDOUT ", 7) == 0) {
        b64_decode(line + 7, decoded, sizeof(decoded));
        size_t dlen = strlen(decoded);
        while (dlen > 0 && (decoded[dlen-1] == '\n' || decoded[dlen-1] == '\r'))
            decoded[--dlen] = '\0';
    } else {
        decoded[0] = '\0';
    }
    read_line(from_server[0], line, sizeof(line));  /* STDERR */
    read_line(from_server[0], line, sizeof(line));  /* EXIT */

    if (strcmp(decoded, "99") == 0) {
        PASS("stdio mode: variable persistence (x=99 → echo $x)");
    } else {
        char reason[256];
        snprintf(reason, sizeof(reason), "expected '99', got '%s'", decoded);
        FAIL("stdio mode: variable persistence (x=99 → echo $x)", reason);
    }

    /* QUIT */
    write_str(to_server[1], "QUIT\n");
    read_line(from_server[0], line, sizeof(line));
    if (strcmp(line, "BYE") == 0) {
        PASS("stdio mode: QUIT → BYE");
    } else {
        FAIL("stdio mode: QUIT → BYE", line);
    }

    close(to_server[1]);
    close(from_server[0]);
    waitpid(pid, &status, 0);
}

/* Test --stdio mode with NDJSON protocol (the bug scenario)
 *
 * This is the key regression test: NDJSON over pipes should work
 * after the pushback fix. Before the fix, recv(MSG_PEEK) fails
 * with ENOTSOCK, detection falls through to v1, and NDJSON auth
 * is rejected as "ERR unknown command". */
static void test_stdio_ndjson_mode(const char *server_path)
{
    int to_server[2];   /* parent writes → server reads (stdin) */
    int from_server[2]; /* server writes (stdout) → parent reads */
    int auth_pipe[2];   /* server writes token → parent reads */
    pid_t pid;
    char line[8192], token[128];
    int status;

    if (pipe(to_server) < 0 || pipe(from_server) < 0 || pipe(auth_pipe) < 0) {
        FAIL("stdio ndjson: pipe creation", strerror(errno));
        return;
    }

    pid = fork();
    if (pid < 0) {
        FAIL("stdio ndjson: fork", strerror(errno));
        return;
    }

    if (pid == 0) {
        /* Child: set up stdin/stdout, exec bash-server --stdio */
        close(to_server[1]);
        close(from_server[0]);
        close(auth_pipe[0]);

        dup2(to_server[0], STDIN_FILENO);
        dup2(from_server[1], STDOUT_FILENO);
        close(to_server[0]);
        close(from_server[1]);

        char auth_fd_str[16];
        snprintf(auth_fd_str, sizeof(auth_fd_str), "%d", auth_pipe[1]);

        execl(server_path, "bash-server", "--stdio",
              "--auth-fd", auth_fd_str, "--norc", NULL);
        _exit(127);
    }

    /* Parent */
    close(to_server[0]);
    close(from_server[1]);
    close(auth_pipe[1]);

    /* Read token from auth pipe */
    int n = read_line(auth_pipe[0], line, sizeof(line));
    close(auth_pipe[0]);

    if (n < 0 || strncmp(line, "TOKEN ", 6) != 0) {
        FAIL("stdio ndjson: token delivery", "no TOKEN line");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(to_server[1]);
        close(from_server[0]);
        return;
    }
    strncpy(token, line + 6, sizeof(token) - 1);
    token[sizeof(token) - 1] = '\0';

    /* Send NDJSON auth — this is the first byte the server sees.
       Before the fix, '{' causes recv(MSG_PEEK) → ENOTSOCK → detection
       falls through to v1, and this line is parsed as v1 command "{"
       which returns "ERR unknown command". */
    {
        char auth_msg[512];
        snprintf(auth_msg, sizeof(auth_msg),
            "{\"ch\":0,\"type\":\"auth\",\"token\":\"%s\"}\n", token);
        write_str(to_server[1], auth_msg);
    }

    /* Read auth response (should be NDJSON with auth_ok) */
    n = read_line(from_server[0], line, sizeof(line));
    if (n < 0) {
        FAIL("stdio ndjson: AUTH response", "no response (EOF)");
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        close(to_server[1]);
        close(from_server[0]);
        return;
    }

    if (strstr(line, "\"auth_ok\"") != NULL || strstr(line, "\"type\":\"auth_ok\"") != NULL) {
        PASS("stdio ndjson: AUTH → auth_ok");
    } else {
        char reason[512];
        snprintf(reason, sizeof(reason), "got: %.200s", line);
        FAIL("stdio ndjson: AUTH → auth_ok", reason);
        /* Continue anyway to clean up */
    }

    /* Send NDJSON eval: echo hello-ndjson */
    write_str(to_server[1],
        "{\"ch\":1,\"type\":\"eval\",\"command\":\"echo hello-ndjson\"}\n");

    /* Read stdout, stderr, complete responses */
    {
        int got_stdout = 0, got_complete = 0;
        char stdout_data[4096] = "", decoded[4096];
        int exit_code = -1;

        /* Read up to 3 response lines */
        for (int i = 0; i < 3; i++) {
            n = read_line(from_server[0], line, sizeof(line));
            if (n < 0) break;

            if (strstr(line, "\"type\":\"stdout\"") != NULL) {
                got_stdout = 1;
                /* Extract base64 data field */
                char *dp = strstr(line, "\"data\":\"");
                if (dp) {
                    dp += 8;
                    char *ep = strchr(dp, '"');
                    if (ep) {
                        *ep = '\0';
                        b64_decode(dp, decoded, sizeof(decoded));
                        /* Trim trailing newline */
                        size_t dlen = strlen(decoded);
                        while (dlen > 0 && (decoded[dlen-1] == '\n' || decoded[dlen-1] == '\r'))
                            decoded[--dlen] = '\0';
                        strncpy(stdout_data, decoded, sizeof(stdout_data) - 1);
                    }
                }
            } else if (strstr(line, "\"type\":\"complete\"") != NULL) {
                got_complete = 1;
                char *ep = strstr(line, "\"exit_code\":");
                if (ep) exit_code = atoi(ep + 12);
            }
        }

        if (got_stdout && got_complete &&
            strcmp(stdout_data, "hello-ndjson") == 0 && exit_code == 0) {
            PASS("stdio ndjson: EVAL echo hello-ndjson");
        } else {
            char reason[512];
            snprintf(reason, sizeof(reason),
                "stdout=%d data='%s' complete=%d exit=%d",
                got_stdout, stdout_data, got_complete, exit_code);
            FAIL("stdio ndjson: EVAL echo hello-ndjson", reason);
        }
    }

    /* Send disconnect */
    write_str(to_server[1], "{\"ch\":0,\"type\":\"disconnect\"}\n");
    n = read_line(from_server[0], line, sizeof(line));
    if (n >= 0 && strstr(line, "\"disconnect_ok\"") != NULL) {
        PASS("stdio ndjson: disconnect → disconnect_ok");
    } else {
        FAIL("stdio ndjson: disconnect → disconnect_ok",
             n >= 0 ? line : "EOF");
    }

    close(to_server[1]);
    close(from_server[0]);
    waitpid(pid, &status, 0);
}

int main(void)
{
    const char *server;

    alarm(60);  /* Safety timeout */

    server = find_server();
    if (!server) {
        fprintf(stderr, "ERROR: bash-server.exe not found\n");
        return 2;
    }

    printf("\n=== bash-server transport integration tests ===\n\n");

    printf("[socketpair / --fd mode]\n");
    test_fd_mode(server);

    printf("\n[stdio mode]\n");
    test_stdio_mode(server);

    printf("\n[stdio NDJSON mode]\n");
    test_stdio_ndjson_mode(server);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           passed, passed + failed, failed);

    return failed ? 1 : 0;
}
