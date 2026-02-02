/* test_pty.c -- Unit tests for PTY mode (server_pty.c)
 *
 * Tests PTY spawn, signal parsing, and basic I/O.
 * Uses fork-based isolation with timeouts for blocking PTY operations. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <arpa/inet.h>

/* Include server header for constants and function declarations */
#include "../server.h"

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
void debug_init(int rfd, int wfd) { (void)rfd; (void)wfd; }
void debug_cleanup(void) { }
int debug_handle_message(int rfd, int wfd, const char *p) { (void)rfd; (void)wfd; (void)p; return 0; }

/* Stubs for command_hooks (referenced by server_observe.c if linked) */
#include "../../command_hooks.h"
int register_pre_command_hook(pre_command_hook_t h) { (void)h; return 0; }
int unregister_pre_command_hook(pre_command_hook_t h) { (void)h; return 0; }
int register_post_command_hook(post_command_hook_t h) { (void)h; return 0; }
int unregister_post_command_hook(post_command_hook_t h) { (void)h; return 0; }
int pre_command_hooks_count(void) { return 0; }
int post_command_hooks_count(void) { return 0; }
void run_pre_command_hooks(const pre_command_info_t *i) { (void)i; }
void run_post_command_hooks(const post_command_info_t *i) { (void)i; }

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
 * Test: pty_parse_signal
 * ================================================================ */
static void
test_parse_signal(void)
{
    printf("test_parse_signal:\n");

    ASSERT(pty_parse_signal("SIGINT") == SIGINT, "SIGINT");
    ASSERT(pty_parse_signal("SIGTERM") == SIGTERM, "SIGTERM");
    ASSERT(pty_parse_signal("SIGKILL") == SIGKILL, "SIGKILL");
    ASSERT(pty_parse_signal("SIGTSTP") == SIGTSTP, "SIGTSTP");
    ASSERT(pty_parse_signal("SIGCONT") == SIGCONT, "SIGCONT");
    ASSERT(pty_parse_signal("SIGWINCH") == SIGWINCH, "SIGWINCH");
    ASSERT(pty_parse_signal("SIGHUP") == SIGHUP, "SIGHUP");
    ASSERT(pty_parse_signal("SIGQUIT") == SIGQUIT, "SIGQUIT");
    ASSERT(pty_parse_signal("SIGUSR1") == SIGUSR1, "SIGUSR1");
    ASSERT(pty_parse_signal("SIGUSR2") == SIGUSR2, "SIGUSR2");

    /* Without SIG prefix */
    ASSERT(pty_parse_signal("INT") == SIGINT, "INT (no SIG prefix)");
    ASSERT(pty_parse_signal("TERM") == SIGTERM, "TERM (no SIG prefix)");
    ASSERT(pty_parse_signal("KILL") == SIGKILL, "KILL (no SIG prefix)");

    /* Numeric */
    ASSERT(pty_parse_signal("2") == 2, "numeric 2");
    ASSERT(pty_parse_signal("9") == 9, "numeric 9");
    ASSERT(pty_parse_signal("15") == 15, "numeric 15");

    /* Unknown */
    ASSERT(pty_parse_signal("BOGUS") == -1, "unknown signal returns -1");
    ASSERT(pty_parse_signal("SIGBOGUS") == -1, "SIGBOGUS returns -1");
}


/* ================================================================
 * Test: PTY spawn and basic I/O
 *
 * Spawns a PTY with /bin/sh (more predictable than bash for tests),
 * sends a simple command, reads output, then exits.
 * Uses fork + timeout for safety.
 * ================================================================ */
