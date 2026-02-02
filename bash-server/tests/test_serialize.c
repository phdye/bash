/* test_serialize.c -- Unit tests for cmd_serialize.c
 *
 * Tests COMMAND tree ↔ JSON serialization/deserialization.
 * Constructs COMMAND structs manually, serializes to JSON,
 * verifies JSON content, then deserializes and verifies structure. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

/* Include server header for function declarations */
#include "../server.h"

/* Include bash command types */
#include "../../command.h"
#include "../../general.h"

/* ================================================================
 * Stubs for bash internals
 * ================================================================ */
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

/* ================================================================
 * Test framework
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

/* ================================================================
 * Helpers: build COMMAND structs manually
 * ================================================================ */

static WORD_DESC *
make_test_word(const char *text, int flags)
{
    WORD_DESC *w = calloc(1, sizeof(WORD_DESC));
    w->word = strdup(text);
    w->flags = flags;
    return w;
}

static WORD_LIST *
make_test_word_list(const char **words, int count)
{
    WORD_LIST *head = NULL, *tail = NULL;
    int i;
    for (i = 0; i < count; i++) {
        WORD_LIST *node = calloc(1, sizeof(WORD_LIST));
        node->word = make_test_word(words[i], 0);
        node->next = NULL;
        if (tail)
            tail->next = node;
        else
            head = node;
        tail = node;
    }
    return head;
}

static COMMAND *
make_test_simple(const char **words, int count, int line)
{
    COMMAND *cmd = calloc(1, sizeof(COMMAND));
    SIMPLE_COM *s = calloc(1, sizeof(SIMPLE_COM));
    s->words = make_test_word_list(words, count);
    s->line = line;
    s->flags = 0;
    s->redirects = NULL;
    cmd->type = cm_simple;
    cmd->flags = 0;
    cmd->line = line;
    cmd->redirects = NULL;
    cmd->value.Simple = s;
    return cmd;
}

/* Free a manually-built COMMAND (uses cmd_free which works for both) */
static void
free_test_cmd(COMMAND *cmd)
{
    cmd_free(cmd);
}

/* ================================================================
 * Test: cmd_type_from_name
 * ================================================================ */
static void
test_type_names(void)
{
    printf("test_type_names:\n");

    ASSERT(cmd_type_from_name("cm_simple") == cm_simple, "cm_simple");
    ASSERT(cmd_type_from_name("cm_connection") == cm_connection, "cm_connection");
    ASSERT(cmd_type_from_name("cm_for") == cm_for, "cm_for");
    ASSERT(cmd_type_from_name("cm_if") == cm_if, "cm_if");
    ASSERT(cmd_type_from_name("cm_while") == cm_while, "cm_while");
    ASSERT(cmd_type_from_name("cm_until") == cm_until, "cm_until");
    ASSERT(cmd_type_from_name("cm_case") == cm_case, "cm_case");
    ASSERT(cmd_type_from_name("cm_group") == cm_group, "cm_group");
    ASSERT(cmd_type_from_name("cm_subshell") == cm_subshell, "cm_subshell");
    ASSERT(cmd_type_from_name("cm_function_def") == cm_function_def, "cm_function_def");
    ASSERT(cmd_type_from_name("cm_bogus") == -1, "unknown returns -1");
}

/* ================================================================
 * Test: connector_from_name
 * ================================================================ */
static void
test_connector_names(void)
{
    printf("test_connector_names:\n");

    ASSERT(connector_from_name(";") == ';', "semicolon");
    ASSERT(connector_from_name("|") == '|', "pipe");
    ASSERT(connector_from_name("&") == '&', "background");
    ASSERT(connector_from_name("&&") == 288, "AND_AND");
    ASSERT(connector_from_name("||") == 289, "OR_OR");
}

/* ================================================================
 * Test: serialize simple command
 * ================================================================ */
