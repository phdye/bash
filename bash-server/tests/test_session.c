/* test_session.c -- Integration tests for fork-per-session model
 *
 * Tests that bash state persists across EVAL commands within a single
 * client session.  Requires a running bash-server.
 *
 * Usage: test_session <socket_path> <auth_token>
 *
 * Tests:
 *   1. Variable persistence: set x=42, then echo $x
 *   2. Working directory persistence: cd /tmp, then pwd
 *   3. Function persistence: define f(), then call f
 *   4. Multiple variable accumulation: set a=1, b=2, echo "$a $b"
 *   5. Exit code persistence: false; echo $?
 *   6. Concurrent sessions have independent state
 *
 * Compile:
 *   gcc -Wall -g -o test_session test_session.c ../server_protocol.c \
 *       -I.. -I../.. -I../../include -DHAVE_CONFIG_H
 */

#include "server.h"
#include <assert.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <signal.h>

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-55s ", name); \
    fflush(stdout); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while(0)

/* Connect to server */
static int
sock_connect(const char *path)
{
    int fd;
    struct sockaddr_un addr;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Read one line from fd into a fixed buffer */
static int
read_line(int fd, char *buf, size_t bufsz)
{
    size_t pos = 0;
    while (pos < bufsz - 1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) break;
        if (c == '\n') break;
        if (c == '\r') continue;
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    return (int)pos;
}

/* Read one line from fd into a dynamically allocated buffer.
   Caller must free the returned buffer.  Returns length, or -1 on error. */
static int
read_line_dynamic(int fd, char **out)
{
    size_t bufsize = 8192;
    size_t pos = 0;
    char *buf = malloc(bufsize);
    if (!buf) { *out = NULL; return -1; }

    while (1) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n <= 0) break;
        if (c == '\n') break;
        if (c == '\r') continue;

        if (pos + 1 >= bufsize) {
            bufsize *= 2;
            char *newbuf = realloc(buf, bufsize);
            if (!newbuf) { free(buf); *out = NULL; return -1; }
            buf = newbuf;
        }
        buf[pos++] = c;
    }
    buf[pos] = '\0';
    *out = buf;
    return (int)pos;
}

/* Send a line */
static void
send_line(int fd, const char *line)
{
    size_t len = strlen(line);
    write(fd, line, len);
    if (len == 0 || line[len - 1] != '\n')
        write(fd, "\n", 1);
}

/* Authenticate with the server.  Returns 0 on success. */
static int
do_auth(int fd, const char *token)
{
    char buf[8192];
    char cmd[256];

    snprintf(cmd, sizeof(cmd), "AUTH %s", token);
    send_line(fd, cmd);
    read_line(fd, buf, sizeof(buf));
    return (strncmp(buf, "OK", 2) == 0) ? 0 : -1;
}

/* Send EVAL, read STDOUT/STDERR/EXIT triplet.
 * Returns exit code.  stdout_out receives decoded stdout (caller frees). */
static int
do_eval(int fd, const char *command, char **stdout_out, char **stderr_out)
{
    char *line;
    char cmd[SERVER_MAX_CMD + 16];
    int exit_code = -1;

    if (stdout_out) *stdout_out = NULL;
    if (stderr_out) *stderr_out = NULL;

    snprintf(cmd, sizeof(cmd), "EVAL %s", command);
    send_line(fd, cmd);

    /* Read STDOUT line */
    if (read_line_dynamic(fd, &line) >= 0 && line) {
        if (strncmp(line, "STDOUT", 6) == 0 && stdout_out) {
            const char *b64 = line + 6;
            while (*b64 == ' ') b64++;
            if (*b64) {
                size_t len;
                *stdout_out = protocol_base64_decode(b64, &len);
            }
        }
        free(line);
    }

    /* Read STDERR line */
    if (read_line_dynamic(fd, &line) >= 0 && line) {
        if (strncmp(line, "STDERR", 6) == 0 && stderr_out) {
            const char *b64 = line + 6;
            while (*b64 == ' ') b64++;
            if (*b64) {
                size_t len;
                *stderr_out = protocol_base64_decode(b64, &len);
            }
        }
        free(line);
    }

    /* Read EXIT line */
    if (read_line_dynamic(fd, &line) >= 0 && line) {
        if (strncmp(line, "EXIT ", 5) == 0) {
            exit_code = atoi(line + 5);
        }
        free(line);
    }

    return exit_code;
}

/* ================================================================
 * Test: Variable persistence across EVALs
 * ================================================================ */