static void
test_pty_spawn_io(void)
{
    pid_t test_pid;
    int status;

    printf("test_pty_spawn_io:\n");

    test_pid = fork();
    if (test_pid == 0) {
        /* Child: run the actual PTY test with alarm timeout */
        int sv[2];  /* socketpair for client<->server simulation */
        int channel, flags;
        char *payload;
        size_t payload_len;
        int got_spawn_ok = 0;
        int got_output = 0;

        alarm(10);  /* Safety timeout */

        /* Create socketpair to simulate client connection */
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
            _exit(1);

        /* Fork again: one side sends spawn + input, other runs pty_handle_spawn */
        {
            pid_t server_pid = fork();
            if (server_pid == 0) {
                /* "Server" side: run pty_handle_spawn */
                close(sv[0]);
                pty_handle_spawn(sv[1], sv[1],
                    "{\"type\":\"spawn\",\"rows\":24,\"cols\":80,\"shell\":\"/bin/sh\"}");
                close(sv[1]);
                _exit(0);
            }

            /* "Client" side: read spawn_ok, send a command, read output, close */
            close(sv[1]);

            /* Read spawn_ok */
            if (json_frame_read(sv[0], &channel, &flags, &payload, &payload_len) == 0) {
                if (channel == CHAN_PTY && strstr(payload, "\"spawn_ok\""))
                    got_spawn_ok = 1;
                free(payload);
            }

            if (got_spawn_ok) {
                /* Send "exit\n" command as base64 input */
                char *exit_b64 = protocol_base64_encode("exit\n", 5);
                if (exit_b64) {
                    char msg[512];
                    snprintf(msg, sizeof(msg),
                        "{\"type\":\"input\",\"data\":\"%s\",\"encoding\":\"base64\"}",
                        exit_b64);
                    json_frame_write(sv[0], CHAN_PTY, 0, msg, strlen(msg));
                    free(exit_b64);
                }

                /* Read output frames until exit or timeout */
                while (json_frame_read(sv[0], &channel, &flags, &payload, &payload_len) == 0) {
                    if (channel == CHAN_PTY) {
                        if (strstr(payload, "\"output\""))
                            got_output = 1;
                        if (strstr(payload, "\"exit\"")) {
                            free(payload);
                            break;
                        }
                    }
                    free(payload);
                }
            }

            close(sv[0]);
            waitpid(server_pid, NULL, 0);
        }

        _exit((got_spawn_ok && got_output) ? 0 : 1);
    }

    /* Parent: wait for test child with timeout */
    {
        int i;
        int child_done = 0;
        for (i = 0; i < 120; i++) {  /* 12 seconds max */
            if (waitpid(test_pid, &status, WNOHANG) == test_pid) {
                child_done = 1;
                break;
            }
            usleep(100000);
        }

        if (!child_done) {
            kill(test_pid, SIGKILL);
            waitpid(test_pid, &status, 0);
            ASSERT(0, "PTY spawn+IO test timed out");
        } else {
            ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "PTY spawn + I/O round-trip succeeded");
        }
    }
}


/* ================================================================
 * Test: PTY handle_spawn with bad shell
 * ================================================================ */
static void
test_pty_bad_shell(void)
{
    pid_t test_pid;
    int status;

    printf("test_pty_bad_shell:\n");

    test_pid = fork();
    if (test_pid == 0) {
        int sv[2];
        int channel, flags;
        char *payload;
        size_t payload_len;
        int got_exit = 0;

        alarm(10);

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
            _exit(1);

        {
            pid_t server_pid = fork();
            if (server_pid == 0) {
                close(sv[0]);
                pty_handle_spawn(sv[1], sv[1],
                    "{\"type\":\"spawn\",\"rows\":24,\"cols\":80,"
                    "\"shell\":\"/nonexistent/shell\"}");
                close(sv[1]);
                _exit(0);
            }

            close(sv[1]);

            /* Read frames — should get spawn_ok then eventually exit with 127 */
            while (json_frame_read(sv[0], &channel, &flags, &payload, &payload_len) == 0) {
                if (channel == CHAN_PTY && strstr(payload, "\"exit\"")) {
                    /* Check exit code is 127 (exec failed) */
                    int exit_code = -1;
                    json_get_int(payload, "exit_code", &exit_code);
                    if (exit_code == 127)
                        got_exit = 1;
                    free(payload);
                    break;
                }
                free(payload);
            }

            close(sv[0]);
            waitpid(server_pid, NULL, 0);
        }

        _exit(got_exit ? 0 : 1);
    }

    {
        int i;
        int child_done = 0;
        for (i = 0; i < 120; i++) {
            if (waitpid(test_pid, &status, WNOHANG) == test_pid) {
                child_done = 1;
                break;
            }
            usleep(100000);
        }

        if (!child_done) {
            kill(test_pid, SIGKILL);
            waitpid(test_pid, &status, 0);
            ASSERT(0, "bad shell test timed out");
        } else {
            ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "bad shell returns exit code 127");
        }
    }
}


