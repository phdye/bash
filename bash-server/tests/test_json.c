/* test_json.c -- Unit tests for JSON protocol v2 (server_json.c)
 *
 * Tests the framing layer and JSON helpers independently using pipes,
 * without needing a running bash-server. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>

/* Include server header for constants and function declarations */
#include "../server.h"

/* Stubs for bash internals referenced by server_json.c but not
   exercised by these unit tests (frame I/O + JSON helpers only). */
int last_command_exit_value = 0;
void init_bash_for_session(server_config_t *config) { (void)config; }
int parse_and_execute(char *s, const char *f, int fl) { (void)s; (void)f; (void)fl; return 0; }
int state_handle_get_var(int w, const char *a) { (void)w; (void)a; return 0; }
int state_handle_set_var(int w, const char *a) { (void)w; (void)a; return 0; }
int state_handle_unset_var(int w, const char *a) { (void)w; (void)a; return 0; }
int state_handle_get_func(int w, const char *a) { (void)w; (void)a; return 0; }
int state_handle_unset_func(int w, const char *a) { (void)w; (void)a; return 0; }
int state_handle_get_alias(int w, const char *a) { (void)w; (void)a; return 0; }
int state_handle_set_alias(int w, const char *a) { (void)w; (void)a; return 0; }
int state_handle_unset_alias(int w, const char *a) { (void)w; (void)a; return 0; }
int state_handle_set_trap(int w, const char *a) { (void)w; (void)a; return 0; }
int state_handle_unset_trap(int w, const char *a) { (void)w; (void)a; return 0; }
int state_handle_inspect(int w, const char *a) { (void)w; (void)a; return 0; }
void observe_init(int fd, int level) { (void)fd; (void)level; }
void observe_cleanup(void) { }
int observe_set_level(int level) { return level; }
int pty_handle_spawn(int rfd, int wfd, const char *p) { (void)rfd; (void)wfd; (void)p; return 0; }
void debug_init(int rfd, int wfd) { (void)rfd; (void)wfd; }
void debug_cleanup(void) { }
int debug_handle_message(int rfd, int wfd, const char *p) { (void)rfd; (void)wfd; (void)p; return 0; }

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


/* ================================================================
 * Test: Frame write/read round-trip
 * ================================================================ */
static void
test_frame_roundtrip(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_frame_roundtrip:\n");

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    /* Write a frame */
    ASSERT(json_frame_write(pipefd[1], CHAN_CONTROL, 0,
        "{\"type\":\"ping\"}", 15) == 0, "frame write");

    close(pipefd[1]);

    /* Read it back */
    ASSERT(json_frame_read(pipefd[0], &channel, &flags,
        &payload, &payload_len) == 0, "frame read");

    ASSERT(channel == CHAN_CONTROL, "channel is CONTROL (0)");
    ASSERT(flags == 0, "flags is 0");
    ASSERT(payload_len == 15, "payload length is 15");
    ASSERT(payload != NULL, "payload not NULL");
    ASSERT(strcmp(payload, "{\"type\":\"ping\"}") == 0, "payload matches");

    free(payload);
    close(pipefd[0]);
}


/* ================================================================
 * Test: Multiple frames on different channels
 * ================================================================ */
static void
test_frame_multi_channel(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_frame_multi_channel:\n");

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    /* Write frames on different channels */
    ASSERT(json_frame_write(pipefd[1], CHAN_CONTROL, 0, "A", 1) == 0,
        "write chan 0");
    ASSERT(json_frame_write(pipefd[1], CHAN_COMMAND, 0, "B", 1) == 0,
        "write chan 1");
    ASSERT(json_frame_write(pipefd[1], CHAN_STATE, 0, "C", 1) == 0,
        "write chan 2");

    close(pipefd[1]);

    /* Read them back in order */
    ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
        "read frame 1");
    ASSERT(channel == CHAN_CONTROL && payload[0] == 'A', "frame 1 is chan 0, 'A'");
    free(payload);

    ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
        "read frame 2");
    ASSERT(channel == CHAN_COMMAND && payload[0] == 'B', "frame 2 is chan 1, 'B'");
    free(payload);

    ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
        "read frame 3");
    ASSERT(channel == CHAN_STATE && payload[0] == 'C', "frame 3 is chan 2, 'C'");
    free(payload);

    close(pipefd[0]);
}