static void
test_serialize_simple(void)
{
    const char *words[] = {"echo", "hello", "world"};
    COMMAND *cmd;
    char *json;

    printf("test_serialize_simple:\n");

    cmd = make_test_simple(words, 3, 1);
    json = cmd_serialize(cmd);

    ASSERT(json != NULL, "serialize returned non-NULL");
    ASSERT(strstr(json, "\"cm_simple\"") != NULL, "type is cm_simple");
    ASSERT(strstr(json, "\"echo\"") != NULL, "has word 'echo'");
    ASSERT(strstr(json, "\"hello\"") != NULL, "has word 'hello'");
    ASSERT(strstr(json, "\"world\"") != NULL, "has word 'world'");
    ASSERT(strstr(json, "\"line\":1") != NULL, "line is 1");

    free(json);
    free_test_cmd(cmd);
}

/* ================================================================
 * Test: serialize NULL command
 * ================================================================ */
static void
test_serialize_null(void)
{
    char *json;

    printf("test_serialize_null:\n");

    json = cmd_serialize(NULL);
    ASSERT(json != NULL, "serialize NULL returned non-NULL");
    ASSERT(strcmp(json, "null") == 0, "serialize NULL returns 'null'");
    free(json);
}

/* ================================================================
 * Test: serialize connection (pipe)
 * ================================================================ */
static void
test_serialize_connection(void)
{
    const char *words1[] = {"ls", "-la"};
    const char *words2[] = {"grep", "test"};
    COMMAND *cmd1, *cmd2, *conn;
    CONNECTION *c;
    char *json;

    printf("test_serialize_connection:\n");

    cmd1 = make_test_simple(words1, 2, 1);
    cmd2 = make_test_simple(words2, 2, 1);

    c = calloc(1, sizeof(CONNECTION));
    c->connector = '|';
    c->first = cmd1;
    c->second = cmd2;

    conn = calloc(1, sizeof(COMMAND));
    conn->type = cm_connection;
    conn->flags = 0;
    conn->line = 1;
    conn->redirects = NULL;
    conn->value.Connection = c;

    json = cmd_serialize(conn);

    ASSERT(json != NULL, "connection serialize non-NULL");
    ASSERT(strstr(json, "\"cm_connection\"") != NULL, "type is cm_connection");
    ASSERT(strstr(json, "\"|\"") != NULL, "connector is pipe");
    ASSERT(strstr(json, "\"ls\"") != NULL, "has 'ls'");
    ASSERT(strstr(json, "\"grep\"") != NULL, "has 'grep'");

    free(json);
    free_test_cmd(conn);
}

/* ================================================================
 * Test: serialize if command
 * ================================================================ */
static void
test_serialize_if(void)
{
    const char *test_words[] = {"test", "-f", "foo"};
    const char *true_words[] = {"echo", "yes"};
    const char *false_words[] = {"echo", "no"};
    COMMAND *cmd;
    IF_COM *i;
    char *json;

    printf("test_serialize_if:\n");

    i = calloc(1, sizeof(IF_COM));
    i->flags = 0;
    i->test = make_test_simple(test_words, 3, 1);
    i->true_case = make_test_simple(true_words, 2, 2);
    i->false_case = make_test_simple(false_words, 2, 3);

    cmd = calloc(1, sizeof(COMMAND));
    cmd->type = cm_if;
    cmd->flags = 0;
    cmd->line = 1;
    cmd->redirects = NULL;
    cmd->value.If = i;

    json = cmd_serialize(cmd);

    ASSERT(json != NULL, "if serialize non-NULL");
    ASSERT(strstr(json, "\"cm_if\"") != NULL, "type is cm_if");
    ASSERT(strstr(json, "\"test\"") != NULL, "has test command");
    ASSERT(strstr(json, "\"yes\"") != NULL, "has true case");
    ASSERT(strstr(json, "\"no\"") != NULL, "has false case");

    free(json);
    free_test_cmd(cmd);
}

/* ================================================================
 * Test: serialize for command
 * ================================================================ */
