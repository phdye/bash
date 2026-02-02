/* test_winpipe.c -- Integration tests for Windows Named Pipe transport */

/* Tests the --named-pipe transport by:
   1. Starting bash-server with --named-pipe <name>
   2. Connecting via CreateFileW (Win32 API)
   3. Running AUTH + EVAL + QUIT over the pipe
   4. Verifying output

   Only built/run on Cygwin. */

#ifdef __CYGWIN__

#include <windows.h>
#include <sys/cygwin.h>

#include "server.h"
#include <sys/wait.h>
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

/* Resolve the bash-server.exe path relative to this test binary.
   Uses /proc/self/exe to find our own path, then navigates to ../bash-server.exe */
static int
resolve_server_path(char *buf, size_t bufsize)
{
    char self[4096];
    char *dir;
    ssize_t n;

    n = readlink("/proc/self/exe", self, sizeof(self) - 1);
    if (n < 0)
        return -1;
    self[n] = '\0';

    dir = dirname(self);
    snprintf(buf, bufsize, "%s/../bash-server.exe", dir);
    return (access(buf, X_OK) == 0) ? 0 : -1;
}

/* Read the token file, stripping trailing whitespace */
static int
read_token(const char *path, char *buf, size_t bufsize)
{
    FILE *fp;
    char *p;

    fp = fopen(path, "r");
    if (!fp)
        return -1;

    if (!fgets(buf, bufsize, fp)) {
        fclose(fp);
        return -1;
    }
    fclose(fp);

    p = buf + strlen(buf);
    while (p > buf && (p[-1] == '\n' || p[-1] == '\r' || p[-1] == ' '))
        *--p = '\0';

    return 0;
}

/* Connect to a named pipe, return a Cygwin fd */
static int
connect_pipe(const char *name)
{
    wchar_t pipe_path[256];
    HANDLE h;
    int fd;

    swprintf(pipe_path, 256, L"\\\\.\\pipe\\bash-server-%hs", name);

    h = CreateFileW(
        pipe_path,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        0,
        NULL
    );

    if (h == INVALID_HANDLE_VALUE)
        return -1;

    fd = cygwin_attach_handle_to_fd(NULL, -1, h,
                                     TRUE, GENERIC_READ | GENERIC_WRITE);
    if (fd < 0) {
        CloseHandle(h);
        return -1;
    }

    return fd;
}

/* Send a line and read response */
static int
send_recv(int fd, const char *cmd, char *resp, size_t respsize)
{
    char line[8192];
    int len;

    len = snprintf(line, sizeof(line), "%s\n", cmd);
    if (write(fd, line, len) != len)
        return -1;

    return protocol_read_line(fd, resp, respsize);
}

/* Resolve token file path for a pipe name */
static void
build_token_path(const char *name, char *buf, size_t bufsize)
{
    const char *xdg = getenv("XDG_RUNTIME_DIR");
    if (xdg && *xdg)
        snprintf(buf, bufsize, "%s/bash-server/%s.token", xdg, name);
    else
        snprintf(buf, bufsize, "/tmp/bash-server-%d/%s.token",
                 (int)getuid(), name);
}

/* Start bash-server with --named-pipe, wait for it to be ready.
   Returns server PID, or -1 on failure.

   Uses system() with & to start the server as a truly independent
   background process.  fork+exec from Cygwin can leave shared state
   between parent and child (Win32 handles, Cygwin internal fds) that
   causes named pipe I/O to hang during capture_output's dup2 cycle. */
