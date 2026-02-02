/* test_observe.c -- Unit tests for observability hooks (server_observe.c)
 *
 * Tests the observe module's init/cleanup/level management and JSON
 * escape helper. Uses stubs for bash internals and command_hooks API.
 * Also tests that hook callbacks produce correct JSON on CHAN_OBSERVE. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>

/* Include server header for constants and function declarations */
#include "../server.h"

/* ================================================================
 * Stubs for bash internals (not exercised in unit tests)
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
int pty_handle_spawn(int rfd, int wfd, const char *p) { (void)rfd; (void)wfd; (void)p; return 0; }
void debug_init(int rfd, int wfd) { (void)rfd; (void)wfd; }
void debug_cleanup(void) { }
int debug_handle_message(int rfd, int wfd, const char *p) { (void)rfd; (void)wfd; (void)p; return 0; }

/* ================================================================
 * Stubs for command_hooks API
 *
 * We simulate the command_hooks infrastructure so we can verify that
 * observe_init/set_level correctly registers/unregisters hooks.
 * ================================================================ */

#include "../../command_hooks.h"

/* Track registered hooks */
static pre_command_hook_t registered_pre_hooks[MAX_COMMAND_HOOKS];
static post_command_hook_t registered_post_hooks[MAX_COMMAND_HOOKS];
static int num_pre = 0;
static int num_post = 0;

int register_pre_command_hook(pre_command_hook_t hook)
{
    if (!hook || num_pre >= MAX_COMMAND_HOOKS) return -1;
    /* Check duplicate */
    for (int i = 0; i < num_pre; i++)
        if (registered_pre_hooks[i] == hook) return 0;
    registered_pre_hooks[num_pre++] = hook;
    return 0;
}

int unregister_pre_command_hook(pre_command_hook_t hook)
{
    for (int i = 0; i < num_pre; i++) {
        if (registered_pre_hooks[i] == hook) {
            for (int j = i; j < num_pre - 1; j++)
                registered_pre_hooks[j] = registered_pre_hooks[j+1];
            num_pre--;
            return 0;
        }
    }
    return -1;
}

int register_post_command_hook(post_command_hook_t hook)
{
    if (!hook || num_post >= MAX_COMMAND_HOOKS) return -1;
    for (int i = 0; i < num_post; i++)
        if (registered_post_hooks[i] == hook) return 0;
    registered_post_hooks[num_post++] = hook;
    return 0;
}

int unregister_post_command_hook(post_command_hook_t hook)
{
    for (int i = 0; i < num_post; i++) {
        if (registered_post_hooks[i] == hook) {
            for (int j = i; j < num_post - 1; j++)
                registered_post_hooks[j] = registered_post_hooks[j+1];
            num_post--;
            return 0;
        }
    }
    return -1;
}

int pre_command_hooks_count(void) { return num_pre; }
int post_command_hooks_count(void) { return num_post; }

void run_pre_command_hooks(const pre_command_info_t *info)
{
    for (int i = 0; i < num_pre; i++)
        if (registered_pre_hooks[i])
            (*registered_pre_hooks[i])(info);
}