static void
test_serialize_for(void)
{
    const char *items[] = {"a", "b", "c"};
    const char *body_words[] = {"echo", "$i"};
    COMMAND *cmd;
    FOR_COM *f;
    char *json;

    printf("test_serialize_for:\n");

    f = calloc(1, sizeof(FOR_COM));
    f->flags = 0;
    f->line = 5;
    f->name = make_test_word("i", 0);
    f->map_list = make_test_word_list(items, 3);
    f->action = make_test_simple(body_words, 2, 6);

    cmd = calloc(1, sizeof(COMMAND));
    cmd->type = cm_for;
    cmd->flags = 0;
    cmd->line = 5;
    cmd->redirects = NULL;
    cmd->value.For = f;

    json = cmd_serialize(cmd);

    ASSERT(json != NULL, "for serialize non-NULL");
    ASSERT(strstr(json, "\"cm_for\"") != NULL, "type is cm_for");
    ASSERT(strstr(json, "\"i\"") != NULL, "loop var is 'i'");
    ASSERT(strstr(json, "\"a\"") != NULL, "map_list has 'a'");
    ASSERT(strstr(json, "\"b\"") != NULL, "map_list has 'b'");
    ASSERT(strstr(json, "\"c\"") != NULL, "map_list has 'c'");

    free(json);
    free_test_cmd(cmd);
}

/* ================================================================
 * Test: serialize group command
 * ================================================================ */
static void
test_serialize_group(void)
{
    const char *words[] = {"echo", "grouped"};
    COMMAND *cmd;
    GROUP_COM *g;
    char *json;

    printf("test_serialize_group:\n");

    g = calloc(1, sizeof(GROUP_COM));
    g->command = make_test_simple(words, 2, 1);

    cmd = calloc(1, sizeof(COMMAND));
    cmd->type = cm_group;
    cmd->flags = 0;
    cmd->line = 1;
    cmd->redirects = NULL;
    cmd->value.Group = g;

    json = cmd_serialize(cmd);

    ASSERT(json != NULL, "group serialize non-NULL");
    ASSERT(strstr(json, "\"cm_group\"") != NULL, "type is cm_group");
    ASSERT(strstr(json, "\"grouped\"") != NULL, "has body word");

    free(json);
    free_test_cmd(cmd);
}

/* ================================================================
 * Test: deserialize simple command
 * ================================================================ */
static void
test_deserialize_simple(void)
{
    const char *json =
        "{\"type\":\"cm_simple\",\"flags\":0,\"line\":10,"
        "\"simple\":{\"flags\":0,\"line\":10,"
        "\"words\":[{\"word\":\"ls\",\"flags\":0},{\"word\":\"-la\",\"flags\":0}],"
        "\"redirects\":null}}";
    COMMAND *cmd;

    printf("test_deserialize_simple:\n");

    cmd = cmd_deserialize(json);

    ASSERT(cmd != NULL, "deserialize returned non-NULL");
    ASSERT(cmd->type == cm_simple, "type is cm_simple");
    ASSERT(cmd->line == 10, "line is 10");
    ASSERT(cmd->value.Simple != NULL, "Simple is non-NULL");
    ASSERT(cmd->value.Simple->words != NULL, "words is non-NULL");
    ASSERT(cmd->value.Simple->words->word != NULL, "first word desc non-NULL");
    ASSERT(strcmp(cmd->value.Simple->words->word->word, "ls") == 0, "first word is 'ls'");
    ASSERT(cmd->value.Simple->words->next != NULL, "second word exists");
    ASSERT(strcmp(cmd->value.Simple->words->next->word->word, "-la") == 0, "second word is '-la'");
    ASSERT(cmd->value.Simple->words->next->next == NULL, "no third word");

    cmd_free(cmd);
}

/* ================================================================
 * Test: deserialize connection
 * ================================================================ */
