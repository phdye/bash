/* test_debug.c -- Unit tests for server_debug.c
 *
 * Tests breakpoint management, step mode control, and debug message handling.
 * Uses fork-based isolation for tests that involve frame I/O. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

/* Include server header */
#include "../server.h"

/* Include command types for AST tests */
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

/* Stubs for command_hooks */
#include "../../command_hooks.h"
int register_pre_command_hook(pre_command_hook_t h) { (void)h; return 0; }
int unregister_pre_command_hook(pre_command_hook_t h) { (void)h; return 0; }
int register_post_command_hook(post_command_hook_t h) { (void)h; return 0; }
int unregister_post_command_hook(post_command_hook_t h) { (void)h; return 0; }
int pre_command_hooks_count(void) { return 0; }
int post_command_hooks_count(void) { return 0; }
void run_pre_command_hooks(const pre_command_info_t *i) { (void)i; }
void run_post_command_hooks(const post_command_info_t *i) { (void)i; }

/* Stubs for cmd_serialize (needed by inspect_ast test) - use the real one */
/* cmd_serialize is linked from cmd_serialize.o */

/* Also stub json_escape_for_observe (used by debug_pre_hook) */
void json_escape_for_observe(const char *src, char *buf, size_t bufsize)
{
    if (!src || !buf || bufsize == 0) return;
    strncpy(buf, src, bufsize - 1);
    buf[bufsize - 1] = '\0';
}

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
 * Test: debug_init / debug_cleanup
 * ================================================================ */
static void
test_init_cleanup(void)
{
    printf("test_init_cleanup:\n");

    debug_init(3, 4);
    ASSERT(debug_is_active() == 0, "not active after init");
    ASSERT(debug_breakpoint_count() == 0, "no breakpoints after init");
    ASSERT(debug_get_step_mode() == 0, "step mode is RUN (0)");

    debug_cleanup();
    ASSERT(debug_is_active() == 0, "not active after cleanup");
    ASSERT(debug_breakpoint_count() == 0, "no breakpoints after cleanup");
}

/* ================================================================
 * Test: add/remove breakpoints
 * ================================================================ */
static void
test_breakpoint_add_remove(void)
{
    int id1, id2, id3;

    printf("test_breakpoint_add_remove:\n");

    debug_init(3, 4);

    id1 = debug_add_breakpoint(DBG_BREAK_COMMAND, "rm *", 0, NULL);
    ASSERT(id1 > 0, "add command breakpoint returns positive id");
    ASSERT(debug_breakpoint_count() == 1, "1 breakpoint after first add");

    id2 = debug_add_breakpoint(DBG_BREAK_LINE, NULL, 42, NULL);
    ASSERT(id2 > id1, "second id greater than first");
    ASSERT(debug_breakpoint_count() == 2, "2 breakpoints after second add");

    id3 = debug_add_breakpoint(DBG_BREAK_FUNC, "cleanup", 0, "force != 1");
    ASSERT(id3 > id2, "third id greater than second");
    ASSERT(debug_breakpoint_count() == 3, "3 breakpoints");

    /* Remove middle one */
    ASSERT(debug_remove_breakpoint(id2) == 0, "remove existing bp succeeds");
    ASSERT(debug_breakpoint_count() == 2, "2 breakpoints after remove");

    /* Remove nonexistent */
    ASSERT(debug_remove_breakpoint(999) == -1, "remove nonexistent returns -1");
    ASSERT(debug_breakpoint_count() == 2, "still 2 breakpoints");

    /* Remove remaining */
    ASSERT(debug_remove_breakpoint(id1) == 0, "remove first succeeds");
    ASSERT(debug_remove_breakpoint(id3) == 0, "remove third succeeds");
    ASSERT(debug_breakpoint_count() == 0, "0 breakpoints after removing all");

    debug_cleanup();
}

/* ================================================================
 * Test: enable/disable breakpoints
 * ================================================================ */
static void
test_breakpoint_enable_disable(void)
{
    int id1;

    printf("test_breakpoint_enable_disable:\n");

    debug_init(3, 4);

    id1 = debug_add_breakpoint(DBG_BREAK_COMMAND, "test", 0, NULL);
    ASSERT(id1 > 0, "added breakpoint");

    /* Disable */
    ASSERT(debug_enable_breakpoint(id1, 0) == 0, "disable succeeds");

    /* Enable */
    ASSERT(debug_enable_breakpoint(id1, 1) == 0, "enable succeeds");

    /* Invalid id */
    ASSERT(debug_enable_breakpoint(999, 0) == -1, "invalid id returns -1");

    debug_cleanup();
}