/* ================================================================
 * Test: Frame with flags
 * ================================================================ */
static void
test_frame_flags(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_frame_flags:\n");

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    ASSERT(json_frame_write(pipefd[1], CHAN_COMMAND,
        FRAME_FLAG_BINARY | FRAME_FLAG_FINAL,
        "raw", 3) == 0, "write with flags");

    close(pipefd[1]);

    ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
        "read flagged frame");
    ASSERT(channel == CHAN_COMMAND, "channel correct");
    ASSERT(flags == (FRAME_FLAG_BINARY | FRAME_FLAG_FINAL), "flags correct (BINARY|FINAL)");
    ASSERT(payload_len == 3, "payload length correct");

    free(payload);
    close(pipefd[0]);
}


/* ================================================================
 * Test: Empty payload frame
 * ================================================================ */
static void
test_frame_empty(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_frame_empty:\n");

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    ASSERT(json_frame_write(pipefd[1], CHAN_CONTROL, 0, "", 0) == 0,
        "write empty frame");

    close(pipefd[1]);

    ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
        "read empty frame");
    ASSERT(channel == CHAN_CONTROL, "channel correct");
    ASSERT(payload_len == 0, "payload length is 0");
    ASSERT(payload != NULL, "payload is non-NULL (empty string)");
    ASSERT(payload[0] == '\0', "payload is empty string");

    free(payload);
    close(pipefd[0]);
}


/* ================================================================
 * Test: Frame write_fmt
 * ================================================================ */
static void
test_frame_write_fmt(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_frame_write_fmt:\n");

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    ASSERT(json_frame_write_fmt(pipefd[1], CHAN_CONTROL,
        "{\"type\":\"pong\",\"val\":%d}", 42) == 0,
        "frame_write_fmt");

    close(pipefd[1]);

    ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
        "read formatted frame");
    ASSERT(strcmp(payload, "{\"type\":\"pong\",\"val\":42}") == 0,
        "formatted payload matches");

    free(payload);
    close(pipefd[0]);
}


/* ================================================================
 * Test: JSON get_string helper
 * ================================================================ */
static void
test_json_get_string(void)
{
    char buf[256];
    const char *json;
    const char *result;

    printf("test_json_get_string:\n");

    json = "{\"type\":\"auth\",\"token\":\"abc123\"}";

    result = json_get_string(json, "type", buf, sizeof(buf));
    ASSERT(result != NULL, "found type key");
    ASSERT(strcmp(buf, "auth") == 0, "type value is 'auth'");

    result = json_get_string(json, "token", buf, sizeof(buf));
    ASSERT(result != NULL, "found token key");
    ASSERT(strcmp(buf, "abc123") == 0, "token value is 'abc123'");

    result = json_get_string(json, "missing", buf, sizeof(buf));
    ASSERT(result == NULL, "missing key returns NULL");
}


/* ================================================================
 * Test: JSON get_string with escapes
 * ================================================================ */
static void
test_json_get_string_escaped(void)
{
    char buf[256];
    const char *result;

    printf("test_json_get_string_escaped:\n");

    result = json_get_string(
        "{\"msg\":\"hello\\nworld\"}", "msg", buf, sizeof(buf));
    ASSERT(result != NULL, "found msg key");
    ASSERT(strcmp(buf, "hello\nworld") == 0, "unescaped newline");

    result = json_get_string(
        "{\"path\":\"C:\\\\Users\\\\test\"}", "path", buf, sizeof(buf));
    ASSERT(result != NULL, "found path key");
    ASSERT(strcmp(buf, "C:\\Users\\test") == 0, "unescaped backslashes");

    result = json_get_string(
        "{\"q\":\"say \\\"hi\\\"\"}", "q", buf, sizeof(buf));
    ASSERT(result != NULL, "found q key");
    ASSERT(strcmp(buf, "say \"hi\"") == 0, "unescaped quotes");
}


