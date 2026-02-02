/* test_protocol.c -- Unit tests for server_protocol.c
 *
 * Compile (from bash-server/tests/):
 *   gcc -I.. -I../.. -DHAVE_CONFIG_H -o test_protocol \
 *       test_protocol.c ../server_protocol.c
 *
 * Tests: base64 encode/decode, secure_compare, parse_command, read/write_line
 */

#include "server.h"
#include <assert.h>
#include <sys/socket.h>
#include <signal.h>

/* Test counters */
static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %-50s ", name); \
    fflush(stdout); \
} while(0)

#define PASS() do { tests_passed++; printf("PASS\n"); } while(0)
#define FAIL(msg) do { tests_failed++; printf("FAIL: %s\n", msg); } while(0)

/* ================================================================
 * Base64 encode tests
 * ================================================================ */
static void
test_base64_encode_empty(void)
{
    char *result;
    TEST("base64_encode: empty string");
    result = protocol_base64_encode("", 0);
    if (result && strcmp(result, "") == 0)
        PASS();
    else
        FAIL(result ? result : "NULL");
    free(result);
}

static void
test_base64_encode_single(void)
{
    char *result;
    TEST("base64_encode: single byte 'f'");
    result = protocol_base64_encode("f", 1);
    if (result && strcmp(result, "Zg==") == 0)
        PASS();
    else
        FAIL(result ? result : "NULL");
    free(result);
}

static void
test_base64_encode_two(void)
{
    char *result;
    TEST("base64_encode: two bytes 'fo'");
    result = protocol_base64_encode("fo", 2);
    if (result && strcmp(result, "Zm8=") == 0)
        PASS();
    else
        FAIL(result ? result : "NULL");
    free(result);
}

static void
test_base64_encode_three(void)
{
    char *result;
    TEST("base64_encode: three bytes 'foo'");
    result = protocol_base64_encode("foo", 3);
    if (result && strcmp(result, "Zm9v") == 0)
        PASS();
    else
        FAIL(result ? result : "NULL");
    free(result);
}

static void
test_base64_encode_hello(void)
{
    char *result;
    TEST("base64_encode: 'Hello, World!'");
    result = protocol_base64_encode("Hello, World!", 13);
    if (result && strcmp(result, "SGVsbG8sIFdvcmxkIQ==") == 0)
        PASS();
    else
        FAIL(result ? result : "NULL");
    free(result);
}

static void
test_base64_encode_binary(void)
{
    char *result;
    char bin[] = {0x00, 0x01, 0x02, 0xff, 0xfe, 0xfd};
    TEST("base64_encode: binary data with NULs");
    result = protocol_base64_encode(bin, 6);
    if (result && strlen(result) == 8)  /* 6 bytes -> 8 base64 chars */
        PASS();
    else
        FAIL(result ? result : "NULL");
    free(result);
}

/* ================================================================
 * Base64 decode tests
 * ================================================================ */
static void
test_base64_decode_empty(void)
{
    char *result;
    size_t len = 99;
    TEST("base64_decode: empty string");
    result = protocol_base64_decode("", &len);
    if (result && len == 0)
        PASS();
    else
        FAIL("expected empty result");
    free(result);
}

static void
test_base64_decode_single(void)
{
    char *result;
    size_t len;
    TEST("base64_decode: 'Zg==' -> 'f'");
    result = protocol_base64_decode("Zg==", &len);
    if (result && len == 1 && result[0] == 'f')
        PASS();
    else
        FAIL(result ? "wrong content" : "NULL");
    free(result);
}

static void
test_base64_decode_hello(void)
{
    char *result;
    size_t len;
    TEST("base64_decode: 'SGVsbG8sIFdvcmxkIQ=='");
    result = protocol_base64_decode("SGVsbG8sIFdvcmxkIQ==", &len);
    if (result && len == 13 && memcmp(result, "Hello, World!", 13) == 0)
        PASS();
    else
        FAIL(result ? "wrong content" : "NULL");
    free(result);
}

static void
test_base64_decode_invalid_length(void)
{
    char *result;
    size_t len = 99;
    TEST("base64_decode: invalid length (not multiple of 4)");
    result = protocol_base64_decode("abc", &len);
    if (!result && len == 0)
        PASS();
    else
        FAIL("expected NULL for invalid input");
    free(result);
}