static void
test_deserialize_connection(void)
{
    const char *json =
        "{\"type\":\"cm_connection\",\"flags\":0,\"line\":1,"
        "\"connection\":{\"connector\":\"&&\","
        "\"first\":{\"type\":\"cm_simple\",\"flags\":0,\"line\":1,"
        "\"simple\":{\"flags\":0,\"line\":1,"
        "\"words\":[{\"word\":\"true\",\"flags\":0}],\"redirects\":null}},"
        "\"second\":{\"type\":\"cm_simple\",\"flags\":0,\"line\":1,"
        "\"simple\":{\"flags\":0,\"line\":1,"
        "\"words\":[{\"word\":\"echo\",\"flags\":0},{\"word\":\"ok\",\"flags\":0}],"
        "\"redirects\":null}}}}";
    COMMAND *cmd;

    printf("test_deserialize_connection:\n");

    cmd = cmd_deserialize(json);

    ASSERT(cmd != NULL, "deserialize returned non-NULL");
    ASSERT(cmd->type == cm_connection, "type is cm_connection");
    ASSERT(cmd->value.Connection != NULL, "Connection non-NULL");
    ASSERT(cmd->value.Connection->connector == 288, "connector is AND_AND (288)");
    ASSERT(cmd->value.Connection->first != NULL, "first cmd non-NULL");
    ASSERT(cmd->value.Connection->first->type == cm_simple, "first is cm_simple");
    ASSERT(cmd->value.Connection->second != NULL, "second cmd non-NULL");
    ASSERT(cmd->value.Connection->second->type == cm_simple, "second is cm_simple");

    /* Check first command word */
    ASSERT(cmd->value.Connection->first->value.Simple != NULL, "first Simple non-NULL");
    ASSERT(strcmp(cmd->value.Connection->first->value.Simple->words->word->word, "true") == 0,
        "first cmd is 'true'");

    /* Check second command word */
    ASSERT(strcmp(cmd->value.Connection->second->value.Simple->words->word->word, "echo") == 0,
        "second cmd is 'echo'");

    cmd_free(cmd);
}

/* ================================================================
 * Test: deserialize NULL/invalid
 * ================================================================ */
static void
test_deserialize_invalid(void)
{
    printf("test_deserialize_invalid:\n");

    ASSERT(cmd_deserialize(NULL) == NULL, "NULL returns NULL");
    ASSERT(cmd_deserialize("null") == NULL, "\"null\" returns NULL");
    ASSERT(cmd_deserialize("") == NULL, "empty returns NULL");
    ASSERT(cmd_deserialize("{\"type\":\"cm_bogus\"}") == NULL, "unknown type returns NULL");
    ASSERT(cmd_deserialize("not json") == NULL, "invalid JSON returns NULL");
}

/* ================================================================
 * Test: roundtrip serialize → deserialize
 * ================================================================ */
static void
test_roundtrip(void)
{
    const char *words[] = {"echo", "hello"};
    COMMAND *orig, *restored;
    char *json;

    printf("test_roundtrip:\n");

    orig = make_test_simple(words, 2, 42);
    json = cmd_serialize(orig);

    ASSERT(json != NULL, "serialize non-NULL");

    restored = cmd_deserialize(json);

    ASSERT(restored != NULL, "deserialize non-NULL");
    ASSERT(restored->type == cm_simple, "type preserved");
    ASSERT(restored->value.Simple != NULL, "Simple preserved");
    ASSERT(restored->value.Simple->words != NULL, "words preserved");
    ASSERT(strcmp(restored->value.Simple->words->word->word, "echo") == 0,
        "first word preserved as 'echo'");
    ASSERT(restored->value.Simple->words->next != NULL, "second word exists");
    ASSERT(strcmp(restored->value.Simple->words->next->word->word, "hello") == 0,
        "second word preserved as 'hello'");

    free(json);
    cmd_free(restored);
    free_test_cmd(orig);
}

/* ================================================================
 * Test: deserialize if command
 * ================================================================ */
static void
test_deserialize_if(void)
{
    const char *json =
        "{\"type\":\"cm_if\",\"flags\":0,\"line\":1,"
        "\"if\":{\"flags\":0,"
        "\"test\":{\"type\":\"cm_simple\",\"flags\":0,\"line\":1,"
        "\"simple\":{\"flags\":0,\"line\":1,"
        "\"words\":[{\"word\":\"test\",\"flags\":0}],\"redirects\":null}},"
        "\"true_case\":{\"type\":\"cm_simple\",\"flags\":0,\"line\":2,"
        "\"simple\":{\"flags\":0,\"line\":2,"
        "\"words\":[{\"word\":\"echo\",\"flags\":0},{\"word\":\"yes\",\"flags\":0}],"
        "\"redirects\":null}},"
        "\"false_case\":null}}";
    COMMAND *cmd;

    printf("test_deserialize_if:\n");

    cmd = cmd_deserialize(json);

    ASSERT(cmd != NULL, "deserialize non-NULL");
    ASSERT(cmd->type == cm_if, "type is cm_if");
    ASSERT(cmd->value.If != NULL, "If non-NULL");
    ASSERT(cmd->value.If->test != NULL, "test non-NULL");
    ASSERT(cmd->value.If->true_case != NULL, "true_case non-NULL");
    ASSERT(cmd->value.If->false_case == NULL, "false_case is NULL");

    cmd_free(cmd);
}