/* ================================================================
 * Test: JSON get_int helper
 * ================================================================ */
static void
test_json_get_int(void)
{
    int val;

    printf("test_json_get_int:\n");

    ASSERT(json_get_int("{\"exit_code\":0}", "exit_code", &val) == 0,
        "found exit_code");
    ASSERT(val == 0, "exit_code is 0");

    ASSERT(json_get_int("{\"exit_code\":42}", "exit_code", &val) == 0,
        "found exit_code 42");
    ASSERT(val == 42, "exit_code is 42");

    ASSERT(json_get_int("{\"exit_code\":-1}", "exit_code", &val) == 0,
        "found negative exit_code");
    ASSERT(val == -1, "exit_code is -1");

    ASSERT(json_get_int("{\"type\":\"auth\"}", "missing", &val) == -1,
        "missing int key returns -1");
}


/* ================================================================
 * Test: Large frame (stress test framing)
 * ================================================================ */
static void
test_frame_large(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;
    size_t large_size = 65536;
    char *large_data;

    printf("test_frame_large:\n");

    large_data = malloc(large_size);
    ASSERT(large_data != NULL, "malloc large data");
    memset(large_data, 'X', large_size);

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    /* Fork a child to write (pipe buffer may not hold 64K) */
    {
        pid_t child = fork();
        if (child == 0) {
            close(pipefd[0]);
            json_frame_write(pipefd[1], CHAN_COMMAND, 0, large_data, large_size);
            close(pipefd[1]);
            _exit(0);
        }
        close(pipefd[1]);

        ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
            "read large frame");
        ASSERT(channel == CHAN_COMMAND, "large frame channel");
        ASSERT(payload_len == large_size, "large frame payload length");
        ASSERT(memcmp(payload, large_data, large_size) == 0, "large frame data matches");

        free(payload);
        close(pipefd[0]);
        waitpid(child, NULL, 0);
    }

    free(large_data);
}


/* ================================================================
 * Test: Frame read on EOF returns -1
 * ================================================================ */
static void
test_frame_eof(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_frame_eof:\n");

    ASSERT(pipe(pipefd) == 0, "pipe creation");
    close(pipefd[1]);  /* Close write end immediately */

    ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == -1,
        "read on closed pipe returns -1");

    close(pipefd[0]);
}


/* ================================================================
 * Test: Protocol version detection
 * ================================================================ */
static void
test_version_detect(void)
{
    int sv[2];
    char first_byte;
    int version;

    printf("test_version_detect:\n");

    /* Test v1 detection: send 'A' (for AUTH) */
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair for v1 test");
    {
        char byte = 'A';
        write(sv[1], &byte, 1);
    }
    version = protocol_detect_version(sv[0], &first_byte);
    ASSERT(version == PROTOCOL_V1, "detected v1 for 'A'");
    ASSERT(first_byte == 'A', "first byte is 'A'");
    close(sv[0]); close(sv[1]);

    /* Test v2 detection: send channel 0 byte */
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair for v2 test");
    {
        unsigned char byte = CHAN_CONTROL;
        write(sv[1], &byte, 1);
    }
    version = protocol_detect_version(sv[0], &first_byte);
    ASSERT(version == PROTOCOL_V2, "detected v2 for channel 0");
    close(sv[0]); close(sv[1]);

    /* Test v2 detection: send channel 2 (STATE) */
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair for v2 chan 2");
    {
        unsigned char byte = CHAN_STATE;
        write(sv[1], &byte, 1);
    }
    version = protocol_detect_version(sv[0], &first_byte);
    ASSERT(version == PROTOCOL_V2, "detected v2 for channel 2");
    close(sv[0]); close(sv[1]);
}