/* ================================================================
 * Test: PTY resize via JSON frame
 * ================================================================ */
static void
test_pty_resize(void)
{
    pid_t test_pid;
    int status;

    printf("test_pty_resize:\n");

    test_pid = fork();
    if (test_pid == 0) {
        int sv[2];
        int channel, flags;
        char *payload;
        size_t payload_len;
        int got_resize_ok = 0;

        alarm(10);

        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
            _exit(1);

        {
            pid_t server_pid = fork();
            if (server_pid == 0) {
                close(sv[0]);
                pty_handle_spawn(sv[1], sv[1],
                    "{\"type\":\"spawn\",\"rows\":24,\"cols\":80,\"shell\":\"/bin/sh\"}");
                close(sv[1]);
                _exit(0);
            }

            close(sv[1]);

            /* Read spawn_ok */
            if (json_frame_read(sv[0], &channel, &flags, &payload, &payload_len) == 0) {
                free(payload);

                /* Send resize */
                {
                    const char *resize_msg =
                        "{\"type\":\"resize\",\"rows\":50,\"cols\":120}";
                    json_frame_write(sv[0], CHAN_PTY, 0, resize_msg, strlen(resize_msg));
                }

                /* Read frames until we get resize_ok */
                while (json_frame_read(sv[0], &channel, &flags, &payload, &payload_len) == 0) {
                    if (channel == CHAN_PTY && strstr(payload, "\"resize_ok\"")) {
                        if (strstr(payload, "\"rows\":50") && strstr(payload, "\"cols\":120"))
                            got_resize_ok = 1;
                        free(payload);
                        break;
                    }
                    free(payload);
                }

                /* Send close */
                {
                    const char *close_msg = "{\"type\":\"close\"}";
                    json_frame_write(sv[0], CHAN_PTY, 0, close_msg, strlen(close_msg));
                }

                /* Drain remaining */
                while (json_frame_read(sv[0], &channel, &flags, &payload, &payload_len) == 0)
                    free(payload);
            }

            close(sv[0]);
            waitpid(server_pid, NULL, 0);
        }

        _exit(got_resize_ok ? 0 : 1);
    }

    {
        int i;
        int child_done = 0;
        for (i = 0; i < 120; i++) {
            if (waitpid(test_pid, &status, WNOHANG) == test_pid) {
                child_done = 1;
                break;
            }
            usleep(100000);
        }

        if (!child_done) {
            kill(test_pid, SIGKILL);
            waitpid(test_pid, &status, 0);
            ASSERT(0, "resize test timed out");
        } else {
            ASSERT(WIFEXITED(status) && WEXITSTATUS(status) == 0,
                "resize 50x120 acknowledged");
        }
    }
}


/* ================================================================
 * ANSI stripping tests
 *
 * ansi_strip() and its types are defined in server_pty.c (compiled
 * alongside this file).  Declare them here for test access.
 * ================================================================ */

enum {
    STRIP_NORMAL = 0,
    STRIP_ESC,
    STRIP_CSI,
    STRIP_OSC,
    STRIP_OSC_ESC,
    STRIP_CHARSET
};

typedef struct ansi_strip_state {
    int state;
    int active;
} ansi_strip_state_t;

extern size_t ansi_strip(ansi_strip_state_t *st, const char *in, size_t in_len,
                         char *out, size_t out_size);


/* Test: plain text passthrough — no escapes, output == input */
static void
test_ansi_strip_plain(void)
{
    ansi_strip_state_t st = { STRIP_NORMAL, 1 };
    char out[256];
    size_t n;

    printf("test_ansi_strip_plain:\n");

    n = ansi_strip(&st, "hello world", 11, out, sizeof(out));
    out[n] = '\0';
    ASSERT(n == 11, "plain text length preserved");
    ASSERT(strcmp(out, "hello world") == 0, "plain text content preserved");
    ASSERT(st.state == STRIP_NORMAL, "state is NORMAL after plain text");
}