static pid_t
start_server(const char *server_path, const char *pipe_name,
             const char *token_path)
{
    char cmd[4096];
    char pidfile[256];
    int i;
    pid_t pid;
    FILE *fp;

    snprintf(pidfile, sizeof(pidfile), "/tmp/bash-server-%s.pid", pipe_name);
    unlink(pidfile);

    snprintf(cmd, sizeof(cmd),
             "%s --named-pipe %s --verbose "
             "</dev/null >/dev/null 2>/tmp/bash-server-%s.log & "
             "echo $! > %s",
             server_path, pipe_name, pipe_name, pidfile);

    system(cmd);

    /* Wait for pid file + token file (up to 10s) */
    for (i = 0; i < 100; i++) {
        if (access(token_path, R_OK) == 0 && access(pidfile, R_OK) == 0) {
            fp = fopen(pidfile, "r");
            if (fp) {
                if (fscanf(fp, "%d", &pid) == 1) {
                    fclose(fp);
                    return pid;
                }
                fclose(fp);
            }
        }
        usleep(100000);
    }

    /* Timeout — try to read pid to kill */
    fp = fopen(pidfile, "r");
    if (fp) {
        if (fscanf(fp, "%d", &pid) == 1) {
            fclose(fp);
            kill(pid, SIGKILL);
            return -1;
        }
        fclose(fp);
    }
    return -1;
}

/* Stop server and cleanup */
static void
stop_server(pid_t pid, const char *token_path)
{
    int i, status;

    kill(pid, SIGTERM);
    for (i = 0; i < 50; i++) {
        if (waitpid(pid, &status, WNOHANG) == pid)
            goto done;
        usleep(100000);
    }
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
done:
    unlink(token_path);
}

/* ------------------------------------------------------------------ */
/* Test: connect to named pipe, AUTH, EVAL echo hello, QUIT           */
/* ------------------------------------------------------------------ */
static void
test_named_pipe_eval(const char *server_path)
{
    const char *pipe_name = "unittest1";
    char token_path[4096];
    char token[256];
    char resp[8192];
    pid_t server_pid;
    int pipe_fd, i;

    TEST("named-pipe: AUTH + EVAL echo hello + QUIT");

    build_token_path(pipe_name, token_path, sizeof(token_path));
    unlink(token_path);  /* remove stale */

    server_pid = start_server(server_path, pipe_name, token_path);
    if (server_pid < 0) {
        FAIL("server failed to start");
        return;
    }

    if (read_token(token_path, token, sizeof(token)) < 0) {
        FAIL("cannot read token file");
        stop_server(server_pid, token_path);
        return;
    }

    /* Connect to the pipe (retry up to 5s) */
    pipe_fd = -1;
    for (i = 0; i < 50; i++) {
        pipe_fd = connect_pipe(pipe_name);
        if (pipe_fd >= 0)
            break;
        usleep(100000);
    }
    if (pipe_fd < 0) {
        FAIL("cannot connect to named pipe");
        stop_server(server_pid, token_path);
        return;
    }

    /* AUTH */
    {
        char auth_cmd[300];
        snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s", token);
        if (send_recv(pipe_fd, auth_cmd, resp, sizeof(resp)) < 0) {
            FAIL("AUTH send/recv failed");
            goto cleanup;
        }
        if (strcmp(resp, "OK") != 0) {
            FAIL("AUTH did not return OK");
            goto cleanup;
        }
    }

    /* EVAL echo hello-pipe */
    if (send_recv(pipe_fd, "EVAL echo hello-pipe", resp, sizeof(resp)) < 0) {
        FAIL("EVAL send failed");
        goto cleanup;
    }
    if (strncmp(resp, "STDOUT ", 7) != 0) {
        FAIL("expected STDOUT response");
        goto cleanup;
    }
    {
        size_t dec_len;
        char *decoded = protocol_base64_decode(resp + 7, &dec_len);
        if (!decoded || strncmp(decoded, "hello-pipe", 10) != 0) {
            FAIL("decoded output != 'hello-pipe'");
            free(decoded);
            goto cleanup;
        }
        free(decoded);
    }

    /* Consume STDERR + EXIT lines */
    protocol_read_line(pipe_fd, resp, sizeof(resp));
    protocol_read_line(pipe_fd, resp, sizeof(resp));
    if (strncmp(resp, "EXIT 0", 6) != 0) {
        FAIL("expected EXIT 0");
        goto cleanup;
    }

    /* QUIT */
    if (send_recv(pipe_fd, "QUIT", resp, sizeof(resp)) < 0) {
        FAIL("QUIT send/recv failed");
        goto cleanup;
    }
    if (strcmp(resp, "BYE") != 0) {
        FAIL("QUIT did not return BYE");
        goto cleanup;
    }

    PASS();

cleanup:
    close(pipe_fd);
    stop_server(server_pid, token_path);
}

