/* test_v2_dispatch.c -- Unit tests for v2 JSON protocol message dispatch
 *
 * Tests json_session_handle() and all 6 channel handlers:
 *   ch 0: CONTROL (auth, ping, disconnect, configure)
 *   ch 1: COMMAND (eval)
 *   ch 2: STATE   (get/set/unset/inspect)
 *   ch 3: OBSERVE (subscribe/unsubscribe)
 *   ch 4: DEBUG   (message handling)
 *   ch 5: PTY     (spawn)
 *
 * Uses socketpair + fork: child runs json_session_handle, parent sends
 * frames and reads responses.  All bash internals are stubbed out.
 *
 * Compile:
 *   gcc -g -O2 -Wall -DHAVE_CONFIG_H -I.. -I../.. -I../../include -I../../lib \
 *       -o test_v2_dispatch test_v2_dispatch.c ../server_json.c ../server_protocol.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <fcntl.h>
#include <errno.h>

#include "../server.h"

/* ================================================================
 * Test auth token
 * ================================================================ */
#define TEST_TOKEN "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

/* ================================================================
 * Stubs for bash internals
 * ================================================================ */

int last_command_exit_value = 0;

/* session_init / session_cleanup (from server_session.c) */
int session_init(client_session_t *session, int client_fd)
{
    memset(session, 0, sizeof(*session));
    session->fd = client_fd;
    session->write_fd = -1;
    session->authenticated = 0;
    session->protocol_version = PROTOCOL_V1;
    session->wire_format = WIRE_BINARY;
    session->pid = getpid();
    session->stdout_pipe[0] = -1;
    session->stdout_pipe[1] = -1;
    session->stderr_pipe[0] = -1;
    session->stderr_pipe[1] = -1;
    return 0;
}

void session_cleanup(client_session_t *session)
{
    if (session->write_fd >= 0 && session->write_fd != session->fd)
        close(session->write_fd);
    if (session->fd >= 0)
        close(session->fd);
}

/* bash initialization (no-op in tests) */
void init_bash_for_session(server_config_t *config) { (void)config; }

/* parse_and_execute: writes to redirected stdout for output capture */
int parse_and_execute(char *s, const char *f, int fl)
{
    (void)s; (void)f; (void)fl;
    /* Write to stdout which v2_handle_command has redirected to a temp file */
    printf("stub output\n");
    last_command_exit_value = 0;
    return 0;
}

/* State handler stubs — write v1 protocol responses to wfd */
int state_handle_get_var(int wfd, const char *name)
{
    char *b64 = protocol_base64_encode("hello", 5);
    protocol_write_line(wfd, "VALUE %s %s exported", name, b64);
    free(b64);
    return 0;
}

int state_handle_set_var(int wfd, const char *arg)
{
    (void)arg;
    protocol_write_line(wfd, "OK set");
    return 0;
}

int state_handle_unset_var(int wfd, const char *arg)
{
    (void)arg;
    protocol_write_line(wfd, "OK unset");
    return 0;
}

int state_handle_get_func(int wfd, const char *name)
{
    char *b64 = protocol_base64_encode("() { echo hi; }", 16);
    protocol_write_line(wfd, "FUNC %s %s", name, b64);
    free(b64);
    return 0;
}

int state_handle_unset_func(int wfd, const char *arg)
{
    (void)arg;
    protocol_write_line(wfd, "OK unset");
    return 0;
}

int state_handle_get_alias(int wfd, const char *name)
{
    char *b64 = protocol_base64_encode("ls -la", 6);
    protocol_write_line(wfd, "ALIAS %s %s", name, b64);
    free(b64);
    return 0;
}

int state_handle_set_alias(int wfd, const char *arg)
{
    (void)arg;
    protocol_write_line(wfd, "OK set");
    return 0;
}

int state_handle_unset_alias(int wfd, const char *arg)
{
    (void)arg;
    protocol_write_line(wfd, "OK unset");
    return 0;
}

int state_handle_set_trap(int wfd, const char *arg)
{
    (void)arg;
    protocol_write_line(wfd, "OK set");
    return 0;
}

int state_handle_unset_trap(int wfd, const char *arg)
{
    (void)arg;
    protocol_write_line(wfd, "OK unset");
    return 0;
}