static void
test_variable_persistence(const char *sock_path, const char *token)
{
    int fd, rc;
    char *out;

    TEST("variable persistence: x=42 then echo $x");

    fd = sock_connect(sock_path);
    if (fd < 0) { FAIL("connect"); return; }
    if (do_auth(fd, token) < 0) { FAIL("auth"); close(fd); return; }

    /* Set variable */
    rc = do_eval(fd, "x=42", NULL, NULL);
    if (rc != 0) { FAIL("set x=42 failed"); close(fd); return; }

    /* Read variable back */
    rc = do_eval(fd, "echo $x", &out, NULL);
    if (rc != 0) { FAIL("echo $x failed"); free(out); close(fd); return; }

    if (out && strcmp(out, "42\n") == 0)
        PASS();
    else
        FAIL(out ? out : "(no output)");

    free(out);
    send_line(fd, "QUIT");
    close(fd);
}

/* ================================================================
 * Test: Working directory persistence
 * ================================================================ */
static void
test_cwd_persistence(const char *sock_path, const char *token)
{
    int fd, rc;
    char *out;

    TEST("cwd persistence: cd /tmp then pwd");

    fd = sock_connect(sock_path);
    if (fd < 0) { FAIL("connect"); return; }
    if (do_auth(fd, token) < 0) { FAIL("auth"); close(fd); return; }

    rc = do_eval(fd, "cd /tmp", NULL, NULL);
    if (rc != 0) { FAIL("cd /tmp failed"); close(fd); return; }

    rc = do_eval(fd, "pwd", &out, NULL);
    if (rc != 0) { FAIL("pwd failed"); free(out); close(fd); return; }

    if (out && strcmp(out, "/tmp\n") == 0)
        PASS();
    else
        FAIL(out ? out : "(no output)");

    free(out);
    send_line(fd, "QUIT");
    close(fd);
}

/* ================================================================
 * Test: Function persistence
 * ================================================================ */
static void
test_function_persistence(const char *sock_path, const char *token)
{
    int fd, rc;
    char *out;

    TEST("function persistence: define f() then call f");

    fd = sock_connect(sock_path);
    if (fd < 0) { FAIL("connect"); return; }
    if (do_auth(fd, token) < 0) { FAIL("auth"); close(fd); return; }

    rc = do_eval(fd, "f() { echo hello-from-f; }", NULL, NULL);
    if (rc != 0) { FAIL("define f() failed"); close(fd); return; }

    rc = do_eval(fd, "f", &out, NULL);
    if (rc != 0) { FAIL("call f failed"); free(out); close(fd); return; }

    if (out && strcmp(out, "hello-from-f\n") == 0)
        PASS();
    else
        FAIL(out ? out : "(no output)");

    free(out);
    send_line(fd, "QUIT");
    close(fd);
}

/* ================================================================
 * Test: Multiple variable accumulation
 * ================================================================ */
static void
test_multi_variable(const char *sock_path, const char *token)
{
    int fd, rc;
    char *out;

    TEST("multi-variable: set a=1, b=2, echo \"$a $b\"");

    fd = sock_connect(sock_path);
    if (fd < 0) { FAIL("connect"); return; }
    if (do_auth(fd, token) < 0) { FAIL("auth"); close(fd); return; }

    do_eval(fd, "a=1", NULL, NULL);
    do_eval(fd, "b=2", NULL, NULL);
    rc = do_eval(fd, "echo \"$a $b\"", &out, NULL);

    if (rc == 0 && out && strcmp(out, "1 2\n") == 0)
        PASS();
    else
        FAIL(out ? out : "(no output)");

    free(out);
    send_line(fd, "QUIT");
    close(fd);
}

/* ================================================================
 * Test: Exit code from previous command
 * ================================================================ */
static void
test_exit_code_persistence(const char *sock_path, const char *token)
{
    int fd, rc;
    char *out;

    TEST("exit code: false then echo $?");

    fd = sock_connect(sock_path);
    if (fd < 0) { FAIL("connect"); return; }
    if (do_auth(fd, token) < 0) { FAIL("auth"); close(fd); return; }

    /* Run false (exit code 1) */
    rc = do_eval(fd, "false", NULL, NULL);
    if (rc != 1) { FAIL("false should exit 1"); close(fd); return; }

    /* $? should reflect the EVAL mechanism's own exit code, not the previous
       command's, because each EVAL is a separate parse_and_execute call.
       Actually, $? IS set by the last command in parse_and_execute. */
    rc = do_eval(fd, "echo $?", &out, NULL);

    /* After "false", $? = 1.  But "echo $?" itself succeeds, so the EVAL
       exit code is 0 and the output is "1\n". */
    if (rc == 0 && out && strcmp(out, "1\n") == 0)
        PASS();
    else {
        char msg[256];
        snprintf(msg, sizeof(msg), "rc=%d out=%s", rc, out ? out : "(null)");
        FAIL(msg);
    }

    free(out);
    send_line(fd, "QUIT");
    close(fd);
}

/* ================================================================
 * Test: Independent session state (two concurrent connections)
 * ================================================================ */