/* ================================================================
 * Test: JSON get_string with multiple keys (disambiguation)
 * ================================================================ */
static void
test_json_get_string_multi(void)
{
    char buf[256];
    const char *result;
    const char *json = "{\"type\":\"set\",\"target\":\"var\",\"name\":\"FOO\",\"value\":\"bar\"}";

    printf("test_json_get_string_multi:\n");

    result = json_get_string(json, "type", buf, sizeof(buf));
    ASSERT(result && strcmp(buf, "set") == 0, "type is 'set'");

    result = json_get_string(json, "target", buf, sizeof(buf));
    ASSERT(result && strcmp(buf, "var") == 0, "target is 'var'");

    result = json_get_string(json, "name", buf, sizeof(buf));
    ASSERT(result && strcmp(buf, "FOO") == 0, "name is 'FOO'");

    result = json_get_string(json, "value", buf, sizeof(buf));
    ASSERT(result && strcmp(buf, "bar") == 0, "value is 'bar'");
}


/* ================================================================
 * NDJSON Tests
 * ================================================================ */

/* Test: NDJSON frame write + read round-trip via pipe */
static void
test_ndjson_frame_roundtrip(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_ndjson_frame_roundtrip:\n");

    /* Switch to NDJSON mode */
    json_set_wire_format(WIRE_NDJSON);

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    /* Write a JSON frame via NDJSON path */
    ASSERT(json_frame_write(pipefd[1], CHAN_COMMAND, 0,
        "{\"type\":\"eval\",\"cmd\":\"ls\"}", 25) == 0,
        "ndjson frame write");

    close(pipefd[1]);

    /* Read it back */
    ASSERT(json_frame_read(pipefd[0], &channel, &flags,
        &payload, &payload_len) == 0, "ndjson frame read");

    ASSERT(channel == CHAN_COMMAND, "channel is COMMAND (1)");
    ASSERT(flags == 0, "flags is 0 (ndjson has no flags)");
    ASSERT(payload != NULL, "payload not NULL");
    /* Payload should contain "ch":1 injected */
    ASSERT(strstr(payload, "\"ch\":1") != NULL, "payload contains ch:1");
    /* And original content */
    ASSERT(strstr(payload, "\"type\":\"eval\"") != NULL, "payload contains type:eval");
    ASSERT(strstr(payload, "\"cmd\":\"ls\"") != NULL, "payload contains cmd:ls");

    free(payload);
    close(pipefd[0]);

    /* Restore binary mode */
    json_set_wire_format(WIRE_BINARY);
}

/* Test: NDJSON frame_write_fmt works correctly */
static void
test_ndjson_frame_write_fmt(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_ndjson_frame_write_fmt:\n");

    json_set_wire_format(WIRE_NDJSON);

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    ASSERT(json_frame_write_fmt(pipefd[1], CHAN_CONTROL,
        "{\"type\":\"pong\",\"val\":%d}", 99) == 0,
        "ndjson write_fmt");

    close(pipefd[1]);

    ASSERT(json_frame_read(pipefd[0], &channel, &flags,
        &payload, &payload_len) == 0, "ndjson read fmt frame");
    ASSERT(channel == CHAN_CONTROL, "channel is CONTROL");
    ASSERT(strstr(payload, "\"ch\":0") != NULL, "payload has ch:0");
    ASSERT(strstr(payload, "\"type\":\"pong\"") != NULL, "payload has type:pong");
    ASSERT(strstr(payload, "\"val\":99") != NULL, "payload has val:99");

    free(payload);
    close(pipefd[0]);

    json_set_wire_format(WIRE_BINARY);
}