int state_handle_inspect(int wfd, const char *arg)
{
    (void)arg;
    char *b64 = protocol_base64_encode("42", 2);
    protocol_write_line(wfd, "VALUE x %s exported", b64);
    free(b64);
    protocol_write_line(wfd, "INSPECT-END");
    return 0;
}

/* Observe stubs */
static int g_observe_level = 0;

void observe_init(int fd, int level)
{
    (void)fd;
    g_observe_level = level;
}

void observe_cleanup(void) { g_observe_level = 0; }

int observe_set_level(int level)
{
    if (level < 0) level = 0;
    if (level > OBSERVE_LEVEL_MAX) level = OBSERVE_LEVEL_MAX;
    g_observe_level = level;
    return level;
}

/* PTY stub */
int pty_handle_spawn(int rfd, int wfd, const char *payload)
{
    (void)rfd; (void)payload;
    json_frame_write_fmt(wfd, CHAN_PTY,
        "{\"type\":\"spawned\",\"pid\":12345}");
    return 0;
}

/* Debug stubs */
void debug_init(int rfd, int wfd) { (void)rfd; (void)wfd; }
void debug_cleanup(void) { }

int debug_handle_message(int rfd, int wfd, const char *payload)
{
    (void)rfd; (void)payload;
    json_frame_write_fmt(wfd, CHAN_DEBUG,
        "{\"type\":\"status\",\"active\":false}");
    return 0;
}

/* ================================================================
 * Test infrastructure
 * ================================================================ */

static int tests_run = 0;
static int tests_passed = 0;

#define ASSERT(cond, msg) do { \
    tests_run++; \
    if (!(cond)) { \
        printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
    } else { \
        tests_passed++; \
        printf("  PASS: %s\n", msg); \
    } \
} while (0)

/* Test session context */
typedef struct {
    int fd;        /* parent's end of socketpair */
    pid_t child;   /* child pid running json_session_handle */
} v2_ctx_t;

/* Set up: create socketpair, fork.  Child runs json_session_handle.
 * Parent gets the other end for sending/receiving frames.
 * Returns 0 on success, -1 on failure. */
static int
v2_setup(v2_ctx_t *ctx, server_config_t *config)
{
    int sv[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
        return -1;

    pid_t pid = fork();
    if (pid < 0) {
        close(sv[0]); close(sv[1]);
        return -1;
    }

    if (pid == 0) {
        /* Child: run json_session_handle */
        close(sv[0]);
        /* Suppress debug logging from json_session_handle */
        int devnull = open("/dev/null", 1);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }

        client_session_t session;
        session_init(&session, sv[1]);
        json_session_handle(&session, config);
        session_cleanup(&session);
        _exit(0);
    }

    /* Parent */
    close(sv[1]);
    ctx->fd = sv[0];
    ctx->child = pid;
    return 0;
}

/* Teardown: close fd, wait for child */
static void
v2_teardown(v2_ctx_t *ctx)
{
    close(ctx->fd);
    ctx->fd = -1;
    int status;
    /* Poll-based wait with timeout (5 seconds) */
    for (int i = 0; i < 50; i++) {
        if (waitpid(ctx->child, &status, WNOHANG) == ctx->child)
            return;
        usleep(100000);
    }
    /* Force kill if still alive */
    kill(ctx->child, SIGKILL);
    waitpid(ctx->child, &status, 0);
}

/* Read a frame with timeout (2 seconds).
 * Returns 0 on success, -1 on timeout/error. */
static int
v2_recv(int fd, int *channel, int *flags, char **payload, size_t *payload_len)
{
    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = 2;
    tv.tv_usec = 0;

    int ready = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (ready <= 0)
        return -1;  /* timeout or error */

    return json_frame_read(fd, channel, flags, payload, payload_len);
}

/* Authenticate a session.  Returns 0 on success, -1 on failure. */
static int
v2_auth(v2_ctx_t *ctx)
{
    char msg[256];
    snprintf(msg, sizeof(msg),
        "{\"type\":\"auth\",\"token\":\"%s\"}", TEST_TOKEN);

    if (json_frame_write(ctx->fd, CHAN_CONTROL, 0, msg, strlen(msg)) < 0)
        return -1;

    int ch, fl;
    char *payload;
    size_t plen;
    if (v2_recv(ctx->fd, &ch, &fl, &payload, &plen) < 0)
        return -1;

    int ok = (ch == CHAN_CONTROL && strstr(payload, "auth_ok") != NULL);
    free(payload);
    return ok ? 0 : -1;
}