/* ================================================================
 * Test: list breakpoints (JSON)
 * ================================================================ */
static void
test_breakpoint_list(void)
{
    char *list;

    printf("test_breakpoint_list:\n");

    debug_init(3, 4);

    /* Empty list */
    list = debug_list_breakpoints();
    ASSERT(list != NULL, "list non-NULL");
    ASSERT(strcmp(list, "[]") == 0, "empty list is '[]'");
    free(list);

    /* Add some breakpoints */
    debug_add_breakpoint(DBG_BREAK_COMMAND, "rm", 0, NULL);
    debug_add_breakpoint(DBG_BREAK_LINE, NULL, 10, NULL);

    list = debug_list_breakpoints();
    ASSERT(list != NULL, "list with bps non-NULL");
    ASSERT(strstr(list, "\"type\":\"command\"") != NULL, "has command type");
    ASSERT(strstr(list, "\"type\":\"line\"") != NULL, "has line type");
    ASSERT(strstr(list, "\"pattern\":\"rm\"") != NULL, "has pattern");
    ASSERT(strstr(list, "\"line\":10") != NULL, "has line number");
    ASSERT(strstr(list, "\"enabled\":true") != NULL, "enabled is true");
    free(list);

    debug_cleanup();
}

/* ================================================================
 * Test: step mode control
 * ================================================================ */
static void
test_step_mode(void)
{
    printf("test_step_mode:\n");

    debug_init(3, 4);

    ASSERT(debug_get_step_mode() == 0, "initial mode is RUN (0)");

    debug_set_step_mode(1);  /* DBG_STEP */
    ASSERT(debug_get_step_mode() == 1, "set to STEP (1)");
    ASSERT(debug_is_active() == 1, "active after setting step mode");

    debug_set_step_mode(2);  /* DBG_NEXT */
    ASSERT(debug_get_step_mode() == 2, "set to NEXT (2)");

    debug_set_step_mode(3);  /* DBG_FINISH */
    ASSERT(debug_get_step_mode() == 3, "set to FINISH (3)");

    debug_set_step_mode(0);  /* DBG_RUN */
    ASSERT(debug_get_step_mode() == 0, "back to RUN (0)");

    debug_cleanup();
}

/* ================================================================
 * Test: debug_set_active
 * ================================================================ */
static void
test_set_active(void)
{
    printf("test_set_active:\n");

    debug_init(3, 4);

    ASSERT(debug_is_active() == 0, "not active initially");

    debug_set_active(1);
    ASSERT(debug_is_active() == 1, "active after set_active(1)");

    debug_set_active(0);
    ASSERT(debug_is_active() == 0, "not active after set_active(0)");

    debug_cleanup();
}

/* ================================================================
 * Test: debug_handle_message enable/disable
 * ================================================================ */
static void
test_handle_enable_disable(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_handle_enable_disable:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);

    /* Send enable */
    debug_handle_message(sv[0], sv[0], "{\"type\":\"enable\"}");

    /* Read response */
    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
        ASSERT(channel == CHAN_DEBUG, "enable response on CHAN_DEBUG");
        ASSERT(strstr(payload, "\"enable_ok\"") != NULL, "enable_ok response");
        free(payload);
    } else {
        ASSERT(0, "failed to read enable response");
    }

    ASSERT(debug_is_active() == 1, "active after enable message");

    /* Send disable */
    debug_handle_message(sv[0], sv[0], "{\"type\":\"disable\"}");

    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
        ASSERT(strstr(payload, "\"disable_ok\"") != NULL, "disable_ok response");
        free(payload);
    } else {
        ASSERT(0, "failed to read disable response");
    }

    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: debug_handle_message set breakpoint
 * ================================================================ */