static void
test_base64_roundtrip(void)
{
    const char *original = "The quick brown fox jumps over the lazy dog";
    char *encoded, *decoded;
    size_t len;
    TEST("base64_roundtrip: encode then decode");
    encoded = protocol_base64_encode(original, strlen(original));
    if (!encoded) { FAIL("encode returned NULL"); return; }
    decoded = protocol_base64_decode(encoded, &len);
    if (decoded && len == strlen(original) && memcmp(decoded, original, len) == 0)
        PASS();
    else
        FAIL("roundtrip mismatch");
    free(encoded);
    free(decoded);
}

static void
test_base64_roundtrip_binary(void)
{
    /* All 256 byte values */
    char original[256];
    char *encoded, *decoded;
    size_t len;
    int i;
    TEST("base64_roundtrip: all 256 byte values");
    for (i = 0; i < 256; i++)
        original[i] = (char)i;
    encoded = protocol_base64_encode(original, 256);
    if (!encoded) { FAIL("encode returned NULL"); return; }
    decoded = protocol_base64_decode(encoded, &len);
    if (decoded && len == 256 && memcmp(decoded, original, 256) == 0)
        PASS();
    else
        FAIL("roundtrip mismatch");
    free(encoded);
    free(decoded);
}

/* ================================================================
 * Secure compare tests
 * ================================================================ */
static void
test_secure_compare_equal(void)
{
    TEST("secure_compare: equal strings");
    if (protocol_secure_compare("hello", "hello"))
        PASS();
    else
        FAIL("expected match");
}

static void
test_secure_compare_differ(void)
{
    TEST("secure_compare: different strings");
    if (!protocol_secure_compare("hello", "world"))
        PASS();
    else
        FAIL("expected no match");
}

static void
test_secure_compare_length_mismatch(void)
{
    TEST("secure_compare: different lengths");
    if (!protocol_secure_compare("hello", "hell"))
        PASS();
    else
        FAIL("expected no match");
}

static void
test_secure_compare_empty(void)
{
    TEST("secure_compare: two empty strings");
    if (protocol_secure_compare("", ""))
        PASS();
    else
        FAIL("expected match");
}

static void
test_secure_compare_one_empty(void)
{
    TEST("secure_compare: one empty, one not");
    if (!protocol_secure_compare("", "x"))
        PASS();
    else
        FAIL("expected no match");
}

static void
test_secure_compare_hex_tokens(void)
{
    /* Simulate 64-char hex tokens */
    char a[65], b[65];
    int i;
    TEST("secure_compare: 64-char hex tokens (match)");
    for (i = 0; i < 64; i++) a[i] = "0123456789abcdef"[i % 16];
    a[64] = '\0';
    memcpy(b, a, 65);
    if (protocol_secure_compare(a, b))
        PASS();
    else
        FAIL("expected match");
}

static void
test_secure_compare_hex_tokens_differ(void)
{
    char a[65], b[65];
    int i;
    TEST("secure_compare: 64-char hex tokens (last char differs)");
    for (i = 0; i < 64; i++) a[i] = "0123456789abcdef"[i % 16];
    a[64] = '\0';
    memcpy(b, a, 65);
    b[63] = (b[63] == 'a') ? 'b' : 'a';
    if (!protocol_secure_compare(a, b))
        PASS();
    else
        FAIL("expected no match");
}

/* ================================================================
 * Parse command tests
 * ================================================================ */
static void
test_parse_cmd_simple(void)
{
    char cmd[32], arg[256];
    TEST("parse_command: 'AUTH token123'");
    if (protocol_parse_command("AUTH token123", cmd, arg, sizeof(arg)) == 0 &&
        strcmp(cmd, "AUTH") == 0 && strcmp(arg, "token123") == 0)
        PASS();
    else
        FAIL(cmd);
}

static void
test_parse_cmd_no_arg(void)
{
    char cmd[32], arg[256];
    TEST("parse_command: 'PING' (no argument)");
    if (protocol_parse_command("PING", cmd, arg, sizeof(arg)) == 0 &&
        strcmp(cmd, "PING") == 0 && arg[0] == '\0')
        PASS();
    else
        FAIL(cmd);
}