/* Test: NDJSON multi-channel extraction */
static void
test_ndjson_channel_extraction(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_ndjson_channel_extraction:\n");

    json_set_wire_format(WIRE_NDJSON);

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    /* Write frames on 3 different channels */
    ASSERT(json_frame_write(pipefd[1], CHAN_CONTROL, 0,
        "{\"type\":\"ping\"}", 15) == 0, "write ch0");
    ASSERT(json_frame_write(pipefd[1], CHAN_STATE, 0,
        "{\"type\":\"get\"}", 14) == 0, "write ch2");
    ASSERT(json_frame_write(pipefd[1], CHAN_PTY, 0,
        "{\"type\":\"data\"}", 15) == 0, "write ch5");

    close(pipefd[1]);

    /* Read and verify channels */
    ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
        "read frame 1");
    ASSERT(channel == CHAN_CONTROL, "frame 1 is ch0");
    free(payload);

    ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
        "read frame 2");
    ASSERT(channel == CHAN_STATE, "frame 2 is ch2");
    free(payload);

    ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
        "read frame 3");
    ASSERT(channel == CHAN_PTY, "frame 3 is ch5");
    free(payload);

    close(pipefd[0]);

    json_set_wire_format(WIRE_BINARY);
}

/* Test: NDJSON large payload round-trip */
static void
test_ndjson_large_payload(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;
    char *large_json;
    size_t data_len = 65536;

    printf("test_ndjson_large_payload:\n");

    json_set_wire_format(WIRE_NDJSON);

    /* Build a large JSON payload: {"data":"XXXX...X"} */
    large_json = malloc(data_len + 32);
    ASSERT(large_json != NULL, "malloc large json");
    {
        int hdr_len = snprintf(large_json, 32, "{\"data\":\"");
        memset(large_json + hdr_len, 'X', data_len);
        memcpy(large_json + hdr_len + data_len, "\"}", 3);  /* includes NUL */
    }
    size_t total_len = strlen(large_json);

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    /* Fork writer to avoid pipe buffer deadlock */
    {
        pid_t child = fork();
        if (child == 0) {
            close(pipefd[0]);
            json_frame_write(pipefd[1], CHAN_COMMAND, 0, large_json, total_len);
            close(pipefd[1]);
            _exit(0);
        }
        close(pipefd[1]);

        ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
            "read large ndjson frame");
        ASSERT(channel == CHAN_COMMAND, "large frame channel");
        ASSERT(payload_len > data_len, "payload has expected size");
        ASSERT(strstr(payload, "\"ch\":1") != NULL, "large payload has ch:1");

        free(payload);
        close(pipefd[0]);
        waitpid(child, NULL, 0);
    }

    free(large_json);

    json_set_wire_format(WIRE_BINARY);
}

/* Test: Binary mode still works after NDJSON tests (regression) */
static void
test_ndjson_binary_mode_unchanged(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_ndjson_binary_mode_unchanged:\n");

    /* Ensure binary mode is active */
    json_set_wire_format(WIRE_BINARY);

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    ASSERT(json_frame_write(pipefd[1], CHAN_CONTROL, 0,
        "{\"type\":\"ping\"}", 15) == 0, "binary write after ndjson tests");

    close(pipefd[1]);

    ASSERT(json_frame_read(pipefd[0], &channel, &flags,
        &payload, &payload_len) == 0, "binary read after ndjson tests");
    ASSERT(channel == CHAN_CONTROL, "binary channel correct");
    ASSERT(payload_len == 15, "binary payload length correct");
    ASSERT(strcmp(payload, "{\"type\":\"ping\"}") == 0, "binary payload matches");

    free(payload);
    close(pipefd[0]);
}

/* Test: protocol_detect_version returns PROTOCOL_V2_NDJSON for '{' */
static void
test_protocol_detect_ndjson(void)
{
    int sv[2];
    char first_byte;
    int version;

    printf("test_protocol_detect_ndjson:\n");

    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair for ndjson test");
    {
        char byte = '{';
        write(sv[1], &byte, 1);
    }
    version = protocol_detect_version(sv[0], &first_byte);
    ASSERT(version == PROTOCOL_V2_NDJSON, "detected ndjson for '{'");
    ASSERT(first_byte == '{', "first byte is '{'");
    close(sv[0]); close(sv[1]);
}