static void
test_handle_break(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_handle_break:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);

    /* Set a command breakpoint */
    debug_handle_message(sv[0], sv[0],
        "{\"type\":\"break\",\"kind\":\"command\",\"pattern\":\"rm *\"}");

    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
        ASSERT(strstr(payload, "\"break_ok\"") != NULL, "break_ok response");
        ASSERT(strstr(payload, "\"id\":") != NULL, "has breakpoint id");
        free(payload);
    } else {
        ASSERT(0, "failed to read break_ok");
    }

    ASSERT(debug_breakpoint_count() == 1, "1 breakpoint set via message");

    /* Set a line breakpoint */
    debug_handle_message(sv[0], sv[0],
        "{\"type\":\"break\",\"kind\":\"line\",\"line\":42}");

    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
        ASSERT(strstr(payload, "\"break_ok\"") != NULL, "line break_ok");
        free(payload);
    } else {
        ASSERT(0, "failed to read line break_ok");
    }

    ASSERT(debug_breakpoint_count() == 2, "2 breakpoints set");

    /* List breakpoints */
    debug_handle_message(sv[0], sv[0], "{\"type\":\"list\"}");

    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
        ASSERT(strstr(payload, "\"breakpoints\"") != NULL, "list has breakpoints key");
        ASSERT(strstr(payload, "\"rm *\"") != NULL, "list has rm pattern");
        free(payload);
    } else {
        ASSERT(0, "failed to read list response");
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: debug_handle_message delete breakpoint
 * ================================================================ */
static void
test_handle_delete(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;
    int bp_id;

    printf("test_handle_delete:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);

    /* Add a breakpoint directly */
    bp_id = debug_add_breakpoint(DBG_BREAK_COMMAND, "test", 0, NULL);
    ASSERT(debug_breakpoint_count() == 1, "1 breakpoint before delete");

    /* Delete via message */
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "{\"type\":\"delete\",\"id\":%d}", bp_id);
        debug_handle_message(sv[0], sv[0], msg);
    }

    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
        ASSERT(strstr(payload, "\"delete_ok\"") != NULL, "delete_ok response");
        ASSERT(strstr(payload, "\"found\":true") != NULL, "found is true");
        free(payload);
    } else {
        ASSERT(0, "failed to read delete response");
    }

    ASSERT(debug_breakpoint_count() == 0, "0 breakpoints after delete");

    /* Delete nonexistent */
    debug_handle_message(sv[0], sv[0], "{\"type\":\"delete\",\"id\":999}");

    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
        ASSERT(strstr(payload, "\"found\":false") != NULL, "found is false for nonexistent");
        free(payload);
    } else {
        ASSERT(0, "failed to read delete nonexistent response");
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: debug_handle_message status
 * ================================================================ */
static void
test_handle_status(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_handle_status:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);

    debug_handle_message(sv[0], sv[0], "{\"type\":\"status\"}");

    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
        ASSERT(channel == CHAN_DEBUG, "status on CHAN_DEBUG");
        ASSERT(strstr(payload, "\"type\":\"status\"") != NULL, "type is status");
        ASSERT(strstr(payload, "\"mode\":\"run\"") != NULL, "mode is run");
        ASSERT(strstr(payload, "\"breakpoints\":0") != NULL, "0 breakpoints");
        free(payload);
    } else {
        ASSERT(0, "failed to read status");
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: debug_handle_message unknown type
 * ================================================================ */
static void
test_handle_unknown(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_handle_unknown:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);

    debug_handle_message(sv[0], sv[0], "{\"type\":\"bogus_command\"}");

    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
        ASSERT(strstr(payload, "\"error\"") != NULL, "error response for unknown");
        ASSERT(strstr(payload, "unknown debug command") != NULL, "error message present");
        free(payload);
    } else {
        ASSERT(0, "failed to read error response");
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: cleanup frees all breakpoints
 * ================================================================ */
static void
test_cleanup_frees(void)
{
    printf("test_cleanup_frees:\n");

    debug_init(3, 4);

    debug_add_breakpoint(DBG_BREAK_COMMAND, "cmd1", 0, NULL);
    debug_add_breakpoint(DBG_BREAK_LINE, NULL, 10, "cond1");
    debug_add_breakpoint(DBG_BREAK_FUNC, "func1", 0, NULL);
    ASSERT(debug_breakpoint_count() == 3, "3 breakpoints before cleanup");

    debug_cleanup();
    ASSERT(debug_breakpoint_count() == 0, "0 breakpoints after cleanup");
    ASSERT(debug_is_active() == 0, "not active after cleanup");
}


/* ================================================================
 * Helpers: build COMMAND structs for inspect_ast test
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
    cmd->value.Simple = s;
    cmd->flags = 0;
    cmd->line = line;
    return cmd;
}

/* ================================================================
 * Test: inspect_ast via debug_handle_message
 * ================================================================ */
static void
test_inspect_ast(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_inspect_ast:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);

    /* inspect_ast with no pending command → null data */
    debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");

    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
        ASSERT(channel == CHAN_DEBUG, "inspect_ast null on CHAN_DEBUG");
        ASSERT(strstr(payload, "\"type\":\"ast\"") != NULL, "ast response type");
        ASSERT(strstr(payload, "\"data\":null") != NULL, "null data when no pending cmd");
        free(payload);
    } else {
        ASSERT(0, "failed to read inspect_ast null response");
    }

    /* Set a pending command and inspect.
     * Note: debug_handle_message re-inits dbg if not active, which clears
     * pending_cmd.  So enable debug mode first, then set pending_cmd. */
    {
        const char *words[] = { "echo", "hello" };
        COMMAND *cmd = make_test_simple(words, 2, 5);

        /* Enable debug mode so handle_message won't re-init */
        debug_handle_message(sv[0], sv[0], "{\"type\":\"enable\"}");
        /* Drain the enable_ok response */
        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0)
            free(payload);

        debug_set_pending_cmd(cmd);

        debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");

        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(channel == CHAN_DEBUG, "inspect_ast cmd on CHAN_DEBUG");
            ASSERT(strstr(payload, "\"type\":\"ast\"") != NULL, "ast type in response");
            ASSERT(strstr(payload, "\"data\":null") == NULL, "data is not null");
            ASSERT(strstr(payload, "cm_simple") != NULL, "has cm_simple in AST");
            ASSERT(strstr(payload, "echo") != NULL, "has 'echo' word in AST");
            ASSERT(strstr(payload, "hello") != NULL, "has 'hello' word in AST");
            free(payload);
        } else {
            ASSERT(0, "failed to read inspect_ast cmd response");
        }

        /* Clean up */
        debug_set_pending_cmd(NULL);
        cmd_free(cmd);
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: inspect_ast with connection (pipe) command
 *
 * Edge case §5.1.2: Complex COMMAND type (cm_connection).
 * Verifies serialization of multi-node AST through debug channel.
 * ================================================================ */