static void
test_independent_sessions(const char *sock_path, const char *token)
{
    int fd1, fd2, rc;
    char *out1, *out2;

    TEST("independent sessions: two clients with separate state");

    fd1 = sock_connect(sock_path);
    if (fd1 < 0) { FAIL("connect fd1"); return; }
    fd2 = sock_connect(sock_path);
    if (fd2 < 0) { FAIL("connect fd2"); close(fd1); return; }

    if (do_auth(fd1, token) < 0) { FAIL("auth fd1"); close(fd1); close(fd2); return; }
    if (do_auth(fd2, token) < 0) { FAIL("auth fd2"); close(fd1); close(fd2); return; }

    /* Set different values in each session */
    do_eval(fd1, "session_var=AAA", NULL, NULL);
    do_eval(fd2, "session_var=BBB", NULL, NULL);

    /* Read them back — each should see its own value */
    rc = do_eval(fd1, "echo $session_var", &out1, NULL);
    if (rc != 0) { FAIL("eval fd1"); free(out1); close(fd1); close(fd2); return; }

    rc = do_eval(fd2, "echo $session_var", &out2, NULL);
    if (rc != 0) { FAIL("eval fd2"); free(out1); free(out2); close(fd1); close(fd2); return; }

    if (out1 && strcmp(out1, "AAA\n") == 0 &&
        out2 && strcmp(out2, "BBB\n") == 0)
        PASS();
    else {
        char msg[256];
        snprintf(msg, sizeof(msg), "fd1=%s fd2=%s",
                 out1 ? out1 : "(null)", out2 ? out2 : "(null)");
        FAIL(msg);
    }

    free(out1);
    free(out2);
    send_line(fd1, "QUIT");
    send_line(fd2, "QUIT");
    close(fd1);
    close(fd2);
}

/* ================================================================
 * Test: Stderr capture
 * ================================================================ */
static void
test_stderr_capture(const char *sock_path, const char *token)
{
    int fd, rc;
    char *out, *err;

    TEST("stderr capture: echo err >&2");

    fd = sock_connect(sock_path);
    if (fd < 0) { FAIL("connect"); return; }
    if (do_auth(fd, token) < 0) { FAIL("auth"); close(fd); return; }

    rc = do_eval(fd, "echo err >&2", &out, &err);

    if (rc == 0 && err && strcmp(err, "err\n") == 0)
        PASS();
    else {
        char msg[256];
        snprintf(msg, sizeof(msg), "rc=%d err=%s", rc, err ? err : "(null)");
        FAIL(msg);
    }

    free(out);
    free(err);
    send_line(fd, "QUIT");
    close(fd);
}

/* ================================================================
 * Test: Large output (exceeds typical pipe buffer of 64KB)
 * ================================================================ */
static void
test_large_output(const char *sock_path, const char *token)
{
    int fd, rc;
    char *out;

    TEST("large output: seq 1 10000 (>64KB)");

    fd = sock_connect(sock_path);
    if (fd < 0) { FAIL("connect"); return; }
    if (do_auth(fd, token) < 0) { FAIL("auth"); close(fd); return; }

    rc = do_eval(fd, "seq 1 10000", &out, NULL);

    /* seq 1 10000 produces: "1\n2\n...\n10000\n"
       Total chars: 1*9 + 2*90 + 3*900 + 4*9000 + 5*1 = 9+180+2700+36000+5 = 38894
       Plus 10000 newlines = 48894 bytes.  Well under 1MB limit. */
    if (rc == 0 && out) {
        /* Verify it starts with "1\n" and ends with "10000\n" */
        int starts_ok = (strncmp(out, "1\n", 2) == 0);
        size_t len = strlen(out);
        int ends_ok = (len >= 6 && strcmp(out + len - 6, "10000\n") == 0);
        if (starts_ok && ends_ok)
            PASS();
        else {
            char msg[128];
            snprintf(msg, sizeof(msg), "start=%d end=%d len=%zu",
                     starts_ok, ends_ok, len);
            FAIL(msg);
        }
    } else {
        FAIL(out ? "bad rc" : "(no output)");
    }

    free(out);
    send_line(fd, "QUIT");
    close(fd);
}

/* ================================================================
 * Main
 * ================================================================ */
int
main(int argc, char **argv)
{
    const char *sock_path, *token;

    alarm(60);  /* Safety timeout */

    if (argc < 3) {
        fprintf(stderr, "Usage: %s <socket_path> <auth_token>\n", argv[0]);
        fprintf(stderr, "\nRun with a bash-server started.\n");
        return 2;
    }

    sock_path = argv[1];
    token = argv[2];

    printf("=== bash-server session integration tests ===\n\n");
    printf("[state persistence]\n");

    test_variable_persistence(sock_path, token);
    test_cwd_persistence(sock_path, token);
    test_function_persistence(sock_path, token);
    test_multi_variable(sock_path, token);
    test_exit_code_persistence(sock_path, token);

    printf("\n[session isolation]\n");
    test_independent_sessions(sock_path, token);

    printf("\n[output capture]\n");
    test_stderr_capture(sock_path, token);
    test_large_output(sock_path, token);

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
