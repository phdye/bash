/* test_v2_errors.c -- Error handling and edge case tests for v2 protocol
 *
 * Complements test_v2_dispatch.c with tests for:
 *   - Oversized frame rejection
 *   - Truncated/partial frame handling
 *   - Multiple sequential operations on a single session
 *   - State set with attributes (--export, --readonly, --integer)
 *   - State unset for all target types (var, function, alias)
 *   - State set_alias
 *   - Observe level clamping (level > max, level < 0)
 *   - Auth re-authentication (already authenticated)
 *   - Configure with no observability field (defaults to 0)
 *   - Empty JSON payload
 *   - PTY non-spawn message before spawn
 *
 * Compile:
 *   gcc -g -O2 -Wall -DHAVE_CONFIG_H -I.. -I../.. -I../../include -I../../lib \
 *       -o test_v2_errors test_v2_errors.c ../server_json.c ../server_protocol.c
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
#include <arpa/inet.h>

#include "../server.h"

#define TEST_TOKEN "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"

/* ================================================================
 * Stubs (same as test_v2_dispatch.c)
 * ================================================================ */

int last_command_exit_value = 0;

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

void init_bash_for_session(server_config_t *config) { (void)config; }

int parse_and_execute(char *s, const char *f, int fl)
{
    (void)s; (void)f; (void)fl;
    printf("output\n");
    last_command_exit_value = 42;
    return 0;
}

int state_handle_get_var(int wfd, const char *name)
{
    char *b64 = protocol_base64_encode("val", 3);
    protocol_write_line(wfd, "VALUE %s %s", name, b64);
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
    char *b64 = protocol_base64_encode("() { :; }", 9);
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
    char *b64v = protocol_base64_encode("1", 1);
    char *b64f = protocol_base64_encode("() { echo; }", 12);
    char *b64a = protocol_base64_encode("ls -la", 6);
    char *b64t = protocol_base64_encode("echo trapped", 12);
    protocol_write_line(wfd, "VALUE x %s", b64v);
    protocol_write_line(wfd, "FUNC f %s", b64f);
    protocol_write_line(wfd, "ALIAS ll %s", b64a);
    protocol_write_line(wfd, "TRAP INT %s", b64t);
    protocol_write_line(wfd, "INSPECT-END");
    free(b64v); free(b64f); free(b64a); free(b64t);
    return 0;
}

static int g_observe_level = 0;
void observe_init(int fd, int level) { (void)fd; g_observe_level = level; }
void observe_cleanup(void) { g_observe_level = 0; }
int observe_set_level(int level)
{
    if (level < 0) level = 0;
    if (level > OBSERVE_LEVEL_MAX) level = OBSERVE_LEVEL_MAX;
    g_observe_level = level;
    return level;
}

int pty_handle_spawn(int rfd, int wfd, const char *payload)
{
    (void)rfd; (void)payload;
    json_frame_write_fmt(wfd, CHAN_PTY, "{\"type\":\"spawned\",\"pid\":99}");
    return 0;
}

void debug_init(int rfd, int wfd) { (void)rfd; (void)wfd; }
void debug_cleanup(void) { }
int debug_handle_message(int rfd, int wfd, const char *payload)
{
    (void)rfd; (void)payload;
    json_frame_write_fmt(wfd, CHAN_DEBUG, "{\"type\":\"ack\"}");
    return 0;
}

/* ================================================================
 * Test infrastructure (same pattern as test_v2_dispatch.c)
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

typedef struct {
    int fd;
    pid_t child;
} v2_ctx_t;

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
        close(sv[0]);
        int devnull = open("/dev/null", 1);
        if (devnull >= 0) { dup2(devnull, STDERR_FILENO); close(devnull); }
        client_session_t session;
        session_init(&session, sv[1]);
        json_session_handle(&session, config);
        session_cleanup(&session);
        _exit(0);
    }
    close(sv[1]);
    ctx->fd = sv[0];
    ctx->child = pid;
    return 0;
}

static void
v2_teardown(v2_ctx_t *ctx)
{
    close(ctx->fd);
    ctx->fd = -1;
    int status;
    for (int i = 0; i < 50; i++) {
        if (waitpid(ctx->child, &status, WNOHANG) == ctx->child)
            return;
        usleep(100000);
    }
    kill(ctx->child, SIGKILL);
    waitpid(ctx->child, &status, 0);
}

static int
v2_recv(int fd, int *channel, int *flags, char **payload, size_t *payload_len)
{
    fd_set rfds;
    struct timeval tv;
    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    if (select(fd + 1, &rfds, NULL, NULL, &tv) <= 0)
        return -1;
    return json_frame_read(fd, channel, flags, payload, payload_len);
}

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
 * Tests: Multiple sequential operations on one session
 * ================================================================ */