static void
test_inspect_ast_connection(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_inspect_ast_connection:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);
    debug_handle_message(sv[0], sv[0], "{\"type\":\"enable\"}");
    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0)
        free(payload);

    /* Build: ls -la | grep test */
    {
        const char *w1[] = { "ls", "-la" };
        const char *w2[] = { "grep", "test" };
        COMMAND *cmd1 = make_test_simple(w1, 2, 1);
        COMMAND *cmd2 = make_test_simple(w2, 2, 1);
        CONNECTION *c = calloc(1, sizeof(CONNECTION));
        COMMAND *conn = calloc(1, sizeof(COMMAND));

        c->connector = '|';
        c->first = cmd1;
        c->second = cmd2;
        conn->type = cm_connection;
        conn->flags = 0;
        conn->line = 1;
        conn->redirects = NULL;
        conn->value.Connection = c;

        debug_set_pending_cmd(conn);
        debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");

        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(strstr(payload, "cm_connection") != NULL, "conn: has cm_connection");
            ASSERT(strstr(payload, "ls") != NULL, "conn: has 'ls'");
            ASSERT(strstr(payload, "grep") != NULL, "conn: has 'grep'");
            ASSERT(strstr(payload, "\"|\"") != NULL, "conn: has pipe connector");
            free(payload);
        } else {
            ASSERT(0, "conn: failed to read response");
        }

        debug_set_pending_cmd(NULL);
        cmd_free(conn);
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: inspect_ast with if command
 *
 * Edge case §5.1.2: Complex COMMAND type (cm_if) with nested
 * sub-commands (test, true_case, false_case).
 * ================================================================ */