/* ================================================================
 * Test: deserialize for command
 * ================================================================ */
static void
test_deserialize_for(void)
{
    const char *json =
        "{\"type\":\"cm_for\",\"flags\":0,\"line\":5,"
        "\"for\":{\"flags\":0,\"line\":5,\"name\":\"x\","
        "\"map_list\":[{\"word\":\"1\",\"flags\":0},{\"word\":\"2\",\"flags\":0}],"
        "\"action\":{\"type\":\"cm_simple\",\"flags\":0,\"line\":6,"
        "\"simple\":{\"flags\":0,\"line\":6,"
        "\"words\":[{\"word\":\"echo\",\"flags\":0}],\"redirects\":null}}}}";
    COMMAND *cmd;

    printf("test_deserialize_for:\n");

    cmd = cmd_deserialize(json);

    ASSERT(cmd != NULL, "deserialize non-NULL");
    ASSERT(cmd->type == cm_for, "type is cm_for");
    ASSERT(cmd->value.For != NULL, "For non-NULL");
    ASSERT(cmd->value.For->name != NULL, "name non-NULL");
    ASSERT(strcmp(cmd->value.For->name->word, "x") == 0, "loop var is 'x'");
    ASSERT(cmd->value.For->map_list != NULL, "map_list non-NULL");
    ASSERT(strcmp(cmd->value.For->map_list->word->word, "1") == 0, "first item is '1'");
    ASSERT(cmd->value.For->action != NULL, "action non-NULL");

    cmd_free(cmd);
}

/* ================================================================
 * Test: serialize/deserialize subshell
 * ================================================================ */
static void
test_subshell(void)
{
    const char *words[] = {"echo", "sub"};
    COMMAND *cmd;
    SUBSHELL_COM *s;
    char *json;
    COMMAND *restored;

    printf("test_subshell:\n");

    s = calloc(1, sizeof(SUBSHELL_COM));
    s->flags = 0;
    s->line = 3;
    s->command = make_test_simple(words, 2, 3);

    cmd = calloc(1, sizeof(COMMAND));
    cmd->type = cm_subshell;
    cmd->flags = 0;
    cmd->line = 3;
    cmd->redirects = NULL;
    cmd->value.Subshell = s;

    json = cmd_serialize(cmd);
    ASSERT(json != NULL, "subshell serialize non-NULL");
    ASSERT(strstr(json, "\"cm_subshell\"") != NULL, "type is cm_subshell");

    restored = cmd_deserialize(json);
    ASSERT(restored != NULL, "subshell deserialize non-NULL");
    ASSERT(restored->type == cm_subshell, "restored type is cm_subshell");
    ASSERT(restored->value.Subshell != NULL, "Subshell non-NULL");
    ASSERT(restored->value.Subshell->command != NULL, "inner command non-NULL");

    free(json);
    cmd_free(restored);
    free_test_cmd(cmd);
}

/* ================================================================
 * Test: serialize with special characters in words
 * ================================================================ */
static void
test_serialize_escaping(void)
{
    const char *words[] = {"echo", "hello\nworld", "foo\"bar"};
    COMMAND *cmd;
    char *json;

    printf("test_serialize_escaping:\n");

    cmd = make_test_simple(words, 3, 1);
    json = cmd_serialize(cmd);

    ASSERT(json != NULL, "serialize with escaping non-NULL");
    ASSERT(strstr(json, "\\n") != NULL, "newline escaped");
    ASSERT(strstr(json, "\\\"") != NULL, "quote escaped");

    free(json);
    free_test_cmd(cmd);
}


int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(30);

    printf("=== COMMAND Serialize Unit Tests ===\n\n");

    test_type_names();
    test_connector_names();
    test_serialize_simple();
    test_serialize_null();
    test_serialize_connection();
    test_serialize_if();
    test_serialize_for();
    test_serialize_group();
    test_deserialize_simple();
    test_deserialize_connection();
    test_deserialize_invalid();
    test_roundtrip();
    test_deserialize_if();
    test_deserialize_for();
    test_subshell();
    test_serialize_escaping();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