/* Default config for tests */
static server_config_t
test_config(void)
{
    server_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.auth_token = TEST_TOKEN;
    cfg.max_clients = 10;
    cfg.auth_fd = -1;
    return cfg;
}

/* ================================================================
 * CONTROL CHANNEL TESTS (ch 0)
 * ================================================================ */

static void test_v2_auth_wrong_token(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_auth_wrong_token:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    char *msg = "{\"type\":\"auth\",\"token\":\"wrong_token\"}";
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv response");
    ASSERT(ch == CHAN_CONTROL, "response on control channel");
    ASSERT(strstr(payload, "\"type\":\"error\"") != NULL, "error type");
    ASSERT(strstr(payload, "invalid token") != NULL, "invalid token message");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_auth_ok(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_auth_ok:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    char msg[256];
    snprintf(msg, sizeof(msg),
        "{\"type\":\"auth\",\"token\":\"%s\"}", TEST_TOKEN);
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv auth response");
    ASSERT(ch == CHAN_CONTROL, "response on control channel");
    ASSERT(strstr(payload, "\"type\":\"auth_ok\"") != NULL, "auth_ok response");
    ASSERT(strstr(payload, "\"capabilities\"") != NULL, "capabilities present");
    free(payload);

    /* Send disconnect to clean up */
    msg[0] = '\0';
    snprintf(msg, sizeof(msg), "{\"type\":\"disconnect\"}");
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg, strlen(msg));
    if (v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0) free(payload);

    v2_teardown(&ctx);
}