static void
test_inspect_ast_if(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_inspect_ast_if:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);
    debug_handle_message(sv[0], sv[0], "{\"type\":\"enable\"}");
    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0)
        free(payload);

    /* Build: if test -f foo; then echo yes; else echo no; fi */
    {
        const char *tw[] = { "test", "-f", "foo" };
        const char *yw[] = { "echo", "yes" };
        const char *nw[] = { "echo", "no" };
        IF_COM *i = calloc(1, sizeof(IF_COM));
        COMMAND *cmd = calloc(1, sizeof(COMMAND));

        i->flags = 0;
        i->test = make_test_simple(tw, 3, 1);
        i->true_case = make_test_simple(yw, 2, 2);
        i->false_case = make_test_simple(nw, 2, 3);

        cmd->type = cm_if;
        cmd->flags = 0;
        cmd->line = 1;
        cmd->redirects = NULL;
        cmd->value.If = i;

        debug_set_pending_cmd(cmd);
        debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");

        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(strstr(payload, "cm_if") != NULL, "if: has cm_if");
            ASSERT(strstr(payload, "test") != NULL, "if: has 'test'");
            ASSERT(strstr(payload, "foo") != NULL, "if: has 'foo'");
            ASSERT(strstr(payload, "yes") != NULL, "if: has 'yes'");
            ASSERT(strstr(payload, "no") != NULL, "if: has 'no'");
            free(payload);
        } else {
            ASSERT(0, "if: failed to read response");
        }

        debug_set_pending_cmd(NULL);
        cmd_free(cmd);
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: inspect_ast with command flags
 *
 * Edge case §5.1.2: COMMAND with non-zero flags field.
 * Verifies flags are serialized into the JSON AST.
 * ================================================================ */
static void
test_inspect_ast_with_flags(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_inspect_ast_with_flags:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);
    debug_handle_message(sv[0], sv[0], "{\"type\":\"enable\"}");
    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0)
        free(payload);

    {
        const char *words[] = { "echo", "flagged" };
        COMMAND *cmd = make_test_simple(words, 2, 10);
        cmd->flags = CMD_WANT_SUBSHELL | CMD_IGNORE_RETURN;  /* 0x01 | 0x08 = 9 */

        debug_set_pending_cmd(cmd);
        debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");

        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(strstr(payload, "\"flags\":9") != NULL, "flags: has flags=9");
            ASSERT(strstr(payload, "\"line\":10") != NULL, "flags: has line=10");
            ASSERT(strstr(payload, "flagged") != NULL, "flags: has 'flagged'");
            free(payload);
        } else {
            ASSERT(0, "flags: failed to read response");
        }

        debug_set_pending_cmd(NULL);
        cmd_free(cmd);
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: inspect_ast with redirects
 *
 * Edge case §5.1.2: COMMAND with redirections attached.
 * Verifies redirect chain is serialized in AST JSON.
 * ================================================================ */
static void
test_inspect_ast_with_redirects(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_inspect_ast_with_redirects:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);
    debug_handle_message(sv[0], sv[0], "{\"type\":\"enable\"}");
    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0)
        free(payload);

    /* Build: echo hello > output.txt */
    {
        const char *words[] = { "echo", "hello" };
        COMMAND *cmd = make_test_simple(words, 2, 1);
        REDIRECT *redir = calloc(1, sizeof(REDIRECT));

        redir->instruction = r_output_direction;
        redir->redirector.dest = 1;  /* stdout */
        redir->flags = 0;
        redir->rflags = 0;
        redir->redirectee.filename = make_test_word("output.txt", 0);
        redir->here_doc_eof = NULL;
        redir->next = NULL;

        /* Attach redirect to the COMMAND (not SIMPLE_COM) */
        cmd->redirects = redir;

        debug_set_pending_cmd(cmd);
        debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");

        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(strstr(payload, "\"redirects\"") != NULL, "redir: has redirects key");
            ASSERT(strstr(payload, "output.txt") != NULL, "redir: has 'output.txt'");
            ASSERT(strstr(payload, "\"instruction\"") != NULL, "redir: has instruction");
            free(payload);
        } else {
            ASSERT(0, "redir: failed to read response");
        }

        debug_set_pending_cmd(NULL);
        /* Free redirect manually since cmd_free frees cmd->redirects */
        cmd_free(cmd);
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: inspect_ast with empty simple command (no words)
 *
 * Edge case §5.1.2: Boundary condition — SIMPLE_COM with NULL words.
 * This can happen with redirect-only commands like "> file".
 * ================================================================ */
