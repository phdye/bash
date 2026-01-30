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

        /* Clean up: clear pending and free cmd (leak the internals for test simplicity) */
        debug_set_pending_cmd(NULL);
        free(cmd->value.Simple->words->next->word->word);
        free(cmd->value.Simple->words->next->word);
        free(cmd->value.Simple->words->next);
        free(cmd->value.Simple->words->word->word);
        free(cmd->value.Simple->words->word);
        free(cmd->value.Simple->words);
        free(cmd->value.Simple);
        free(cmd);
    }

    debug_cleanup();
    close(sv[0]);
    close(sv[1]);
}

int
main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    alarm(30);

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

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