/* Test: SGR (color) stripping — ESC[31m hello ESC[0m → hello */
static void
test_ansi_strip_sgr(void)
{
    ansi_strip_state_t st = { STRIP_NORMAL, 1 };
    char out[256];
    size_t n;

    printf("test_ansi_strip_sgr:\n");

    /* ESC[31m = red, ESC[0m = reset */
    const char input[] = "\x1b[31mhello\x1b[0m";
    n = ansi_strip(&st, input, sizeof(input) - 1, out, sizeof(out));
    out[n] = '\0';
    ASSERT(n == 5, "SGR stripped, only 'hello' remains");
    ASSERT(strcmp(out, "hello") == 0, "SGR content is 'hello'");
}


/* Test: cursor movement stripping — ESC[2J ESC[H → empty */
static void
test_ansi_strip_cursor(void)
{
    ansi_strip_state_t st = { STRIP_NORMAL, 1 };
    char out[256];
    size_t n;

    printf("test_ansi_strip_cursor:\n");

    /* ESC[2J = clear screen, ESC[H = cursor home */
    const char input[] = "\x1b[2J\x1b[H";
    n = ansi_strip(&st, input, sizeof(input) - 1, out, sizeof(out));
    ASSERT(n == 0, "cursor-only input produces empty output");
}


/* Test: OSC title with BEL terminator */
static void
test_ansi_strip_osc_bel(void)
{
    ansi_strip_state_t st = { STRIP_NORMAL, 1 };
    char out[256];
    size_t n;

    printf("test_ansi_strip_osc_bel:\n");

    /* ESC]0;my title BEL */
    const char input[] = "\x1b]0;my title\x07";
    n = ansi_strip(&st, input, sizeof(input) - 1, out, sizeof(out));
    ASSERT(n == 0, "OSC+BEL produces empty output");
    ASSERT(st.state == STRIP_NORMAL, "state is NORMAL after OSC+BEL");
}


/* Test: OSC title with ST (ESC \) terminator */
static void
test_ansi_strip_osc_st(void)
{
    ansi_strip_state_t st = { STRIP_NORMAL, 1 };
    char out[256];
    size_t n;

    printf("test_ansi_strip_osc_st:\n");

    /* ESC]0;my title ESC\ */
    const char input[] = "\x1b]0;my title\x1b\\";
    n = ansi_strip(&st, input, sizeof(input) - 1, out, sizeof(out));
    ASSERT(n == 0, "OSC+ST produces empty output");
    ASSERT(st.state == STRIP_NORMAL, "state is NORMAL after OSC+ST");
}


/* Test: mixed content — text + escapes interleaved */
static void
test_ansi_strip_mixed(void)
{
    ansi_strip_state_t st = { STRIP_NORMAL, 1 };
    char out[256];
    size_t n;

    printf("test_ansi_strip_mixed:\n");

    /* "normal" + ESC[1m + "bold" + ESC[0m + " more" */
    const char input[] = "normal\x1b[1mbold\x1b[0m more";
    n = ansi_strip(&st, input, sizeof(input) - 1, out, sizeof(out));
    out[n] = '\0';
    ASSERT(n == 15, "mixed: 'normal' + 'bold' + ' more' = 15");
    ASSERT(strcmp(out, "normalbold more") == 0, "mixed content correct");
}


/* Test: split sequence across two calls */
static void
test_ansi_strip_split(void)
{
    ansi_strip_state_t st = { STRIP_NORMAL, 1 };
    char out[256];
    size_t n1, n2;

    printf("test_ansi_strip_split:\n");

    /* First call ends mid-CSI: "AB" + ESC + "[" */
    const char part1[] = "AB\x1b[";
    n1 = ansi_strip(&st, part1, sizeof(part1) - 1, out, sizeof(out));

    ASSERT(n1 == 2, "split part1: 'AB' emitted");
    ASSERT(st.state == STRIP_CSI, "state is CSI after part1");

    /* Second call completes CSI: "31m" + "CD" */
    const char part2[] = "31mCD";
    n2 = ansi_strip(&st, part2, sizeof(part2) - 1, out + n1, sizeof(out) - n1);

    ASSERT(n2 == 2, "split part2: 'CD' emitted");
    out[n1 + n2] = '\0';
    ASSERT(strcmp(out, "ABCD") == 0, "split result is 'ABCD'");
    ASSERT(st.state == STRIP_NORMAL, "state is NORMAL after split");
}