/* Test: 3-way detection — binary, ndjson, v1 all detected correctly */
static void
test_protocol_detect_three_way(void)
{
    int sv[2];
    char first_byte;
    int version;

    printf("test_protocol_detect_three_way:\n");

    /* Binary: channel 0 */
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair for binary");
    { unsigned char b = 0; write(sv[1], &b, 1); }
    version = protocol_detect_version(sv[0], &first_byte);
    ASSERT(version == PROTOCOL_V2, "byte 0x00 -> PROTOCOL_V2");
    close(sv[0]); close(sv[1]);

    /* Binary: channel 5 (max) */
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair for binary ch5");
    { unsigned char b = 5; write(sv[1], &b, 1); }
    version = protocol_detect_version(sv[0], &first_byte);
    ASSERT(version == PROTOCOL_V2, "byte 0x05 -> PROTOCOL_V2");
    close(sv[0]); close(sv[1]);

    /* NDJSON: '{' */
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair for ndjson");
    { char b = '{'; write(sv[1], &b, 1); }
    version = protocol_detect_version(sv[0], &first_byte);
    ASSERT(version == PROTOCOL_V2_NDJSON, "byte '{' -> PROTOCOL_V2_NDJSON");
    close(sv[0]); close(sv[1]);

    /* v1: 'A' (AUTH) */
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair for v1 A");
    { char b = 'A'; write(sv[1], &b, 1); }
    version = protocol_detect_version(sv[0], &first_byte);
    ASSERT(version == PROTOCOL_V1, "byte 'A' -> PROTOCOL_V1");
    close(sv[0]); close(sv[1]);

    /* v1: 'P' (PING) */
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair for v1 P");
    { char b = 'P'; write(sv[1], &b, 1); }
    version = protocol_detect_version(sv[0], &first_byte);
    ASSERT(version == PROTOCOL_V1, "byte 'P' -> PROTOCOL_V1");
    close(sv[0]); close(sv[1]);

    /* Edge: byte 6 (just above CHAN_MAX) — should be v1 */
    ASSERT(socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == 0, "socketpair for byte 6");
    { unsigned char b = 6; write(sv[1], &b, 1); }
    version = protocol_detect_version(sv[0], &first_byte);
    ASSERT(version == PROTOCOL_V1, "byte 0x06 -> PROTOCOL_V1 (above CHAN_MAX)");
    close(sv[0]); close(sv[1]);
}


/* ================================================================
 * Test: protocol_detect_version works on pipe fds (not just sockets)
 *
 * This is the core regression test for the stdio transport bug:
 * recv(MSG_PEEK) returns ENOTSOCK on pipes, so protocol detection
 * must fall back to read() + pushback.
 * ================================================================ */