void run_post_command_hooks(const post_command_info_t *info)
{
    for (int i = 0; i < num_post; i++)
        if (registered_post_hooks[i])
            (*registered_post_hooks[i])(info);
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
 * Test: observe_init and observe_cleanup
 * ================================================================ */
static void
test_observe_init_cleanup(void)
{
    printf("test_observe_init_cleanup:\n");

    /* Init at level 0 should not register hooks */
    num_pre = 0; num_post = 0;
    observe_init(STDOUT_FILENO, 0);
    ASSERT(observe_get_level() == 0, "initial level is 0");
    ASSERT(num_pre == 0, "no pre hooks at level 0");
    ASSERT(num_post == 0, "no post hooks at level 0");

    observe_cleanup();
    ASSERT(observe_get_level() == 0, "level 0 after cleanup");
}


/* ================================================================
 * Test: observe_set_level registers/unregisters hooks
 * ================================================================ */
static void
test_observe_set_level(void)
{
    printf("test_observe_set_level:\n");

    num_pre = 0; num_post = 0;
    observe_init(STDOUT_FILENO, 0);

    /* Set level 1 — should register pre+post hooks */
    int actual = observe_set_level(1);
    ASSERT(actual == 1, "set_level returns 1");
    ASSERT(observe_get_level() == 1, "get_level returns 1");
    ASSERT(num_pre == 1, "1 pre hook registered");
    ASSERT(num_post == 1, "1 post hook registered");

    /* Set level 1 again — idempotent */
    actual = observe_set_level(1);
    ASSERT(actual == 1, "set_level(1) idempotent");
    ASSERT(num_pre == 1, "still 1 pre hook");
    ASSERT(num_post == 1, "still 1 post hook");

    /* Set level 0 — unregister hooks */
    actual = observe_set_level(0);
    ASSERT(actual == 0, "set_level returns 0");
    ASSERT(num_pre == 0, "0 pre hooks at level 0");
    ASSERT(num_post == 0, "0 post hooks at level 0");

    /* Set level > max — clamped */
    actual = observe_set_level(99);
    ASSERT(actual == OBSERVE_LEVEL_MAX, "clamped to OBSERVE_LEVEL_MAX");

    /* Set negative — clamped to 0 */
    actual = observe_set_level(-5);
    ASSERT(actual == 0, "negative clamped to 0");

    observe_cleanup();
}


/* ================================================================
 * Test: observe_init with level > 0 auto-registers hooks
 * ================================================================ */
static void
test_observe_init_with_level(void)
{
    printf("test_observe_init_with_level:\n");

    num_pre = 0; num_post = 0;
    observe_init(STDOUT_FILENO, 1);
    ASSERT(observe_get_level() == 1, "level 1 after init(1)");
    ASSERT(num_pre == 1, "pre hook registered by init");
    ASSERT(num_post == 1, "post hook registered by init");

    observe_cleanup();
    ASSERT(num_pre == 0, "hooks unregistered by cleanup");
    ASSERT(num_post == 0, "post hooks unregistered by cleanup");
}


/* ================================================================
 * Test: json_escape_for_observe
 * ================================================================ */
static void
test_json_escape_for_observe(void)
{
    char buf[256];

    printf("test_json_escape_for_observe:\n");

    json_escape_for_observe("hello", buf, sizeof(buf));
    ASSERT(strcmp(buf, "hello") == 0, "plain string unchanged");

    json_escape_for_observe("say \"hi\"", buf, sizeof(buf));
    ASSERT(strcmp(buf, "say \\\"hi\\\"") == 0, "quotes escaped");

    json_escape_for_observe("path\\to\\file", buf, sizeof(buf));
    ASSERT(strcmp(buf, "path\\\\to\\\\file") == 0, "backslashes escaped");

    json_escape_for_observe("line1\nline2", buf, sizeof(buf));
    ASSERT(strcmp(buf, "line1\\nline2") == 0, "newlines escaped");

    json_escape_for_observe("tab\there", buf, sizeof(buf));
    ASSERT(strcmp(buf, "tab\\there") == 0, "tabs escaped");

    json_escape_for_observe("cr\rhere", buf, sizeof(buf));
    ASSERT(strcmp(buf, "cr\\rhere") == 0, "carriage returns escaped");

    /* Empty string */
    json_escape_for_observe("", buf, sizeof(buf));
    ASSERT(buf[0] == '\0', "empty string produces empty output");

    /* Control character */
    json_escape_for_observe("\x01", buf, sizeof(buf));
    ASSERT(strcmp(buf, "\\u0001") == 0, "control char \\x01 escaped as \\u0001");
}


/* ================================================================
 * Test: Hook callbacks produce JSON frames on CHAN_OBSERVE
 *
 * We set up a pipe as the observe fd, trigger hooks via
 * run_pre_command_hooks / run_post_command_hooks, then read
 * the resulting frames.
 * ================================================================ */
static void
test_observe_hook_events(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;
    pre_command_info_t pre_info;
    post_command_info_t post_info;

    printf("test_observe_hook_events:\n");

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    /* Fork so the write end doesn't block */
    {
        pid_t child = fork();
        if (child == 0) {
            close(pipefd[0]);

            /* Initialize observe with the pipe write end at level 1 */
            num_pre = 0; num_post = 0;
            observe_init(pipefd[1], 1);

            /* Simulate a pre-command event */
            pre_info.command_string = "echo hello";
            pre_info.cwd = "/tmp";
            pre_info.line_number = 42;
            pre_info.is_subshell = 0;
            pre_info.is_async = 0;
            run_pre_command_hooks(&pre_info);

            /* Simulate a post-command event */
            post_info.command_string = "echo hello";
            post_info.exit_status = 0;
            post_info.signal_number = 0;
            run_post_command_hooks(&post_info);

            observe_cleanup();
            close(pipefd[1]);
            _exit(0);
        }

        close(pipefd[1]);

        /* Read pre_command frame */
        ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
            "read pre_command frame");
        ASSERT(channel == CHAN_OBSERVE, "pre_command on CHAN_OBSERVE");
        ASSERT(payload != NULL, "pre_command payload not NULL");

        /* Verify pre_command JSON contents */
        {
            char type_buf[64];
            char cmd_buf[256];
            char cwd_buf[256];
            int line_num = -1;
            int level = -1;

            ASSERT(json_get_string(payload, "type", type_buf, sizeof(type_buf)) != NULL,
                "pre_command has type field");
            ASSERT(strcmp(type_buf, "pre_command") == 0, "type is 'pre_command'");

            ASSERT(json_get_int(payload, "level", &level) == 0, "has level field");
            ASSERT(level == 1, "level is 1");

            /* The data fields are nested, but our flat parser can find them
               since keys are unique in the message */
            ASSERT(json_get_string(payload, "command", cmd_buf, sizeof(cmd_buf)) != NULL,
                "has command field");
            ASSERT(strcmp(cmd_buf, "echo hello") == 0, "command is 'echo hello'");

            ASSERT(json_get_string(payload, "cwd", cwd_buf, sizeof(cwd_buf)) != NULL,
                "has cwd field");
            ASSERT(strcmp(cwd_buf, "/tmp") == 0, "cwd is '/tmp'");

            ASSERT(json_get_int(payload, "line_number", &line_num) == 0,
                "has line_number field");
            ASSERT(line_num == 42, "line_number is 42");
        }
        free(payload);

        /* Read post_command frame */
        ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
            "read post_command frame");
        ASSERT(channel == CHAN_OBSERVE, "post_command on CHAN_OBSERVE");

        {
            char type_buf[64];
            int exit_status = -1;

            ASSERT(json_get_string(payload, "type", type_buf, sizeof(type_buf)) != NULL,
                "post_command has type field");
            ASSERT(strcmp(type_buf, "post_command") == 0, "type is 'post_command'");

            ASSERT(json_get_int(payload, "exit_status", &exit_status) == 0,
                "has exit_status field");
            ASSERT(exit_status == 0, "exit_status is 0");
        }
        free(payload);

        /* Should be EOF now */
        ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == -1,
            "EOF after all events");

        close(pipefd[0]);
        waitpid(child, NULL, 0);
    }
}