static void
test_parse_cmd_lowercase(void)
{
    char cmd[32], arg[256];
    TEST("parse_command: 'auth token' (lowercase -> uppercase)");
    if (protocol_parse_command("auth token", cmd, arg, sizeof(arg)) == 0 &&
        strcmp(cmd, "AUTH") == 0 && strcmp(arg, "token") == 0)
        PASS();
    else
        FAIL(cmd);
}

static void
test_parse_cmd_leading_space(void)
{
    char cmd[32], arg[256];
    TEST("parse_command: '  PING' (leading whitespace)");
    if (protocol_parse_command("  PING", cmd, arg, sizeof(arg)) == 0 &&
        strcmp(cmd, "PING") == 0)
        PASS();
    else
        FAIL(cmd);
}

static void
test_parse_cmd_empty(void)
{
    char cmd[32], arg[256];
    TEST("parse_command: '' (empty string)");
    if (protocol_parse_command("", cmd, arg, sizeof(arg)) < 0)
        PASS();
    else
        FAIL("expected error for empty command");
}

static void
test_parse_cmd_whitespace_only(void)
{
    char cmd[32], arg[256];
    TEST("parse_command: '   ' (whitespace only)");
    if (protocol_parse_command("   ", cmd, arg, sizeof(arg)) < 0)
        PASS();
    else
        FAIL("expected error for whitespace-only");
}

static void
test_parse_cmd_eval_with_spaces(void)
{
    char cmd[32], arg[256];
    TEST("parse_command: 'EVAL echo hello world'");
    if (protocol_parse_command("EVAL echo hello world", cmd, arg, sizeof(arg)) == 0 &&
        strcmp(cmd, "EVAL") == 0 && strcmp(arg, "echo hello world") == 0)
        PASS();
    else
        FAIL(arg);
}

static void
test_parse_cmd_trailing_space(void)
{
    char cmd[32], arg[256];
    TEST("parse_command: 'AUTH token   ' (trailing whitespace)");
    if (protocol_parse_command("AUTH token   ", cmd, arg, sizeof(arg)) == 0 &&
        strcmp(cmd, "AUTH") == 0 && strcmp(arg, "token") == 0)
        PASS();
    else
        FAIL(arg);
}

static void
test_parse_cmd_multiple_spaces(void)
{
    char cmd[32], arg[256];
    TEST("parse_command: 'AUTH   token' (multiple spaces)");
    if (protocol_parse_command("AUTH   token", cmd, arg, sizeof(arg)) == 0 &&
        strcmp(cmd, "AUTH") == 0 && strcmp(arg, "token") == 0)
        PASS();
    else
        FAIL(arg);
}

/* ================================================================
 * Read/write line tests (using socketpair)
 * ================================================================ */
static void
test_write_read_line_simple(void)
{
    int sv[2];
    char buf[256];
    int n;
    TEST("write_line + read_line: simple message");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        FAIL("socketpair failed");
        return;
    }
    protocol_write_line(sv[0], "OK hello");
    shutdown(sv[0], SHUT_WR);
    n = protocol_read_line(sv[1], buf, sizeof(buf));
    if (n > 0 && strcmp(buf, "OK hello") == 0)
        PASS();
    else
        FAIL(buf);
    close(sv[0]);
    close(sv[1]);
}

static void
test_write_read_line_formatted(void)
{
    int sv[2];
    char buf[256];
    int n;
    TEST("write_line + read_line: formatted '%s %d'");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        FAIL("socketpair failed");
        return;
    }
    protocol_write_line(sv[0], "%s %d", "EXIT", 42);
    shutdown(sv[0], SHUT_WR);
    n = protocol_read_line(sv[1], buf, sizeof(buf));
    if (n > 0 && strcmp(buf, "EXIT 42") == 0)
        PASS();
    else
        FAIL(buf);
    close(sv[0]);
    close(sv[1]);
}