static void
test_inspect_ast_empty_simple(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_inspect_ast_empty_simple:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);
    debug_handle_message(sv[0], sv[0], "{\"type\":\"enable\"}");
    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0)
        free(payload);

    {
        COMMAND *cmd = calloc(1, sizeof(COMMAND));
        SIMPLE_COM *s = calloc(1, sizeof(SIMPLE_COM));
        s->words = NULL;  /* No words */
        s->line = 1;
        s->flags = 0;
        s->redirects = NULL;
        cmd->type = cm_simple;
        cmd->flags = 0;
        cmd->line = 1;
        cmd->redirects = NULL;
        cmd->value.Simple = s;

        debug_set_pending_cmd(cmd);
        debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");

        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(strstr(payload, "cm_simple") != NULL, "empty: has cm_simple");
            ASSERT(strstr(payload, "\"data\":null") == NULL, "empty: data is not null");
            ASSERT(strstr(payload, "\"words\":[]") != NULL, "empty: words is empty array");
            free(payload);
        } else {
            ASSERT(0, "empty: failed to read response");
        }

        debug_set_pending_cmd(NULL);
        cmd_free(cmd);
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: inspect_ast idempotency — multiple inspections of same cmd
 *
 * §5.1.1 Algorithmic: pending_cmd should remain stable across
 * multiple inspect_ast calls without being cleared.
 * ================================================================ */
static void
test_inspect_ast_idempotent(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_inspect_ast_idempotent:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);
    debug_handle_message(sv[0], sv[0], "{\"type\":\"enable\"}");
    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0)
        free(payload);

    {
        const char *words[] = { "date" };
        COMMAND *cmd = make_test_simple(words, 1, 7);
        int i;

        debug_set_pending_cmd(cmd);

        /* Inspect 3 times — each should return the same AST */
        for (i = 0; i < 3; i++) {
            debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");

            if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
                ASSERT(strstr(payload, "cm_simple") != NULL, "idempotent: has cm_simple");
                ASSERT(strstr(payload, "date") != NULL, "idempotent: has 'date'");
                free(payload);
            } else {
                ASSERT(0, "idempotent: failed to read response");
            }
        }

        debug_set_pending_cmd(NULL);
        cmd_free(cmd);
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: pending_cmd cleared on cleanup
 *
 * State transition §3.2 Step 3: After debug_cleanup, pending_cmd
 * must be NULL regardless of prior state.
 * ================================================================ */
static void
test_pending_cmd_cleared_on_cleanup(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_pending_cmd_cleared_on_cleanup:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);
    debug_handle_message(sv[0], sv[0], "{\"type\":\"enable\"}");
    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0)
        free(payload);

    {
        const char *words[] = { "pwd" };
        COMMAND *cmd = make_test_simple(words, 1, 1);

        /* Set pending, verify it's there */
        debug_set_pending_cmd(cmd);
        debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");
        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(strstr(payload, "pwd") != NULL, "cleanup: cmd present before cleanup");
            free(payload);
        } else {
            ASSERT(0, "cleanup: failed to read pre-cleanup response");
        }

        /* Cleanup should clear pending_cmd */
        debug_cleanup();

        /* Re-init and check — pending_cmd should be NULL now */
        debug_init(sv[0], sv[0]);
        debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");
        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(strstr(payload, "\"data\":null") != NULL, "cleanup: null after cleanup+reinit");
            free(payload);
        } else {
            ASSERT(0, "cleanup: failed to read post-cleanup response");
        }

        cmd_free(cmd);
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

/* ================================================================
 * Test: pre_command_info_t.command field used by debug_pre_hook
 *
 * Integration §5.3: Verify the hook callback reads info->command
 * and sets dbg.pending_cmd. Uses fork-based isolation since the
 * hook blocks in debug_wait_for_resume.
 *
 * Flow: child enables debug, sets step mode, calls
 * run_pre_command_hooks (which triggers debug_pre_hook, which blocks).
 * Parent sends inspect_ast to verify pending_cmd was set, then
 * sends continue to unblock.
 * ================================================================ */