/* ================================================================
 * Test: Hook with special characters in command
 * ================================================================ */
static void
test_observe_hook_escaping(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;
    pre_command_info_t pre_info;

    printf("test_observe_hook_escaping:\n");

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    {
        pid_t child = fork();
        if (child == 0) {
            close(pipefd[0]);

            num_pre = 0; num_post = 0;
            observe_init(pipefd[1], 1);

            pre_info.command_string = "echo \"hello\\nworld\"";
            pre_info.cwd = "/home/user/my dir";
            pre_info.line_number = 1;
            pre_info.is_subshell = 1;
            pre_info.is_async = 1;

            run_pre_command_hooks(&pre_info);
            observe_cleanup();
            close(pipefd[1]);
            _exit(0);
        }

        close(pipefd[1]);

        ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
            "read escaped pre_command frame");
        ASSERT(channel == CHAN_OBSERVE, "escaped event on CHAN_OBSERVE");

        /* Verify the JSON is valid (has expected fields) */
        {
            char type_buf[64];
            ASSERT(json_get_string(payload, "type", type_buf, sizeof(type_buf)) != NULL,
                "escaped event has type");
            ASSERT(strcmp(type_buf, "pre_command") == 0, "type is pre_command");

            /* Check is_subshell and is_async are true */
            ASSERT(strstr(payload, "\"is_subshell\":true") != NULL,
                "is_subshell is true");
            ASSERT(strstr(payload, "\"is_async\":true") != NULL,
                "is_async is true");
        }
        free(payload);

        close(pipefd[0]);
        waitpid(child, NULL, 0);
    }
}


/* ================================================================
 * Test: Post-command with signal
 * ================================================================ */
