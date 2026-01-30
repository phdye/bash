/*
 * bashclient.h - C client for bash-server v2 NDJSON protocol
 *
 * Public API: opaque handle, function-pointer callbacks.
 * Thread-safety: NOT thread-safe. Use one client per thread.
 * Memory: caller frees returned strings via bc_free().
 */

#ifndef BASHCLIENT_H
#define BASHCLIENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque client handle */
typedef struct bc_client bc_client_t;

/* Channel IDs */
#define BC_CHAN_CONTROL  0
#define BC_CHAN_COMMAND  1
#define BC_CHAN_STATE    2
#define BC_CHAN_OBSERVE  3
#define BC_CHAN_DEBUG    4
#define BC_CHAN_PTY      5

/* Error codes */
#define BC_OK            0
#define BC_ERR_AUTH     -1
#define BC_ERR_PROTOCOL -2
#define BC_ERR_TIMEOUT  -3
#define BC_ERR_TRANSPORT -4
#define BC_ERR_SERVER   -5
#define BC_ERR_NOMEM    -6
#define BC_ERR_PARAM    -7

/* Protocol limits */
#define BC_FRAME_MAX_PAYLOAD  (1024 * 1024)
#define BC_TOKEN_HEXLEN       64
#define BC_DEFAULT_TIMEOUT_MS 30000

/* ---------- Connection ---------- */

/* Connect via Unix domain socket. Returns NULL on error (check errno). */
bc_client_t *bc_connect(const char *socket_path);

/* Connect via stdio (subprocess). cmd is executed with popen-like semantics.
   Pass NULL-terminated argv. */
bc_client_t *bc_connect_stdio(const char *const argv[]);

/* Connect via inherited file descriptor. */
bc_client_t *bc_connect_fd(int fd);

/* Connect via Named Pipe (Cygwin). */
bc_client_t *bc_connect_named_pipe(const char *pipe_name);

/* Authenticate. Returns BC_OK or BC_ERR_AUTH. */
int bc_auth(bc_client_t *c, const char *token);

/* Close and free. */
void bc_close(bc_client_t *c);

/* Free a string returned by the API. */
void bc_free(void *ptr);

/* Last error message (static buffer, do not free). */
const char *bc_error(bc_client_t *c);

/* Ping. Returns BC_OK or error. */
int bc_ping(bc_client_t *c);

/* ---------- COMMAND channel ---------- */

typedef struct {
    char *stdout_data;   /* caller frees via bc_free */
    char *stderr_data;   /* caller frees via bc_free */
    int exit_code;
} bc_eval_result_t;

/* Evaluate a command. Caller must bc_eval_result_free() the result. */
int bc_eval(bc_client_t *c, const char *command, bc_eval_result_t *result);

void bc_eval_result_free(bc_eval_result_t *r);

/* ---------- STATE channel ---------- */

typedef struct {
    char *name;       /* bc_free */
    char *value;      /* bc_free */
    char **attributes; /* NULL-terminated array, bc_free each + the array */
    int num_attributes;
} bc_var_info_t;

int bc_state_get_var(bc_client_t *c, const char *name, bc_var_info_t *info);
int bc_state_set_var(bc_client_t *c, const char *name, const char *value,
                     const char *const *attributes, int num_attrs);
int bc_state_unset_var(bc_client_t *c, const char *name);
void bc_var_info_free(bc_var_info_t *info);

int bc_state_get_func(bc_client_t *c, const char *name, char **definition);
int bc_state_unset_func(bc_client_t *c, const char *name);

int bc_state_get_alias(bc_client_t *c, const char *name, char **value);
int bc_state_set_alias(bc_client_t *c, const char *name, const char *value);
int bc_state_unset_alias(bc_client_t *c, const char *name);

int bc_state_set_trap(bc_client_t *c, const char *signal, const char *command);
int bc_state_unset_trap(bc_client_t *c, const char *signal);