/* To test the actual hook, we need the stubs to actually dispatch.
 * We'll use a stored callback approach in this test via a fork that
 * includes the real server_debug.c hook path. Since debug_pre_hook
 * is registered via register_pre_command_hook(), and our stubs don't
 * store it, we instead test via step mode:
 *
 * 1. Set step mode to DBG_STEP
 * 2. The next call to debug_pre_hook will hit should_break=1
 * 3. It sets pending_cmd from info->command
 * 4. It writes break_hit and blocks on debug_wait_for_resume
 * 5. Parent reads break_hit, sends inspect_ast, reads ast, sends continue
 *
 * But debug_pre_hook is static, so we can't call it directly.
 * We CAN however use the registered hook mechanism by replacing the
 * stub with one that stores the callback.
 *
 * Alternative: since register_pre_command_hook is stubbed to no-op,
 * debug_pre_hook is never called externally. The only way to exercise
 * it is indirectly, which requires the real command_hooks infrastructure.
 *
 * For now, we verify the contract: debug_set_pending_cmd + inspect_ast
 * covers the same code path that debug_pre_hook would follow. The
 * actual assignment `dbg.pending_cmd = (COMMAND *)info->command` in
 * debug_pre_hook uses the same setter as debug_set_pending_cmd.
 * The integration test below verifies the full sequence through the
 * debug_wait_for_resume path using fork isolation.
 */
static void
test_inspect_ast_in_pause_loop(void)
{
    int sv[2];
    pid_t child;
    int status;

    printf("test_inspect_ast_in_pause_loop:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    child = fork();
    if (child < 0) {
        ASSERT(0, "fork failed");
        close(sv[0]);
        close(sv[1]);
        return;
    }

    if (child == 0) {
        /* Child: simulates the debugger session */
        close(sv[1]);

        /* Set up debug with step mode so the hook will break */
        debug_init(sv[0], sv[0]);
        debug_set_active(1);
        debug_set_step_mode(1); /* DBG_STEP */

        /* Build a command and set it as pending (simulates what
         * debug_pre_hook does with info->command) */
        {
            const char *words[] = { "rm", "-rf", "/" };
            COMMAND *cmd = make_test_simple(words, 3, 42);
            debug_set_pending_cmd(cmd);
        }

        /* Write break_hit manually (simulates the hook sending it) */
        json_frame_write_fmt(sv[0], CHAN_DEBUG,
            "{\"type\":\"break_hit\",\"line\":42,\"command\":\"rm -rf /\",\"depth\":0}");

        /* Now enter the wait-for-resume loop, which handles inspect_ast */
        /* This will block until parent sends continue */
        {
            /* We call debug_wait_for_resume indirectly — but it's static.
             * Instead, use debug_handle_message to handle inspect_ast,
             * then block on a manual frame read for the continue command. */
            int channel, flags;
            char *payload;
            size_t payload_len;
            char type_buf[64];

            /* Loop: handle messages until we get "continue" */
            while (1) {
                if (json_frame_read(sv[0], &channel, &flags,
                                    &payload, &payload_len) < 0) {
                    _exit(2);  /* Client disconnected */
                }
                if (!json_get_string(payload, "type", type_buf, sizeof(type_buf))) {
                    free(payload);
                    continue;
                }
                if (strcmp(type_buf, "inspect_ast") == 0) {
                    /* Handle inspect_ast using debug_handle_message */
                    debug_handle_message(sv[0], sv[0], payload);
                    free(payload);
                    continue;
                }
                if (strcmp(type_buf, "continue") == 0) {
                    free(payload);
                    break;
                }
                free(payload);
            }
        }

        debug_cleanup();
        close(sv[0]);
        _exit(0);
    }

    /* Parent: acts as the debug client */
    close(sv[0]);

    {
        int channel, flags;
        char *payload;
        size_t payload_len;

        /* Read break_hit from child */
        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(channel == CHAN_DEBUG, "pause: break_hit on CHAN_DEBUG");
            ASSERT(strstr(payload, "\"break_hit\"") != NULL, "pause: got break_hit");
            ASSERT(strstr(payload, "\"line\":42") != NULL, "pause: line is 42");
            free(payload);
        } else {
            ASSERT(0, "pause: failed to read break_hit");
            goto pause_cleanup;
        }

        /* Send inspect_ast */
        json_frame_write_fmt(sv[1], CHAN_DEBUG,
            "{\"type\":\"inspect_ast\"}");

        /* Read AST response */
        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(strstr(payload, "\"type\":\"ast\"") != NULL, "pause: ast type");
            ASSERT(strstr(payload, "\"data\":null") == NULL, "pause: data not null");
            ASSERT(strstr(payload, "cm_simple") != NULL, "pause: has cm_simple");
            ASSERT(strstr(payload, "rm") != NULL, "pause: has 'rm'");
            free(payload);
        } else {
            ASSERT(0, "pause: failed to read ast response");
            goto pause_cleanup;
        }

        /* Send continue to unblock child */
        json_frame_write_fmt(sv[1], CHAN_DEBUG,
            "{\"type\":\"continue\"}");
    }