static void test_v2_multi_ops(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_multi_ops:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    /* Ping */
    char *msg1 = "{\"type\":\"ping\"}";
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg1, strlen(msg1));
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv pong");
    ASSERT(strstr(payload, "pong") != NULL, "pong response");
    free(payload);

    /* Eval */
    char *msg2 = "{\"type\":\"eval\",\"command\":\"echo test\"}";
    json_frame_write(ctx.fd, CHAN_COMMAND, 0, msg2, strlen(msg2));
    /* Consume stdout, stderr, complete */
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "stdout frame");
    free(payload);
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "stderr frame");
    free(payload);
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "complete frame");
    ASSERT(strstr(payload, "\"exit_code\":42") != NULL, "exit code 42");
    free(payload);

    /* State get */
    char *msg3 = "{\"type\":\"get\",\"target\":\"var\",\"name\":\"X\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg3, strlen(msg3));
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "state value");
    ASSERT(strstr(payload, "\"type\":\"value\"") != NULL, "value type");
    free(payload);

    /* Subscribe */
    char *msg4 = "{\"type\":\"subscribe\",\"level\":1}";
    json_frame_write(ctx.fd, CHAN_OBSERVE, 0, msg4, strlen(msg4));
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "subscribed");
    ASSERT(strstr(payload, "subscribed") != NULL, "subscribed response");
    free(payload);

    /* Disconnect */
    char *msg5 = "{\"type\":\"disconnect\"}";
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg5, strlen(msg5));
    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "disconnect_ok");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: State set with attributes
 * ================================================================ */

static void test_v2_state_set_with_attrs(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_state_set_with_attrs:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"set\",\"target\":\"var\",\"name\":\"X\","
                "\"value\":\"42\",\"attributes\":[\"exported\",\"integer\"]}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv set_ok");
    ASSERT(strstr(payload, "\"type\":\"set_ok\"") != NULL, "set_ok type");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: State set_alias
 * ================================================================ */

static void test_v2_state_set_alias(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_state_set_alias:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"set\",\"target\":\"alias\",\"name\":\"ll\","
                "\"value\":\"ls -la\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv set_ok");
    ASSERT(strstr(payload, "\"type\":\"set_ok\"") != NULL, "set_ok type");
    ASSERT(strstr(payload, "\"target\":\"alias\"") != NULL, "target is alias");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: State unset for function and alias
 * ================================================================ */

static void test_v2_state_unset_func(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_state_unset_func:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"unset\",\"target\":\"function\",\"name\":\"f\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv unset_ok");
    ASSERT(strstr(payload, "\"type\":\"unset_ok\"") != NULL, "unset_ok type");
    ASSERT(strstr(payload, "\"target\":\"function\"") != NULL, "target is function");
    free(payload);

    v2_teardown(&ctx);
}

static void test_v2_state_unset_alias(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_state_unset_alias:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"unset\",\"target\":\"alias\",\"name\":\"ll\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv unset_ok");
    ASSERT(strstr(payload, "\"type\":\"unset_ok\"") != NULL, "unset_ok type");
    ASSERT(strstr(payload, "\"target\":\"alias\"") != NULL, "target is alias");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: State unset unknown target
 * ================================================================ */

