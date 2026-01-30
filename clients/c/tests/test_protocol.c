/*
 * test_protocol.c - Unit tests for base64 and JSON helpers.
 */

#include "bashclient.h"
#include "../src/internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static int tests_run = 0, tests_passed = 0;

#define TEST(name) do { \
    tests_run++; \
    printf("  %s ... ", #name); \
    if (test_##name()) { tests_passed++; printf("PASS\n"); } \
    else printf("FAIL\n"); \
} while(0)

#define ASSERT(cond) do { if (!(cond)) { printf("[line %d: %s] ", __LINE__, #cond); return 0; } } while(0)

static int test_b64_roundtrip(void)
{
    const char *input = "hello world";
    char *enc = bc_b64_encode(input, strlen(input));
    ASSERT(enc != NULL);
    size_t dlen;
    char *dec = bc_b64_decode(enc, &dlen);
    ASSERT(dec != NULL);
    ASSERT(dlen == strlen(input));
    ASSERT(strcmp(dec, input) == 0);
    free(enc); free(dec);
    return 1;
}

static int test_b64_empty(void)
{
    char *enc = bc_b64_encode("", 0);
    ASSERT(enc != NULL);
    ASSERT(strlen(enc) == 0);
    free(enc);
    return 1;
}

static int test_b64_binary(void)
{
    char data[] = {0, 1, 2, 127, (char)128, (char)255};
    char *enc = bc_b64_encode(data, 6);
    ASSERT(enc != NULL);
    size_t dlen;
    char *dec = bc_b64_decode(enc, &dlen);
    ASSERT(dlen == 6);
    ASSERT(memcmp(dec, data, 6) == 0);
    free(enc); free(dec);
    return 1;
}

static int test_json_get_string(void)
{
    const char *json = "{\"name\":\"hello\",\"value\":\"world\"}";
    char *v = bc_json_get_string(json, "name");
    ASSERT(v != NULL);
    ASSERT(strcmp(v, "hello") == 0);
    free(v);
    v = bc_json_get_string(json, "value");
    ASSERT(strcmp(v, "world") == 0);
    free(v);
    return 1;
}

static int test_json_get_int(void)
{
    const char *json = "{\"ch\":3,\"exit_code\":42,\"active\":true}";
    ASSERT(bc_json_get_int(json, "ch", -1) == 3);
    ASSERT(bc_json_get_int(json, "exit_code", -1) == 42);
    ASSERT(bc_json_get_int(json, "active", 0) == 1);
    ASSERT(bc_json_get_int(json, "missing", -1) == -1);
    return 1;
}

static int test_json_get_string_escaped(void)
{
    const char *json = "{\"cmd\":\"echo \\\"hello\\\"\"}";
    char *v = bc_json_get_string(json, "cmd");
    ASSERT(v != NULL);
    ASSERT(strcmp(v, "echo \"hello\"") == 0);
    free(v);
    return 1;
}

static int test_json_get_object(void)
{
    const char *json = "{\"data\":{\"name\":\"x\",\"val\":1}}";
    char *obj = bc_json_get_object(json, "data");
    ASSERT(obj != NULL);
    ASSERT(obj[0] == '{');
    char *name = bc_json_get_string(obj, "name");
    ASSERT(strcmp(name, "x") == 0);
    free(name); free(obj);
    return 1;
}

static int test_json_get_llong(void)
{
    const char *json = "{\"ts\":1704067200000}";
    long long ts = bc_json_get_llong(json, "ts", 0);
    ASSERT(ts == 1704067200000LL);
    return 1;
}

int main(void)
{
    alarm(10);
    printf("test_protocol:\n");

    TEST(b64_roundtrip);
    TEST(b64_empty);
    TEST(b64_binary);
    TEST(json_get_string);
    TEST(json_get_int);
    TEST(json_get_string_escaped);
    TEST(json_get_object);
    TEST(json_get_llong);

    printf("%d/%d passed\n", tests_passed, tests_run);
    return tests_passed == tests_run ? 0 : 1;
}