static void test_v2_auth_missing_token(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_auth_missing_token:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    char *msg = "{\"type\":\"auth\"}";
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv response");
    ASSERT(strstr(payload, "\"type\":\"error\"") != NULL, "error type");
    ASSERT(strstr(payload, "token required") != NULL, "token required message");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_ping(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_ping:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    /* Ping doesn't require auth */
    char *msg = "{\"type\":\"ping\"}";
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv pong");
    ASSERT(ch == CHAN_CONTROL, "pong on control channel");
    ASSERT(strstr(payload, "\"type\":\"pong\"") != NULL, "pong response");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_disconnect(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_disconnect:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    char *msg = "{\"type\":\"disconnect\"}";
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv disconnect_ok");
    ASSERT(ch == CHAN_CONTROL, "response on control channel");
    ASSERT(strstr(payload, "\"type\":\"disconnect_ok\"") != NULL, "disconnect_ok");
    free(payload);

    /* Child should have exited — next read should fail */
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == -1,
        "no more frames after disconnect");

    v2_teardown(&ctx);
}

static void test_v2_configure(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_configure:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    char *msg = "{\"type\":\"configure\",\"observability\":1}";
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv configured");
    ASSERT(strstr(payload, "\"type\":\"configured\"") != NULL, "configured response");
    ASSERT(strstr(payload, "\"observability\":1") != NULL, "observability level 1");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_control_missing_type(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_control_missing_type:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    char *msg = "{\"foo\":\"bar\"}";
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(strstr(payload, "\"type\":\"error\"") != NULL, "error type");
    ASSERT(strstr(payload, "missing type") != NULL, "missing type message");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_control_unknown_type(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_control_unknown_type:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    char *msg = "{\"type\":\"foobar\"}";
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(strstr(payload, "\"type\":\"error\"") != NULL, "error type");
    ASSERT(strstr(payload, "unknown control") != NULL, "unknown type message");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * COMMAND CHANNEL TESTS (ch 1)
 * ================================================================ */

static void test_v2_command_not_authed(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_command_not_authed:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    char *msg = "{\"type\":\"eval\",\"command\":\"echo hi\"}";
    json_frame_write(ctx.fd, CHAN_COMMAND, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(ch == CHAN_COMMAND, "error on command channel");
    ASSERT(strstr(payload, "not authenticated") != NULL, "not authenticated");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_command_eval(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_command_eval:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"eval\",\"command\":\"echo hi\"}";
    json_frame_write(ctx.fd, CHAN_COMMAND, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;

    /* Expect stdout frame */
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv stdout");
    ASSERT(ch == CHAN_COMMAND, "stdout on command channel");
    ASSERT(strstr(payload, "\"type\":\"stdout\"") != NULL, "stdout type");
    ASSERT(strstr(payload, "\"encoding\":\"base64\"") != NULL, "base64 encoding");
    free(payload);

    /* Expect stderr frame */
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv stderr");
    ASSERT(ch == CHAN_COMMAND, "stderr on command channel");
    ASSERT(strstr(payload, "\"type\":\"stderr\"") != NULL, "stderr type");
    free(payload);

    /* Expect complete frame */
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv complete");
    ASSERT(ch == CHAN_COMMAND, "complete on command channel");
    ASSERT(strstr(payload, "\"type\":\"complete\"") != NULL, "complete type");
    ASSERT(strstr(payload, "\"exit_code\":0") != NULL, "exit code 0");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_command_eval_with_id(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_command_eval_with_id:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"eval\",\"command\":\"echo hi\",\"id\":\"req-42\"}";
    json_frame_write(ctx.fd, CHAN_COMMAND, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;

    /* All 3 response frames should include the id */
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv stdout");
    ASSERT(strstr(payload, "\"id\":\"req-42\"") != NULL, "stdout has id");
    free(payload);

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv stderr");
    ASSERT(strstr(payload, "\"id\":\"req-42\"") != NULL, "stderr has id");
    free(payload);

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv complete");
    ASSERT(strstr(payload, "\"id\":\"req-42\"") != NULL, "complete has id");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_command_missing_command(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_command_missing_command:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"eval\"}";
    json_frame_write(ctx.fd, CHAN_COMMAND, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(strstr(payload, "command required") != NULL, "command required");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_command_unknown_type(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_command_unknown_type:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"foobar\"}";
    json_frame_write(ctx.fd, CHAN_COMMAND, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(strstr(payload, "unknown command type") != NULL, "unknown type");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * STATE CHANNEL TESTS (ch 2)
 * ================================================================ */

static void test_v2_state_not_authed(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_state_not_authed:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    char *msg = "{\"type\":\"get\",\"target\":\"var\",\"name\":\"PATH\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(ch == CHAN_STATE, "error on state channel");
    ASSERT(strstr(payload, "not authenticated") != NULL, "not authenticated");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_state_get_var(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_state_get_var:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"get\",\"target\":\"var\",\"name\":\"FOO\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv value");
    ASSERT(ch == CHAN_STATE, "value on state channel");
    ASSERT(strstr(payload, "\"type\":\"value\"") != NULL, "value type");
    ASSERT(strstr(payload, "\"target\":\"var\"") != NULL, "target is var");
    ASSERT(strstr(payload, "\"name\":\"FOO\"") != NULL, "name is FOO");
    ASSERT(strstr(payload, "\"value\":\"hello\"") != NULL, "value is hello");
    ASSERT(strstr(payload, "\"attributes\"") != NULL, "has attributes");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_state_set_var(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_state_set_var:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"set\",\"target\":\"var\",\"name\":\"FOO\",\"value\":\"bar\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv set_ok");
    ASSERT(ch == CHAN_STATE, "response on state channel");
    ASSERT(strstr(payload, "\"type\":\"set_ok\"") != NULL, "set_ok type");
    ASSERT(strstr(payload, "\"target\":\"var\"") != NULL, "target is var");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_state_unset_var(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_state_unset_var:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"unset\",\"target\":\"var\",\"name\":\"FOO\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv unset_ok");
    ASSERT(ch == CHAN_STATE, "response on state channel");
    ASSERT(strstr(payload, "\"type\":\"unset_ok\"") != NULL, "unset_ok type");
    ASSERT(strstr(payload, "\"target\":\"var\"") != NULL, "target is var");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_state_get_func(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_state_get_func:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"get\",\"target\":\"function\",\"name\":\"myfunc\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv value");
    ASSERT(strstr(payload, "\"type\":\"value\"") != NULL, "value type");
    ASSERT(strstr(payload, "\"target\":\"function\"") != NULL, "target is function");
    ASSERT(strstr(payload, "\"name\":\"myfunc\"") != NULL, "name is myfunc");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_state_get_alias(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_state_get_alias:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"get\",\"target\":\"alias\",\"name\":\"ll\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv value");
    ASSERT(strstr(payload, "\"type\":\"value\"") != NULL, "value type");
    ASSERT(strstr(payload, "\"target\":\"alias\"") != NULL, "target is alias");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_state_inspect(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_state_inspect:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"inspect\",\"query\":\"variables\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv inspect_result");
    ASSERT(strstr(payload, "\"type\":\"inspect_result\"") != NULL, "inspect_result type");
    ASSERT(strstr(payload, "\"query\":\"variables\"") != NULL, "query field present");
    ASSERT(strstr(payload, "\"data\":[") != NULL, "data is array");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_state_unknown_target(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_state_unknown_target:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"get\",\"target\":\"foobar\",\"name\":\"x\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(strstr(payload, "unknown target") != NULL, "unknown target error");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_state_unknown_type(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_state_unknown_type:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"foobar\",\"target\":\"var\",\"name\":\"x\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(strstr(payload, "unknown state message type") != NULL,
        "unknown state type error");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_state_missing_fields(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_state_missing_fields:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    int ch, fl;
    char *payload;
    size_t plen;

    /* Missing type */
    char *msg1 = "{\"target\":\"var\",\"name\":\"x\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg1, strlen(msg1));
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error 1");
    ASSERT(strstr(payload, "missing type") != NULL, "missing type error");
    free(payload);

    /* Missing target for get */
    char *msg2 = "{\"type\":\"get\",\"name\":\"x\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg2, strlen(msg2));
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error 2");
    ASSERT(strstr(payload, "missing target") != NULL, "missing target error");
    free(payload);

    /* Missing name for get */
    char *msg3 = "{\"type\":\"get\",\"target\":\"var\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg3, strlen(msg3));
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error 3");
    ASSERT(strstr(payload, "missing name") != NULL, "missing name error");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * OBSERVE CHANNEL TESTS (ch 3)
 * ================================================================ */

static void test_v2_observe_not_authed(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_observe_not_authed:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    char *msg = "{\"type\":\"subscribe\",\"level\":1}";
    json_frame_write(ctx.fd, CHAN_OBSERVE, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(ch == CHAN_OBSERVE, "error on observe channel");
    ASSERT(strstr(payload, "not authenticated") != NULL, "not authenticated");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_observe_subscribe(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_observe_subscribe:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"subscribe\",\"level\":1}";
    json_frame_write(ctx.fd, CHAN_OBSERVE, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv subscribed");
    ASSERT(ch == CHAN_OBSERVE, "response on observe channel");
    ASSERT(strstr(payload, "\"type\":\"subscribed\"") != NULL, "subscribed type");
    ASSERT(strstr(payload, "\"level\":1") != NULL, "level is 1");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_observe_unsubscribe(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_observe_unsubscribe:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    /* Subscribe first */
    char *msg1 = "{\"type\":\"subscribe\",\"level\":1}";
    json_frame_write(ctx.fd, CHAN_OBSERVE, 0, msg1, strlen(msg1));
    int ch, fl;
    char *payload;
    size_t plen;
    v2_recv(ctx.fd, &ch, &fl, &payload, &plen);
    free(payload);

    /* Then unsubscribe */
    char *msg2 = "{\"type\":\"unsubscribe\"}";
    json_frame_write(ctx.fd, CHAN_OBSERVE, 0, msg2, strlen(msg2));
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv unsubscribed");
    ASSERT(strstr(payload, "\"type\":\"unsubscribed\"") != NULL, "unsubscribed type");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * PTY CHANNEL TESTS (ch 5)
 * ================================================================ */

static void test_v2_pty_not_authed(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_pty_not_authed:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    char *msg = "{\"type\":\"spawn\"}";
    json_frame_write(ctx.fd, CHAN_PTY, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(ch == CHAN_PTY, "error on pty channel");
    ASSERT(strstr(payload, "not authenticated") != NULL, "not authenticated");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_pty_spawn(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_pty_spawn:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"spawn\",\"command\":\"bash\"}";
    json_frame_write(ctx.fd, CHAN_PTY, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv spawned");
    ASSERT(ch == CHAN_PTY, "response on pty channel");
    ASSERT(strstr(payload, "\"type\":\"spawned\"") != NULL, "spawned type");
    ASSERT(strstr(payload, "\"pid\":12345") != NULL, "stub pid present");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * DEBUG CHANNEL TESTS (ch 4)
 * ================================================================ */

static void test_v2_debug_message(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_debug_message:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"status\"}";
    json_frame_write(ctx.fd, CHAN_DEBUG, 0, msg, strlen(msg));

    int ch, fl;
    char *payload;
    size_t plen;
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv debug response");
    ASSERT(ch == CHAN_DEBUG, "response on debug channel");
    ASSERT(strstr(payload, "\"type\":\"status\"") != NULL, "status type");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * NDJSON MODE TESTS
 * ================================================================ */

static void test_v2_ndjson_auth_flow(void)
{
    int sv[2];
    server_config_t cfg = test_config();

    printf("test_v2_ndjson_auth_flow:\n");

    /* Switch to NDJSON mode before fork */
    json_set_wire_format(WIRE_NDJSON);

    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    pid_t pid = fork();
    if (pid == 0) {
        close(sv[0]);
        int devnull = open("/dev/null", 1);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }

        client_session_t session;
        session_init(&session, sv[1]);
        session.wire_format = WIRE_NDJSON;
        json_session_handle(&session, &cfg);
        session_cleanup(&session);
        _exit(0);
    }
    close(sv[1]);

    int ch, fl;
    char *payload;
    size_t plen;

    /* Send auth via NDJSON */
    char msg[256];
    snprintf(msg, sizeof(msg),
        "{\"type\":\"auth\",\"token\":\"%s\"}", TEST_TOKEN);
    ASSERT(json_frame_write(sv[0], CHAN_CONTROL, 0, msg, strlen(msg)) == 0,
        "ndjson auth write");

    ASSERT(v2_recv(sv[0], &ch, &fl, &payload, &plen) == 0, "ndjson auth response");
    ASSERT(ch == CHAN_CONTROL, "response on control channel");
    ASSERT(strstr(payload, "auth_ok") != NULL, "auth_ok via ndjson");
    free(payload);

    /* Send ping via NDJSON */
    char *ping = "{\"type\":\"ping\"}";
    json_frame_write(sv[0], CHAN_CONTROL, 0, ping, strlen(ping));
    ASSERT(v2_recv(sv[0], &ch, &fl, &payload, &plen) == 0, "ndjson ping response");
    ASSERT(strstr(payload, "pong") != NULL, "pong via ndjson");
    free(payload);

    /* Disconnect */
    char *disc = "{\"type\":\"disconnect\"}";
    json_frame_write(sv[0], CHAN_CONTROL, 0, disc, strlen(disc));
    if (v2_recv(sv[0], &ch, &fl, &payload, &plen) == 0) free(payload);

    close(sv[0]);
    waitpid(pid, NULL, 0);

    /* Restore binary mode */
    json_set_wire_format(WIRE_BINARY);
}

/* ================================================================
 * MAIN
 * ================================================================ */

int
main(void)
{
    /* Unbuffered output */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Safety timeout */
    alarm(60);

    printf("=== v2 Protocol Dispatch Unit Tests ===\n\n");

    /* Control channel */
    test_v2_auth_wrong_token();
    test_v2_auth_ok();
    test_v2_auth_missing_token();
    test_v2_ping();
    test_v2_disconnect();
    test_v2_configure();
    test_v2_control_missing_type();
    test_v2_control_unknown_type();

    /* Command channel */
    test_v2_command_not_authed();
    test_v2_command_eval();
    test_v2_command_eval_with_id();
    test_v2_command_missing_command();
    test_v2_command_unknown_type();

    /* State channel */
    test_v2_state_not_authed();
    test_v2_state_get_var();
    test_v2_state_set_var();
    test_v2_state_unset_var();
    test_v2_state_get_func();
    test_v2_state_get_alias();
    test_v2_state_inspect();
    test_v2_state_unknown_target();
    test_v2_state_unknown_type();
    test_v2_state_missing_fields();

    /* Observe channel */
    test_v2_observe_not_authed();
    test_v2_observe_subscribe();
    test_v2_observe_unsubscribe();

    /* PTY channel */
    test_v2_pty_not_authed();
    test_v2_pty_spawn();

    /* Debug channel */
    test_v2_debug_message();

    /* NDJSON mode */
    test_v2_ndjson_auth_flow();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