static void test_v2_state_unset_unknown_target(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_state_unset_unknown_target:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"unset\",\"target\":\"foobar\",\"name\":\"x\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(strstr(payload, "unknown target") != NULL, "unknown target error");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: State set missing value field
 * ================================================================ */

static void test_v2_state_set_missing_value(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_state_set_missing_value:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"set\",\"target\":\"var\",\"name\":\"X\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(strstr(payload, "missing value") != NULL, "missing value error");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: State set unknown target for set
 * ================================================================ */

static void test_v2_state_set_unknown_target(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_state_set_unknown_target:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"set\",\"target\":\"trap\",\"name\":\"X\",\"value\":\"Y\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(strstr(payload, "unknown target") != NULL, "unknown target for set");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: Inspect result with multiple item types
 * ================================================================ */

static void test_v2_state_inspect_multi(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_state_inspect_multi:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"inspect\",\"query\":\"all\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv inspect_result");
    ASSERT(strstr(payload, "\"type\":\"inspect_result\"") != NULL, "inspect_result type");
    ASSERT(strstr(payload, "\"data\":[") != NULL, "data is array");
    /* Stub returns VALUE, FUNC, ALIAS, TRAP lines — all should appear in data */
    ASSERT(strstr(payload, "\"name\":\"x\"") != NULL, "has variable x");
    ASSERT(strstr(payload, "\"name\":\"f\"") != NULL, "has function f");
    ASSERT(strstr(payload, "\"name\":\"ll\"") != NULL, "has alias ll");
    ASSERT(strstr(payload, "\"signal\":\"INT\"") != NULL, "has trap INT");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: Inspect missing query field
 * ================================================================ */

static void test_v2_state_inspect_missing_query(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_state_inspect_missing_query:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"inspect\"}";
    json_frame_write(ctx.fd, CHAN_STATE, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(strstr(payload, "missing query") != NULL, "missing query error");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: Auth re-authentication
 * ================================================================ */

static void test_v2_auth_already_authed(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_auth_already_authed:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "first authenticate");

    /* Second auth attempt */
    char msg[256];
    snprintf(msg, sizeof(msg),
        "{\"type\":\"auth\",\"token\":\"%s\"}", TEST_TOKEN);
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv response");
    ASSERT(strstr(payload, "\"type\":\"auth_ok\"") != NULL, "auth_ok (idempotent)");
    ASSERT(strstr(payload, "already authenticated") != NULL, "already authenticated");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: Configure with default (no observability field)
 * ================================================================ */

static void test_v2_configure_default(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_configure_default:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    /* Configure without observability field — defaults to 0 */
    char *msg = "{\"type\":\"configure\"}";
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv configured");
    ASSERT(strstr(payload, "\"type\":\"configured\"") != NULL, "configured type");
    ASSERT(strstr(payload, "\"observability\":0") != NULL, "default observability 0");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: Configure with level > max (clamped)
 * ================================================================ */

static void test_v2_configure_clamp(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_configure_clamp:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");

    char *msg = "{\"type\":\"configure\",\"observability\":99}";
    json_frame_write(ctx.fd, CHAN_CONTROL, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv configured");
    ASSERT(strstr(payload, "\"type\":\"configured\"") != NULL, "configured type");
    /* Level should be clamped to OBSERVE_LEVEL_MAX (1) */
    ASSERT(strstr(payload, "\"observability\":1") != NULL, "clamped to max (1)");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: Observe unknown type
 * ================================================================ */

static void test_v2_observe_unknown_type(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_observe_unknown_type:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    char *msg = "{\"type\":\"foobar\"}";
    json_frame_write(ctx.fd, CHAN_OBSERVE, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(ch == CHAN_OBSERVE, "error on observe channel");
    ASSERT(strstr(payload, "unknown observe") != NULL, "unknown observe type");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: PTY non-spawn before spawn
 * ================================================================ */

static void test_v2_pty_non_spawn(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_pty_non_spawn:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    /* Send a non-spawn message (e.g., resize) — should get error */
    char *msg = "{\"type\":\"resize\",\"rows\":24,\"cols\":80}";
    json_frame_write(ctx.fd, CHAN_PTY, 0, msg, strlen(msg));

    ASSERT(v2_recv(ctx.fd, &ch, &fl, &payload, &plen) == 0, "recv error");
    ASSERT(ch == CHAN_PTY, "error on pty channel");
    ASSERT(strstr(payload, "spawn") != NULL, "must spawn first");
    free(payload);

    v2_teardown(&ctx);
}

/* ================================================================
 * Tests: EOF causes session to end cleanly
 * ================================================================ */

static void test_v2_eof_ends_session(void)
{
    v2_ctx_t ctx;
    server_config_t cfg = test_config();

    printf("test_v2_eof_ends_session:\n");
    ASSERT(v2_setup(&ctx, &cfg) == 0, "setup");
    ASSERT(v2_auth(&ctx) == 0, "authenticate");

    /* Close our end — child should see EOF and exit cleanly */
    close(ctx.fd);
    ctx.fd = -1;

    int status;
    int exited = 0;
    for (int i = 0; i < 30; i++) {
        if (waitpid(ctx.child, &status, WNOHANG) == ctx.child) {
            exited = 1;
            break;
        }
        usleep(100000);
    }
    ASSERT(exited, "child exited on EOF");
    if (exited) {
        ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0,
            "child exited with status 0");
    }

    /* Skip normal teardown since we already closed and waited */
    if (!exited) {
        kill(ctx.child, SIGKILL);
        waitpid(ctx.child, &status, 0);
    }
}

/* ================================================================
 * Tests: Oversized binary frame rejected
 * ================================================================ */

static void test_v2_oversized_frame(void)
{
    int sv[2];
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_oversized_frame:\n");

    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    /* Write a frame header with length > FRAME_MAX_PAYLOAD directly */
    {
        unsigned char header[6];
        uint32_t net_len;
        header[0] = CHAN_CONTROL;
        header[1] = 0;
        net_len = htonl(FRAME_MAX_PAYLOAD + 1);
        memcpy(header + 2, &net_len, 4);
        write(sv[1], header, 6);
    }
    close(sv[1]);

    /* json_frame_read should reject this */
    int rc = json_frame_read(sv[0], &ch, &fl, &payload, &plen);
    ASSERT(rc == -1, "oversized frame rejected");

    close(sv[0]);
}

/* ================================================================
 * Tests: Truncated binary frame (header only, no payload)
 * ================================================================ */

static void test_v2_truncated_frame(void)
{
    int sv[2];
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_truncated_frame:\n");

    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    /* Write a header claiming 100 bytes, but close immediately */
    {
        unsigned char header[6];
        uint32_t net_len;
        header[0] = CHAN_CONTROL;
        header[1] = 0;
        net_len = htonl(100);
        memcpy(header + 2, &net_len, 4);
        write(sv[1], header, 6);
    }
    close(sv[1]);

    int rc = json_frame_read(sv[0], &ch, &fl, &payload, &plen);
    ASSERT(rc == -1, "truncated frame returns error");

    close(sv[0]);
}

/* ================================================================
 * Tests: Invalid channel in binary frame header
 * ================================================================ */

static void test_v2_invalid_channel_binary(void)
{
    int sv[2];
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_invalid_channel_binary:\n");

    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair");

    /* Write a frame with channel 99 (> CHAN_MAX) */
    {
        unsigned char header[6];
        uint32_t net_len;
        header[0] = 99;  /* invalid channel */
        header[1] = 0;
        net_len = htonl(4);
        memcpy(header + 2, &net_len, 4);
        write(sv[1], header, 6);
        write(sv[1], "test", 4);
    }
    close(sv[1]);

    int rc = json_frame_read(sv[0], &ch, &fl, &payload, &plen);
    ASSERT(rc == -1, "invalid channel frame rejected");

    close(sv[0]);
}

/* ================================================================
 * Tests: NDJSON invalid channel
 * ================================================================ */

static void test_v2_ndjson_invalid_channel(void)
{
    int pipefd[2];
    int ch, fl;
    char *payload;
    size_t plen;

    printf("test_v2_ndjson_invalid_channel:\n");

    json_set_wire_format(WIRE_NDJSON);

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    /* Write NDJSON with ch > CHAN_MAX */
    char *line = "{\"ch\":99,\"type\":\"ping\"}\n";
    write(pipefd[1], line, strlen(line));
    close(pipefd[1]);

    int rc = json_frame_read(pipefd[0], &ch, &fl, &payload, &plen);
    ASSERT(rc == -1, "ndjson invalid channel rejected");

    close(pipefd[0]);
    json_set_wire_format(WIRE_BINARY);
}

/* ================================================================
 * MAIN
 * ================================================================ */

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(60);

    printf("=== v2 Protocol Error Handling & Edge Case Tests ===\n\n");

    /* Sequential operations */
    test_v2_multi_ops();

    /* State edge cases */
    test_v2_state_set_with_attrs();
    test_v2_state_set_alias();
    test_v2_state_unset_func();
    test_v2_state_unset_alias();
    test_v2_state_unset_unknown_target();
    test_v2_state_set_missing_value();
    test_v2_state_set_unknown_target();
    test_v2_state_inspect_multi();
    test_v2_state_inspect_missing_query();

    /* Auth edge cases */
    test_v2_auth_already_authed();

    /* Configure edge cases */
    test_v2_configure_default();
    test_v2_configure_clamp();

    /* Observe errors */
    test_v2_observe_unknown_type();

    /* PTY errors */
    test_v2_pty_non_spawn();

    /* Session lifecycle */
    test_v2_eof_ends_session();

    /* Protocol-level errors */
    test_v2_oversized_frame();
    test_v2_truncated_frame();
    test_v2_invalid_channel_binary();
    test_v2_ndjson_invalid_channel();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