/* Inspect returns JSON string. Caller frees. */
int bc_state_inspect(bc_client_t *c, const char *query, char **json_result);

/* ---------- OBSERVE channel ---------- */

typedef struct {
    int seq;
    long long timestamp;
    char *command;
    char *cwd;
    int line_number;
    int is_subshell;
    int is_async;
} bc_pre_command_event_t;

typedef struct {
    int seq;
    long long timestamp;
    char *command;
    int exit_status;
    int signal_number;
    int duration_ms;
} bc_post_command_event_t;

typedef void (*bc_pre_command_cb)(const bc_pre_command_event_t *event, void *userdata);
typedef void (*bc_post_command_cb)(const bc_post_command_event_t *event, void *userdata);

int bc_observe_subscribe(bc_client_t *c, int level);
int bc_observe_unsubscribe(bc_client_t *c);
void bc_observe_on_pre_command(bc_client_t *c, bc_pre_command_cb cb, void *userdata);
void bc_observe_on_post_command(bc_client_t *c, bc_post_command_cb cb, void *userdata);

/* Poll for server-push messages. timeout_ms=0 for non-blocking. Returns number processed. */
int bc_poll(bc_client_t *c, int timeout_ms);

/* ---------- DEBUG channel ---------- */

typedef struct {
    int id;
    char *kind;        /* "command", "line", "function" */
    int enabled;
    int hit_count;
    char *pattern;     /* may be NULL */
    int line;          /* -1 if not line-based */
    char *condition;   /* may be NULL */
} bc_breakpoint_t;

typedef struct {
    int line;
    char *command;
    int depth;
} bc_break_hit_event_t;

typedef void (*bc_break_hit_cb)(const bc_break_hit_event_t *event, void *userdata);

int bc_debug_enable(bc_client_t *c);
int bc_debug_disable(bc_client_t *c);

typedef struct {
    int active;
    char *mode;
    int breakpoints;
    int depth;
} bc_debug_status_t;

int bc_debug_status(bc_client_t *c, bc_debug_status_t *status);
void bc_debug_status_free(bc_debug_status_t *s);

int bc_debug_add_breakpoint(bc_client_t *c, const char *kind,
                            const char *pattern, int line,
                            const char *condition);
int bc_debug_remove_breakpoint(bc_client_t *c, int bp_id);
int bc_debug_list_breakpoints(bc_client_t *c, bc_breakpoint_t **bps, int *count);
void bc_breakpoint_list_free(bc_breakpoint_t *bps, int count);

int bc_debug_continue(bc_client_t *c);
int bc_debug_step(bc_client_t *c);
int bc_debug_next(bc_client_t *c);
int bc_debug_finish(bc_client_t *c);
int bc_debug_skip(bc_client_t *c);

/* Returns JSON string. Caller frees. */
int bc_debug_inspect_ast(bc_client_t *c, char **json_ast);

void bc_debug_on_break_hit(bc_client_t *c, bc_break_hit_cb cb, void *userdata);

/* ---------- PTY channel ---------- */

typedef struct {
    int rows;
    int cols;
    int pid;
    int strip_ansi;
} bc_pty_info_t;

typedef void (*bc_pty_output_cb)(const char *data, size_t len, void *userdata);
typedef void (*bc_pty_exit_cb)(int exit_code, void *userdata);

int bc_pty_spawn(bc_client_t *c, int rows, int cols,
                 const char *shell, int strip_ansi, bc_pty_info_t *info);
int bc_pty_write(bc_client_t *c, const char *data, size_t len);
int bc_pty_resize(bc_client_t *c, int rows, int cols);
int bc_pty_signal(bc_client_t *c, const char *name);
int bc_pty_close(bc_client_t *c);

void bc_pty_on_output(bc_client_t *c, bc_pty_output_cb cb, void *userdata);
void bc_pty_on_exit(bc_client_t *c, bc_pty_exit_cb cb, void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* BASHCLIENT_H */