/* Test: 8-bit CSI (0x9B) */
static void
test_ansi_strip_8bit_csi(void)
{
    ansi_strip_state_t st = { STRIP_NORMAL, 1 };
    char out[256];
    size_t n;

    printf("test_ansi_strip_8bit_csi:\n");

    /* 0x9B 31 6D = 8-bit CSI "31m" (red) */
    const char input[] = "\x9b" "31m";
    n = ansi_strip(&st, input, 3, out, sizeof(out));
    ASSERT(n == 0, "8-bit CSI produces empty output");
}


/* Test: charset designator — ESC(B */
static void
test_ansi_strip_charset(void)
{
    ansi_strip_state_t st = { STRIP_NORMAL, 1 };
    char out[256];
    size_t n;

    printf("test_ansi_strip_charset:\n");

    /* ESC(B = designate ASCII charset */
    const char input[] = "\x1b(B";
    n = ansi_strip(&st, input, sizeof(input) - 1, out, sizeof(out));
    ASSERT(n == 0, "charset designator produces empty output");
    ASSERT(st.state == STRIP_NORMAL, "state is NORMAL after charset");

    /* ESC)0 = designate DEC Special Graphics */
    st.state = STRIP_NORMAL;
    const char input2[] = "\x1b)0";
    n = ansi_strip(&st, input2, sizeof(input2) - 1, out, sizeof(out));
    ASSERT(n == 0, "ESC)0 charset produces empty output");
}


/* Test: two-character escape — ESC M (reverse index) */
static void
test_ansi_strip_two_char(void)
{
    ansi_strip_state_t st = { STRIP_NORMAL, 1 };
    char out[256];
    size_t n;

    printf("test_ansi_strip_two_char:\n");

    /* ESC M = reverse index, ESC 7 = save cursor, ESC 8 = restore */
    const char input[] = "\x1bM\x1b" "7\x1b" "8";
    n = ansi_strip(&st, input, 6, out, sizeof(out));
    ASSERT(n == 0, "two-char escapes produce empty output");
    ASSERT(st.state == STRIP_NORMAL, "state is NORMAL after two-char escapes");
}


/* Test: large buffer with mixed content — verify no overrun */
static void
test_ansi_strip_large(void)
{
    ansi_strip_state_t st = { STRIP_NORMAL, 1 };
    size_t data_len = 65536;
    char *in, *out;
    size_t n, i;

    printf("test_ansi_strip_large:\n");

    in = malloc(data_len);
    out = malloc(data_len);
    ASSERT(in != NULL && out != NULL, "malloc large buffers");

    /* Fill with pattern: "AAAA" + ESC[31m + "BBBB" + ESC[0m repeated */
    {
        size_t pos = 0;
        while (pos + 16 < data_len) {
            memcpy(in + pos, "AAAA\x1b[31mBBBB\x1b[0m", 17);
            pos += 17;
        }
        /* Fill remainder with plain text */
        while (pos < data_len)
            in[pos++] = 'X';
    }

    n = ansi_strip(&st, in, data_len, out, data_len);

    ASSERT(n > 0, "large buffer produces output");
    ASSERT(n < data_len, "output smaller than input (escapes stripped)");

    /* Verify no escape bytes in output */
    {
        int found_esc = 0;
        for (i = 0; i < n; i++) {
            if ((unsigned char)out[i] == 0x1B) {
                found_esc = 1;
                break;
            }
        }
        ASSERT(!found_esc, "no ESC bytes in stripped output");
    }

    free(in);
    free(out);
}


int
main(void)
{
    /* Unbuffered output for timeout compatibility */
    setvbuf(stdout, NULL, _IONBF, 0);

    /* Safety timeout */
    alarm(30);

    printf("=== PTY Mode Unit Tests ===\n\n");

    test_parse_signal();
    test_pty_spawn_io();
    test_pty_bad_shell();
    test_pty_resize();

    /* ANSI stripping tests */
    test_ansi_strip_plain();
    test_ansi_strip_sgr();
    test_ansi_strip_cursor();
    test_ansi_strip_osc_bel();
    test_ansi_strip_osc_st();
    test_ansi_strip_mixed();
    test_ansi_strip_split();
    test_ansi_strip_8bit_csi();
    test_ansi_strip_charset();
    test_ansi_strip_two_char();
    test_ansi_strip_large();

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