/* ------------------------------------------------------------------ */
/* Test: variable persistence over named pipe                         */
/* ------------------------------------------------------------------ */
static void
test_named_pipe_persistence(const char *server_path)
{
    const char *pipe_name = "unittest2";
    char token_path[4096];
    char token[256];
    char resp[8192];
    pid_t server_pid;
    int pipe_fd, i;

    TEST("named-pipe: variable persistence across EVALs");

    build_token_path(pipe_name, token_path, sizeof(token_path));
    unlink(token_path);

    server_pid = start_server(server_path, pipe_name, token_path);
    if (server_pid < 0) {
        FAIL("server failed to start");
        return;
    }

    if (read_token(token_path, token, sizeof(token)) < 0) {
        FAIL("cannot read token");
        stop_server(server_pid, token_path);
        return;
    }

    pipe_fd = -1;
    for (i = 0; i < 50; i++) {
        pipe_fd = connect_pipe(pipe_name);
        if (pipe_fd >= 0) break;
        usleep(100000);
    }
    if (pipe_fd < 0) {
        FAIL("cannot connect");
        stop_server(server_pid, token_path);
        return;
    }

    /* AUTH */
    {
        char auth_cmd[300];
        snprintf(auth_cmd, sizeof(auth_cmd), "AUTH %s", token);
        send_recv(pipe_fd, auth_cmd, resp, sizeof(resp));
        if (strcmp(resp, "OK") != 0) {
            FAIL("AUTH failed");
            goto cleanup2;
        }
    }

    /* EVAL x=77 */
    send_recv(pipe_fd, "EVAL x=77", resp, sizeof(resp));
    protocol_read_line(pipe_fd, resp, sizeof(resp));  /* STDERR */
    protocol_read_line(pipe_fd, resp, sizeof(resp));  /* EXIT */

    /* EVAL echo $x */
    send_recv(pipe_fd, "EVAL echo $x", resp, sizeof(resp));
    if (strncmp(resp, "STDOUT ", 7) != 0) {
        FAIL("expected STDOUT");
        goto cleanup2;
    }
    {
        size_t dec_len;
        char *decoded = protocol_base64_decode(resp + 7, &dec_len);
        if (!decoded || strncmp(decoded, "77", 2) != 0) {
            FAIL("variable not persisted");
            free(decoded);
            goto cleanup2;
        }
        free(decoded);
    }
    protocol_read_line(pipe_fd, resp, sizeof(resp));  /* STDERR */
    protocol_read_line(pipe_fd, resp, sizeof(resp));  /* EXIT */

    /* QUIT */
    send_recv(pipe_fd, "QUIT", resp, sizeof(resp));

    PASS();

cleanup2:
    close(pipe_fd);
    stop_server(server_pid, token_path);
}

int
main(void)
{
    char server_path[4096];

    printf("\n=== bash-server named pipe integration tests ===\n\n");

    alarm(60);  /* global safety timeout */

    if (resolve_server_path(server_path, sizeof(server_path)) < 0) {
        printf("ERROR: cannot find bash-server.exe\n");
        return 2;
    }

    printf("[named pipe transport]\n");

    test_named_pipe_eval(server_path);
    test_named_pipe_persistence(server_path);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_run - tests_passed);

    return (tests_passed == tests_run) ? 0 : 1;
}

#else /* !__CYGWIN__ */

#include <stdio.h>
int main(void)
{
    printf("Named pipe tests skipped (not Cygwin)\n");
    return 0;
}

#endif /* __CYGWIN__ */