static void
test_detect_version_on_pipe(void)
{
    int pipefd[2];
    char first_byte;
    int version;

    printf("test_detect_version_on_pipe:\n");

    /* Test NDJSON detection on pipe (the broken case) */
    ASSERT(pipe(pipefd) == 0, "pipe creation");
    {
        const char *msg = "{\"ch\":0,\"type\":\"auth\"}\n";
        write(pipefd[1], msg, strlen(msg));
    }

    version = protocol_detect_version(pipefd[0], &first_byte);
    ASSERT(version == PROTOCOL_V2_NDJSON, "detected NDJSON on pipe fd");
    ASSERT(first_byte == '{', "first byte is '{'");

    /* The pushback byte must allow reading the full line correctly.
       protocol_read_line should return the complete line starting with '{'. */
    {
        char buf[256];
        int n = protocol_read_line(pipefd[0], buf, sizeof(buf));
        ASSERT(n > 0, "protocol_read_line succeeds after pipe detect");
        ASSERT(buf[0] == '{', "pushback byte '{' restored as first char");
        ASSERT(strstr(buf, "\"ch\":0") != NULL, "full NDJSON line readable");
    }

    close(pipefd[0]);
    close(pipefd[1]);

    /* Test v1 detection on pipe */
    ASSERT(pipe(pipefd) == 0, "pipe creation for v1");
    write(pipefd[1], "AUTH token123\n", 14);

    version = protocol_detect_version(pipefd[0], &first_byte);
    ASSERT(version == PROTOCOL_V1, "detected v1 on pipe fd");
    ASSERT(first_byte == 'A', "first byte is 'A'");

    /* Pushback should restore 'A' for the v1 line reader */
    {
        char buf[256];
        int n = protocol_read_line(pipefd[0], buf, sizeof(buf));
        ASSERT(n > 0, "protocol_read_line succeeds after v1 pipe detect");
        ASSERT(strncmp(buf, "AUTH", 4) == 0, "v1 line starts with AUTH");
    }

    close(pipefd[0]);
    close(pipefd[1]);

    /* Test binary v2 detection on pipe */
    ASSERT(pipe(pipefd) == 0, "pipe creation for v2 binary");
    {
        unsigned char data[7] = { CHAN_CONTROL, 0, 0, 0, 0, 1, 'X' };
        write(pipefd[1], data, sizeof(data));
    }

    version = protocol_detect_version(pipefd[0], &first_byte);
    ASSERT(version == PROTOCOL_V2, "detected v2 binary on pipe fd");
    ASSERT((unsigned char)first_byte == CHAN_CONTROL, "first byte is channel 0");

    close(pipefd[0]);
    close(pipefd[1]);
}


/* ================================================================
 * Test: NDJSON frame read works after pipe-based detection
 *
 * Simulates the full data path: detect version on pipe, then read
 * a complete NDJSON frame. The pushback byte from detection must
 * be consumed by the first ndjson_frame_read call.
 * ================================================================ */
static void
test_ndjson_read_after_pipe_detect(void)
{
    int pipefd[2];
    char first_byte;
    int version;
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_ndjson_read_after_pipe_detect:\n");

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    /* Write a complete NDJSON auth message */
    {
        const char *msg = "{\"ch\":0,\"type\":\"auth\",\"token\":\"abc\"}\n";
        write(pipefd[1], msg, strlen(msg));
    }
    close(pipefd[1]);

    /* Detect version (consumes first byte via read, pushes it back) */
    version = protocol_detect_version(pipefd[0], &first_byte);
    ASSERT(version == PROTOCOL_V2_NDJSON, "detected NDJSON");

    /* Switch to NDJSON wire format as session_handle would */
    json_set_wire_format(WIRE_NDJSON);

    /* Read the full frame — pushback byte must be restored */
    ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
        "ndjson frame read after pipe detect");
    ASSERT(channel == CHAN_CONTROL, "channel is CONTROL");
    ASSERT(payload != NULL, "payload not NULL");
    ASSERT(strstr(payload, "\"type\":\"auth\"") != NULL, "payload has auth type");
    ASSERT(strstr(payload, "\"token\":\"abc\"") != NULL, "payload has token");

    free(payload);
    close(pipefd[0]);

    /* Restore binary mode */
    json_set_wire_format(WIRE_BINARY);
}


int
main(void)
{
    /* Unbuffered output for timeout compatibility */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Safety timeout */
    alarm(30);

    printf("=== JSON Protocol v2 Unit Tests ===\n\n");

    test_frame_roundtrip();
    test_frame_multi_channel();
    test_frame_flags();
    test_frame_empty();
    test_frame_write_fmt();
    test_frame_eof();
    test_frame_large();
    test_json_get_string();
    test_json_get_string_escaped();
    test_json_get_int();
    test_json_get_string_multi();
    test_version_detect();

    /* NDJSON tests */
    test_ndjson_frame_roundtrip();
    test_ndjson_frame_write_fmt();
    test_ndjson_channel_extraction();
    test_ndjson_large_payload();
    test_ndjson_binary_mode_unchanged();
    test_protocol_detect_ndjson();
    test_protocol_detect_three_way();

    /* Pipe transport detection tests */
    test_detect_version_on_pipe();
    test_ndjson_read_after_pipe_detect();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