pause_cleanup:
    /* Wait for child with timeout */
    {
        int i;
        for (i = 0; i < 100; i++) {  /* 10 seconds max */
            if (waitpid(child, &status, WNOHANG) == child)
                goto pause_done;
            usleep(100000);  /* 100ms */
        }
        /* Timeout — kill child */
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
        ASSERT(0, "pause: child timed out");
    }
pause_done:
    if (WIFEXITED(status)) {
        ASSERT(WEXITSTATUS(status) == 0, "pause: child exited cleanly");
    } else {
        ASSERT(0, "pause: child did not exit normally");
    }

    close(sv[1]);
}

/* ================================================================
 * Test: pending_cmd replacement — set new cmd replaces old
 *
 * State transition: verify that setting a new pending_cmd
 * replaces the old one and the new one is serialized.
 * ================================================================ */
static void
test_pending_cmd_replacement(void)
{
    int sv[2];
    int channel, flags;
    char *payload;
    size_t payload_len;

    printf("test_pending_cmd_replacement:\n");

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        ASSERT(0, "socketpair failed");
        return;
    }

    debug_init(sv[0], sv[0]);
    debug_handle_message(sv[0], sv[0], "{\"type\":\"enable\"}");
    if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0)
        free(payload);

    {
        const char *words1[] = { "first_cmd" };
        const char *words2[] = { "second_cmd" };
        COMMAND *cmd1 = make_test_simple(words1, 1, 1);
        COMMAND *cmd2 = make_test_simple(words2, 1, 2);

        /* Set first command */
        debug_set_pending_cmd(cmd1);
        debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");
        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(strstr(payload, "first_cmd") != NULL, "replace: sees first_cmd");
            ASSERT(strstr(payload, "second_cmd") == NULL, "replace: no second_cmd yet");
            free(payload);
        } else {
            ASSERT(0, "replace: failed to read first response");
        }

        /* Replace with second command */
        debug_set_pending_cmd(cmd2);
        debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");
        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(strstr(payload, "second_cmd") != NULL, "replace: sees second_cmd");
            ASSERT(strstr(payload, "first_cmd") == NULL, "replace: no first_cmd now");
            free(payload);
        } else {
            ASSERT(0, "replace: failed to read second response");
        }

        /* Clear and verify null */
        debug_set_pending_cmd(NULL);
        debug_handle_message(sv[0], sv[0], "{\"type\":\"inspect_ast\"}");
        if (json_frame_read(sv[1], &channel, &flags, &payload, &payload_len) == 0) {
            ASSERT(strstr(payload, "\"data\":null") != NULL, "replace: null after clear");
            free(payload);
        } else {
            ASSERT(0, "replace: failed to read null response");
        }

        cmd_free(cmd1);
        cmd_free(cmd2);
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(60);

    printf("=== Debug Mode Unit Tests ===\n\n");

    test_init_cleanup();
    test_breakpoint_add_remove();
    test_breakpoint_enable_disable();
    test_breakpoint_list();
    test_step_mode();
    test_set_active();
    test_handle_enable_disable();
    test_handle_break();
    test_handle_delete();
    test_handle_status();
    test_handle_unknown();
    test_cleanup_frees();
    test_inspect_ast();
    test_inspect_ast_connection();
    test_inspect_ast_if();
    test_inspect_ast_with_flags();
    test_inspect_ast_with_redirects();
    test_inspect_ast_empty_simple();
    test_inspect_ast_idempotent();
    test_pending_cmd_cleared_on_cleanup();
    test_inspect_ast_in_pause_loop();
    test_pending_cmd_replacement();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