static void
test_read_line_eof(void)
{
    int sv[2];
    char buf[256];
    int n;
    TEST("read_line: EOF returns -1");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        FAIL("socketpair failed");
        return;
    }
    close(sv[0]); /* Close writer immediately */
    n = protocol_read_line(sv[1], buf, sizeof(buf));
    if (n < 0)
        PASS();
    else
        FAIL("expected -1 on EOF");
    close(sv[1]);
}

static void
test_read_line_crlf(void)
{
    int sv[2];
    char buf[256];
    int n;
    TEST("read_line: strips CR from CRLF");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        FAIL("socketpair failed");
        return;
    }
    write(sv[0], "PING\r\n", 6);
    shutdown(sv[0], SHUT_WR);
    n = protocol_read_line(sv[1], buf, sizeof(buf));
    if (n > 0 && strcmp(buf, "PING") == 0)
        PASS();
    else
        FAIL(buf);
    close(sv[0]);
    close(sv[1]);
}

static void
test_read_line_multiple(void)
{
    int sv[2];
    char buf[256];
    int n;
    TEST("read_line: reads multiple lines sequentially");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        FAIL("socketpair failed");
        return;
    }
    write(sv[0], "LINE1\nLINE2\nLINE3\n", 18);
    shutdown(sv[0], SHUT_WR);

    n = protocol_read_line(sv[1], buf, sizeof(buf));
    if (n <= 0 || strcmp(buf, "LINE1") != 0) { FAIL("line 1"); goto cleanup; }
    n = protocol_read_line(sv[1], buf, sizeof(buf));
    if (n <= 0 || strcmp(buf, "LINE2") != 0) { FAIL("line 2"); goto cleanup; }
    n = protocol_read_line(sv[1], buf, sizeof(buf));
    if (n <= 0 || strcmp(buf, "LINE3") != 0) { FAIL("line 3"); goto cleanup; }
    PASS();
cleanup:
    close(sv[0]);
    close(sv[1]);
}

static void
test_write_line_empty(void)
{
    int sv[2];
    char buf[256];
    int n;
    TEST("write_line + read_line: empty format string");
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        FAIL("socketpair failed");
        return;
    }
    protocol_write_line(sv[0], "");
    shutdown(sv[0], SHUT_WR);
    n = protocol_read_line(sv[1], buf, sizeof(buf));
    /* Empty write should produce just a newline, which read_line returns as empty string */
    if (n == 0 && buf[0] == '\0')
        PASS();
    else
        FAIL("expected empty line");
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Main
 * ================================================================ */
int
main(void)
{
    /* Timeout protection */
    alarm(10);

    printf("=== bash-server protocol unit tests ===\n\n");

    printf("[base64 encode]\n");
    test_base64_encode_empty();
    test_base64_encode_single();
    test_base64_encode_two();
    test_base64_encode_three();
    test_base64_encode_hello();
    test_base64_encode_binary();

    printf("\n[base64 decode]\n");
    test_base64_decode_empty();
    test_base64_decode_single();
    test_base64_decode_hello();
    test_base64_decode_invalid_length();
    test_base64_roundtrip();
    test_base64_roundtrip_binary();

    printf("\n[secure_compare]\n");
    test_secure_compare_equal();
    test_secure_compare_differ();
    test_secure_compare_length_mismatch();
    test_secure_compare_empty();
    test_secure_compare_one_empty();
    test_secure_compare_hex_tokens();
    test_secure_compare_hex_tokens_differ();

    printf("\n[parse_command]\n");
    test_parse_cmd_simple();
    test_parse_cmd_no_arg();
    test_parse_cmd_lowercase();
    test_parse_cmd_leading_space();
    test_parse_cmd_empty();
    test_parse_cmd_whitespace_only();
    test_parse_cmd_eval_with_spaces();
    test_parse_cmd_trailing_space();
    test_parse_cmd_multiple_spaces();

    printf("\n[read/write line]\n");
    test_write_read_line_simple();
    test_write_read_line_formatted();
    test_read_line_eof();
    test_read_line_crlf();
    test_read_line_multiple();
    test_write_line_empty();

    printf("\n=== Results: %d/%d passed, %d failed ===\n",
           tests_passed, tests_run, tests_failed);

    return tests_failed > 0 ? 1 : 0;
}