static void
test_observe_hook_signal(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;
    post_command_info_t post_info;

    printf("test_observe_hook_signal:\n");

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    {
        pid_t child = fork();
        if (child == 0) {
            close(pipefd[0]);

            num_pre = 0; num_post = 0;
            observe_init(pipefd[1], 1);

            post_info.command_string = "sleep 999";
            post_info.exit_status = 137;
            post_info.signal_number = 9;

            run_post_command_hooks(&post_info);
            observe_cleanup();
            close(pipefd[1]);
            _exit(0);
        }

        close(pipefd[1]);

        ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
            "read signal post_command frame");
        ASSERT(channel == CHAN_OBSERVE, "signal event on CHAN_OBSERVE");

        {
            int exit_status = -1, signal_number = -1;
            ASSERT(json_get_int(payload, "exit_status", &exit_status) == 0,
                "has exit_status");
            ASSERT(exit_status == 137, "exit_status is 137");

            ASSERT(json_get_int(payload, "signal_number", &signal_number) == 0,
                "has signal_number");
            ASSERT(signal_number == 9, "signal_number is 9 (SIGKILL)");
        }
        free(payload);

        close(pipefd[0]);
        waitpid(child, NULL, 0);
    }
}


/* ================================================================
 * Test: Level 0 suppresses hook events
 * ================================================================ */
static void
test_observe_level_zero_suppresses(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;
    pre_command_info_t pre_info;

    printf("test_observe_level_zero_suppresses:\n");

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    {
        pid_t child = fork();
        if (child == 0) {
            close(pipefd[0]);

            num_pre = 0; num_post = 0;
            /* Init at level 1, then drop to 0 */
            observe_init(pipefd[1], 1);
            observe_set_level(0);

            pre_info.command_string = "should not appear";
            pre_info.cwd = "/tmp";
            pre_info.line_number = 1;
            pre_info.is_subshell = 0;
            pre_info.is_async = 0;

            /* Hooks are unregistered at level 0, so run_pre_command_hooks
               won't call our callback. But even if it did, the callback
               checks the level and returns early. */
            run_pre_command_hooks(&pre_info);

            observe_cleanup();
            close(pipefd[1]);
            _exit(0);
        }

        close(pipefd[1]);

        /* Should be EOF — no frames written */
        ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == -1,
            "no events at level 0");

        close(pipefd[0]);
        waitpid(child, NULL, 0);
    }
}


/* ================================================================
 * Test: Sequence numbers increment
 * ================================================================ */
static void
test_observe_sequence_numbers(void)
{
    int pipefd[2];
    int channel, flags;
    char *payload;
    size_t payload_len;
    pre_command_info_t pre_info;
    post_command_info_t post_info;

    printf("test_observe_sequence_numbers:\n");

    ASSERT(pipe(pipefd) == 0, "pipe creation");

    {
        pid_t child = fork();
        if (child == 0) {
            close(pipefd[0]);

            num_pre = 0; num_post = 0;
            observe_init(pipefd[1], 1);

            pre_info.command_string = "cmd1";
            pre_info.cwd = "/tmp";
            pre_info.line_number = 1;
            pre_info.is_subshell = 0;
            pre_info.is_async = 0;
            run_pre_command_hooks(&pre_info);

            post_info.command_string = "cmd1";
            post_info.exit_status = 0;
            post_info.signal_number = 0;
            run_post_command_hooks(&post_info);

            pre_info.command_string = "cmd2";
            run_pre_command_hooks(&pre_info);

            observe_cleanup();
            close(pipefd[1]);
            _exit(0);
        }

        close(pipefd[1]);

        /* Frame 1: seq=0 */
        ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
            "read seq 0 frame");
        {
            int seq = -1;
            json_get_int(payload, "seq", &seq);
            ASSERT(seq == 0, "first event seq is 0");
        }
        free(payload);

        /* Frame 2: seq=1 */
        ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
            "read seq 1 frame");
        {
            int seq = -1;
            json_get_int(payload, "seq", &seq);
            ASSERT(seq == 1, "second event seq is 1");
        }
        free(payload);

        /* Frame 3: seq=2 */
        ASSERT(json_frame_read(pipefd[0], &channel, &flags, &payload, &payload_len) == 0,
            "read seq 2 frame");
        {
            int seq = -1;
            json_get_int(payload, "seq", &seq);
            ASSERT(seq == 2, "third event seq is 2");
        }
        free(payload);

        close(pipefd[0]);
        waitpid(child, NULL, 0);
    }
}


int
main(void)
{
    /* Unbuffered output for timeout compatibility */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Safety timeout */
    alarm(30);

    printf("=== Observability Hooks Unit Tests ===\n\n");

    test_observe_init_cleanup();
    test_observe_set_level();
    test_observe_init_with_level();
    test_json_escape_for_observe();
    test_observe_hook_events();
    test_observe_hook_escaping();
    test_observe_hook_signal();
    test_observe_level_zero_suppresses();
    test_observe_sequence_numbers();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
