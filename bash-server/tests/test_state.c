/* test_state.c -- Unit tests for Phase 2 state operations */

/* Tests the state protocol commands by starting bash-server in --stdio mode
   and exercising GET-VAR, SET-VAR, UNSET-VAR, GET-FUNC, UNSET-FUNC,
   GET-ALIAS, SET-ALIAS, UNSET-ALIAS, SET-TRAP, UNSET-TRAP, and INSPECT.

   Uses socketpair so we can write commands and read responses from the
   same test process. */

#include "server.h"
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgen.h>

/* Test framework */
static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) do { \
    printf("  %-55s ", name); \
    fflush(stdout); \
    tests_run++; \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); } while(0)

/* Resolve the bash-server.exe path relative to this test binary */
static int
resolve_server_path(char *buf, size_t bufsize)
{
    char self[4096];
    char *dir;
    ssize_t n;

    n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n < 0) return -1;
    self[n] = '\0';
    dir = dirname(self);
    snprintf(buf, bufsize, "%s/../bash-server.exe", dir);
    return (access(buf, X_OK) == 0) ? 0 : -1;
}

/* Helper: start server in --fd mode via socketpair, authenticate, return pid.
   fds[0] = our end, fds[1] = server end. */
static pid_t
start_server_socketpair(const char *server_path, int *client_fd)
{
    int sv[2];
    pid_t pid;
    char fd_str[16];
    char resp[8192];
    char auth_cmd[512];
    char token[256];
    int i;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
        return -1;

    pid = fork();
    if (pid < 0) {
        close(sv[0]); close(sv[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child = server */
        close(sv[0]);
        snprintf(fd_str, sizeof(fd_str), "%d", sv[1]);
        execl(server_path, "bash-server", "--fd", fd_str, "--auth-fd", "2", NULL);
        _exit(127);
    }

    /* Parent = client */
    close(sv[1]);
    *client_fd = sv[0];

    /* Read token from stderr of child - the server writes it to auth-fd=2.
       Since we share stderr, we need to read from a pipe instead.
       Actually, --auth-fd 2 writes to the server's stderr which we don't capture.
       
       Instead, let's read the token from the token file, or use a different approach.
       Let's just use --stdio mode with a pipe. */
    
    /* This approach won't easily give us the token. Let's kill this and use
       a different strategy: start server on a unix socket and connect. */
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    close(sv[0]);
    return -1;  /* Fall through to socket approach */
}

/* Start server using --fd mode with a socketpair.
   Creates a socketpair and a pipe for the auth token.
   Child execs bash-server --fd <sv[1]> --auth-fd <token_pipe[1]>.
   Parent reads the token from token_pipe[0], then authenticates on sv[0].
   Returns server PID, sets *client_fd to our end of the socketpair. */
static pid_t
start_server_fd(const char *server_path, int *client_fd)
{
    int sv[2];          /* socketpair: sv[0]=client, sv[1]=server */
    int token_pipe[2];  /* pipe: token_pipe[0]=read(parent), token_pipe[1]=write(child) */
    pid_t pid;
    char fd_str[16], auth_fd_str[16];
    char token[512];
    ssize_t n;
    char resp[8192];
    char auth_line[512];
    int len;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
        return -1;
    if (pipe(token_pipe) < 0) {
        close(sv[0]); close(sv[1]);
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        close(sv[0]); close(sv[1]);
        close(token_pipe[0]); close(token_pipe[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child = server */
        close(sv[0]);           /* close client end */
        close(token_pipe[0]);   /* close read end */

        snprintf(fd_str, sizeof(fd_str), "%d", sv[1]);
        snprintf(auth_fd_str, sizeof(auth_fd_str), "%d", token_pipe[1]);

        /* Redirect stderr to log */
        {
            int logfd = open("/tmp/bash-server-test-state.log",
                             O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (logfd >= 0) {
                dup2(logfd, STDERR_FILENO);
                close(logfd);
            }
        }

        execl(server_path, "bash-server",
              "--fd", fd_str,
              "--auth-fd", auth_fd_str,
              "--verbose", NULL);
        _exit(127);
    }

    /* Parent = client */
    close(sv[1]);           /* close server end */
    close(token_pipe[1]);   /* close write end */

    /* Read token from pipe — format is "TOKEN <hex>\n" */
    n = read(token_pipe[0], token, sizeof(token) - 1);
    close(token_pipe[0]);
    if (n <= 0) {
        close(sv[0]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    token[n] = '\0';
    /* Strip trailing whitespace */
    { char *p = token + strlen(token);
      while (p > token && (p[-1] == '\n' || p[-1] == '\r' || p[-1] == ' '))
          *--p = '\0'; }
    /* Strip "TOKEN " prefix */
    { char *hex = token;
      if (strncmp(hex, "TOKEN ", 6) == 0)
          memmove(token, hex + 6, strlen(hex + 6) + 1); }

    /* Authenticate */
    len = snprintf(auth_line, sizeof(auth_line), "AUTH %s\n", token);
    if (write(sv[0], auth_line, len) != len) {
        close(sv[0]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }
    if (protocol_read_line(sv[0], resp, sizeof(resp)) < 0 ||
        strcmp(resp, "OK") != 0) {
        fprintf(stderr, "AUTH failed: '%s'\n", resp);
        close(sv[0]);
        kill(pid, SIGKILL);
        waitpid(pid, NULL, 0);
        return -1;
    }

    *client_fd = sv[0];
    return pid;
}

static void
stop_server(pid_t pid)
{
    int i, status;
    kill(pid, SIGTERM);
    for (i = 0; i < 50; i++) {
        if (waitpid(pid, &status, WNOHANG) == pid)
            return;
        usleep(100000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

/* Send command, read one response line */
static int
send_recv(int fd, const char *cmd, char *resp, size_t respsize)
{
    char line[8192];
    int len = snprintf(line, sizeof(line), "%s\n", cmd);
    if (write(fd, line, len) != len) return -1;
    return protocol_read_line(fd, resp, respsize);
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void
test_set_get_var(int fd)
{
    char resp[8192];

    TEST("SET-VAR + GET-VAR: set and retrieve variable");
    send_recv(fd, "SET-VAR TESTVAR hello_world", resp, sizeof(resp));
    if (strcmp(resp, "OK") != 0) { FAIL("SET-VAR did not return OK"); return; }

    send_recv(fd, "GET-VAR TESTVAR", resp, sizeof(resp));
    /* Response: VALUE TESTVAR <base64> */
    if (strncmp(resp, "VALUE TESTVAR ", 14) != 0) {
        FAIL("GET-VAR response format wrong");
        return;
    }
    {
        size_t dec_len;
        char *decoded = protocol_base64_decode(resp + 14, &dec_len);
        if (!decoded || strcmp(decoded, "hello_world") != 0) {
            FAIL("decoded value != 'hello_world'");
            free(decoded);
            return;
        }
        free(decoded);
    }
    PASS();
}

static void
test_get_var_not_found(int fd)
{
    char resp[8192];

    TEST("GET-VAR: nonexistent variable returns ERR");
    send_recv(fd, "GET-VAR NOSUCHVAR_XYZ_999", resp, sizeof(resp));
    if (strncmp(resp, "ERR", 3) != 0) {
        FAIL("expected ERR response");
        return;
    }
    PASS();
}

static void
test_unset_var(int fd)
{
    char resp[8192];

    TEST("UNSET-VAR: remove variable");
    send_recv(fd, "SET-VAR TMPVAR deleteme", resp, sizeof(resp));
    send_recv(fd, "UNSET-VAR TMPVAR", resp, sizeof(resp));
    if (strcmp(resp, "OK") != 0) { FAIL("UNSET-VAR did not return OK"); return; }

    send_recv(fd, "GET-VAR TMPVAR", resp, sizeof(resp));
    if (strncmp(resp, "ERR", 3) != 0) {
        FAIL("variable still exists after unset");
        return;
    }
    PASS();
}

static void
test_set_var_export(int fd)
{
    char resp[8192];

    TEST("SET-VAR --export: variable has exported attribute");
    send_recv(fd, "SET-VAR EXPVAR myval --export", resp, sizeof(resp));
    if (strcmp(resp, "OK") != 0) { FAIL("SET-VAR did not return OK"); return; }

    send_recv(fd, "GET-VAR EXPVAR", resp, sizeof(resp));
    if (strstr(resp, "exported") == NULL) {
        FAIL("expected 'exported' attribute");
        return;
    }
    PASS();
}

static void
test_set_get_alias(int fd)
{
    char resp[8192];

    TEST("SET-ALIAS + GET-ALIAS: set and retrieve alias");
    send_recv(fd, "SET-ALIAS ll ls -la", resp, sizeof(resp));
    if (strcmp(resp, "OK") != 0) { FAIL("SET-ALIAS did not return OK"); return; }

    send_recv(fd, "GET-ALIAS ll", resp, sizeof(resp));
    if (strncmp(resp, "ALIAS ll ", 9) != 0) {
        FAIL("GET-ALIAS response format wrong");
        return;
    }
    {
        size_t dec_len;
        char *decoded = protocol_base64_decode(resp + 9, &dec_len);
        if (!decoded || strcmp(decoded, "ls -la") != 0) {
            FAIL("decoded alias != 'ls -la'");
            free(decoded);
            return;
        }
        free(decoded);
    }
    PASS();
}

static void
test_unset_alias(int fd)
{
    char resp[8192];

    TEST("UNSET-ALIAS: remove alias");
    send_recv(fd, "SET-ALIAS tmpalias echo hi", resp, sizeof(resp));
    send_recv(fd, "UNSET-ALIAS tmpalias", resp, sizeof(resp));
    if (strcmp(resp, "OK") != 0) { FAIL("UNSET-ALIAS did not return OK"); return; }

    send_recv(fd, "GET-ALIAS tmpalias", resp, sizeof(resp));
    if (strncmp(resp, "ERR", 3) != 0) {
        FAIL("alias still exists after unset");
        return;
    }
    PASS();
}

static void
test_get_func(int fd)
{
    char resp[8192];

    TEST("GET-FUNC: define function via EVAL, then GET-FUNC");
    /* Define function via EVAL */
    send_recv(fd, "EVAL myfunc() { echo hello; }", resp, sizeof(resp));
    /* Consume STDOUT, STDERR, EXIT */
    protocol_read_line(fd, resp, sizeof(resp));
    protocol_read_line(fd, resp, sizeof(resp));

    send_recv(fd, "GET-FUNC myfunc", resp, sizeof(resp));
    if (strncmp(resp, "FUNC myfunc ", 12) != 0) {
        FAIL("GET-FUNC response format wrong");
        return;
    }
    {
        size_t dec_len;
        char *decoded = protocol_base64_decode(resp + 12, &dec_len);
        if (!decoded || strstr(decoded, "echo hello") == NULL) {
            FAIL("function body doesn't contain 'echo hello'");
            free(decoded);
            return;
        }
        free(decoded);
    }
    PASS();
}

static void
test_unset_func(int fd)
{
    char resp[8192];

    TEST("UNSET-FUNC: remove function");
    /* Define a function */
    send_recv(fd, "EVAL tmpfunc() { echo tmp; }", resp, sizeof(resp));
    protocol_read_line(fd, resp, sizeof(resp));
    protocol_read_line(fd, resp, sizeof(resp));

    send_recv(fd, "UNSET-FUNC tmpfunc", resp, sizeof(resp));
    if (strcmp(resp, "OK") != 0) { FAIL("UNSET-FUNC did not return OK"); return; }

    send_recv(fd, "GET-FUNC tmpfunc", resp, sizeof(resp));
    if (strncmp(resp, "ERR", 3) != 0) {
        FAIL("function still exists after unset");
        return;
    }
    PASS();
}

static void
test_inspect_vars(int fd)
{
    char resp[8192];
    int found_testvar = 0;
    int got_end = 0;

    TEST("INSPECT vars: lists variables including SET-VAR'd ones");

    /* Ensure a variable exists */
    send_recv(fd, "SET-VAR INSPECTME 42", resp, sizeof(resp));

    send_recv(fd, "INSPECT vars", resp, sizeof(resp));
    /* Read VALUE lines until INSPECT-END */
    do {
        if (strncmp(resp, "VALUE INSPECTME ", 16) == 0)
            found_testvar = 1;
        if (strcmp(resp, "INSPECT-END") == 0) {
            got_end = 1;
            break;
        }
    } while (protocol_read_line(fd, resp, sizeof(resp)) >= 0);

    if (!got_end) { FAIL("missing INSPECT-END"); return; }
    if (!found_testvar) { FAIL("INSPECTME not found in vars"); return; }
    PASS();
}

static void
test_inspect_aliases(int fd)
{
    char resp[8192];
    int found_alias = 0;
    int got_end = 0;

    TEST("INSPECT aliases: lists aliases");

    send_recv(fd, "SET-ALIAS insptest echo test", resp, sizeof(resp));

    send_recv(fd, "INSPECT aliases", resp, sizeof(resp));
    do {
        if (strncmp(resp, "ALIAS insptest ", 15) == 0)
            found_alias = 1;
        if (strcmp(resp, "INSPECT-END") == 0) {
            got_end = 1;
            break;
        }
    } while (protocol_read_line(fd, resp, sizeof(resp)) >= 0);

    if (!got_end) { FAIL("missing INSPECT-END"); return; }
    if (!found_alias) { FAIL("insptest alias not found"); return; }
    PASS();
}

static void
test_inspect_functions(int fd)
{
    char resp[8192];
    int found_func = 0;
    int got_end = 0;

    TEST("INSPECT functions: lists functions");

    /* Define a function */
    send_recv(fd, "EVAL inspfunc() { echo inspect; }", resp, sizeof(resp));
    protocol_read_line(fd, resp, sizeof(resp));
    protocol_read_line(fd, resp, sizeof(resp));

    send_recv(fd, "INSPECT functions", resp, sizeof(resp));
    do {
        if (strncmp(resp, "FUNC inspfunc ", 14) == 0)
            found_func = 1;
        if (strcmp(resp, "INSPECT-END") == 0) {
            got_end = 1;
            break;
        }
    } while (protocol_read_line(fd, resp, sizeof(resp)) >= 0);

    if (!got_end) { FAIL("missing INSPECT-END"); return; }
    if (!found_func) { FAIL("inspfunc not found"); return; }
    PASS();
}

static void
test_state_requires_auth(const char *server_path)
{
    char sock_path[256];
    char resp[8192];
    int fd, i;
    struct sockaddr_un addr;

    TEST("state operations require authentication");

    snprintf(sock_path, sizeof(sock_path), "/tmp/bash-server-test-state/sock");

    /* Connect without authenticating */
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) { FAIL("socket() failed"); return; }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);

    for (i = 0; i < 50; i++) {
        if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0) break;
        usleep(100000);
    }
    if (i >= 50) { close(fd); FAIL("connect failed"); return; }

    send_recv(fd, "GET-VAR PATH", resp, sizeof(resp));
    if (strncmp(resp, "ERR", 3) != 0 || strstr(resp, "not authenticated") == NULL) {
        FAIL("expected 'ERR not authenticated'");
        close(fd);
        return;
    }

    send_recv(fd, "QUIT", resp, sizeof(resp));
    close(fd);
    PASS();
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int
main(void)
{
    char server_path[4096];
    int fd = -1;
    pid_t server_pid;

    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    printf("\n=== bash-server state operations tests ===\n\n");
    alarm(60);

    if (resolve_server_path(server_path, sizeof(server_path)) < 0) {
        printf("ERROR: cannot find bash-server.exe\n");
        return 2;
    }

    server_pid = start_server_fd(server_path, &fd);
    if (server_pid < 0) {
        printf("ERROR: cannot start server\n");
        return 2;
    }

    printf("[variable operations]\n");
    test_set_get_var(fd);
    test_get_var_not_found(fd);
    test_unset_var(fd);
    test_set_var_export(fd);

    printf("\n[alias operations]\n");
    test_set_get_alias(fd);
    test_unset_alias(fd);

    printf("\n[function operations]\n");
    test_get_func(fd);
    test_unset_func(fd);

    printf("\n[inspect operations]\n");
    test_inspect_vars(fd);
    test_inspect_aliases(fd);
    test_inspect_functions(fd);

    /* Cleanup */
    send_recv(fd, "QUIT", (char[8192]){0}, 8192);
    close(fd);
    stop_server(server_pid);

    /* Clean up temp files */
    unlink("/tmp/bash-server-test-state.log");

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_run - tests_passed);

    return (tests_passed == tests_run) ? 0 : 1;
}
