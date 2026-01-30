# BASH-SERVER-CLIENT-C(7) -- C client binding for bash-server

# DESCRIPTION

The C client binding provides a synchronous, blocking interface to the
bash-server v2 NDJSON protocol.  It is implemented as `libbashclient`,
a C99 library with POSIX dependencies only (no external libraries).
The public API is declared in a single header, `bashclient.h`, and the
library is available in both static (`libbashclient.a`) and shared
(`libbashclient.so`) forms.

Unlike the Python, TypeScript, and Java bindings, the C binding is
fully synchronous.  There is no background thread or event loop.
Server-push messages (observe events, debug break_hit, PTY output)
are delivered through registered function-pointer callbacks when the
caller invokes `bc_poll()`.

# CONCEPTS

# Installation

The library is located at `clients/c/` and uses a plain Makefile:

```sh
cd clients/c
make
```

This produces:

- `libbashclient.a` -- Static library
- `libbashclient.so` -- Shared library
- `include/bashclient.h` -- Public header

To link against the library:

```sh
# Static
cc -o myprogram myprogram.c -Iclients/c/include -Lclients/c -lbashclient

# Shared
cc -o myprogram myprogram.c -Iclients/c/include -Lclients/c -lbashclient
LD_LIBRARY_PATH=clients/c ./myprogram
```

Tests are built and run with:

```sh
make test
```

# Source structure

```
include/
    bashclient.h     -- Public API (opaque handle, all declarations)
src/
    bashclient.c     -- Client logic, channel dispatch, polling
    internal.h       -- Internal structures (bc_client internals)
    protocol.c       -- NDJSON encode/decode, base64
    transport.c      -- Unix socket, stdio, fd, Named Pipe transports
```

# Architecture

The central type is `bc_client_t`, an opaque handle that encapsulates
the transport connection, internal message buffers, callback
registrations, and per-channel response state.

All operations are synchronous and blocking.  When a function like
`bc_eval()` is called, it:

1. Encodes the request as an NDJSON line and writes it to the transport.
2. Reads lines from the transport until it receives the expected
   response message(s) for the target channel.
3. While reading, any server-push messages encountered are buffered
   internally.
4. Returns the result through an output parameter.

Server-push messages (observe events, debug break_hit, PTY output/exit)
accumulate in an internal buffer.  The caller must periodically call
`bc_poll()` to dispatch them to registered callbacks.  Alternatively,
push messages that arrive while waiting for a request/response are
dispatched immediately if callbacks are registered.

# Connection

Four functions create a `bc_client_t` handle:

```c
/* Unix domain socket (default) */
bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock");

/* Subprocess (--stdio mode) */
const char *argv[] = {"bash-server", "--stdio", NULL};
bc_client_t *c = bc_connect_stdio(argv);

/* Inherited file descriptor */
bc_client_t *c = bc_connect_fd(3);

/* Windows Named Pipe (Cygwin) */
bc_client_t *c = bc_connect_named_pipe("\\\\.\\pipe\\bash-server-myname");
```

All connect functions return `NULL` on error; check `errno` for the
failure reason.

# Lifecycle

```c
bc_client_t *c = bc_connect(socket_path);
if (!c) {
    perror("bc_connect");
    exit(1);
}

int rc = bc_auth(c, token);
if (rc != BC_OK) {
    fprintf(stderr, "auth failed: %s\n", bc_error(c));
    bc_close(c);
    exit(1);
}

/* ... use client ... */

bc_close(c);  /* sends disconnect, frees all resources */
```

`bc_close()` sends a `disconnect` message, closes the transport, and
frees all internal state including the `bc_client_t` handle itself.
After `bc_close()`, the pointer is invalid.

# Error codes

All functions that can fail return an `int` error code:

| Code              | Value | Meaning                         |
|-------------------|-------|---------------------------------|
| `BC_OK`           |  0    | Success                         |
| `BC_ERR_AUTH`     | -1    | Authentication failed           |
| `BC_ERR_PROTOCOL` | -2    | Malformed frame or message      |
| `BC_ERR_TIMEOUT`  | -3    | Operation timed out             |
| `BC_ERR_TRANSPORT`| -4    | Socket or I/O error             |
| `BC_ERR_SERVER`   | -5    | Server returned an error        |
| `BC_ERR_NOMEM`    | -6    | Memory allocation failed        |
| `BC_ERR_PARAM`    | -7    | Invalid parameter               |

The last error message is available via `bc_error()`:

```c
const char *msg = bc_error(c);  /* static buffer, do not free */
```

# Memory management

The C binding follows a consistent memory ownership convention:

- **Strings returned by the API** (e.g., in result structs) are
  allocated by the library and must be freed by the caller via
  `bc_free()`.

- **Struct contents** are freed by type-specific free functions
  (`bc_eval_result_free()`, `bc_var_info_free()`, etc.).

- **Array results** (e.g., breakpoint lists) are freed with their
  type-specific free function plus a count parameter.

```c
/* String in struct -- freed by type-specific function */
bc_eval_result_t result;
bc_eval(c, "echo hello", &result);
printf("%s", result.stdout_data);
bc_eval_result_free(&result);

/* String returned directly -- freed by bc_free */
char *json;
bc_state_inspect(c, "variables", &json);
printf("%s\n", json);
bc_free(json);

/* Array -- freed with count */
bc_breakpoint_t *bps;
int count;
bc_debug_list_breakpoints(c, &bps, &count);
bc_breakpoint_list_free(bps, count);
```

**Never use `free()` directly** on pointers returned by the library.
Always use `bc_free()` or the appropriate type-specific free function.

# Thread safety

The C binding is **NOT thread-safe**.  Use one `bc_client_t` per
thread.  Do not share a client handle between threads.  Do not call
any `bc_*` function from a signal handler.

# CONTROL channel

```c
/* Authenticate */
int rc = bc_auth(c, token);

/* Ping */
rc = bc_ping(c);
```

# COMMAND channel

```c
bc_eval_result_t result;
int rc = bc_eval(c, "echo hello", &result);
if (rc == BC_OK) {
    printf("stdout: %s\n", result.stdout_data);
    printf("stderr: %s\n", result.stderr_data);
    printf("exit:   %d\n", result.exit_code);
    bc_eval_result_free(&result);
}
```

The `bc_eval_result_t` struct contains:

```c
typedef struct {
    char *stdout_data;   /* caller frees via bc_free */
    char *stderr_data;   /* caller frees via bc_free */
    int exit_code;
} bc_eval_result_t;
```

# STATE channel

**Variables:**

```c
bc_var_info_t info;
int rc = bc_state_get_var(c, "HOME", &info);
if (rc == BC_OK) {
    printf("name: %s\n", info.name);
    printf("value: %s\n", info.value);
    for (int i = 0; i < info.num_attributes; i++)
        printf("attr: %s\n", info.attributes[i]);
    bc_var_info_free(&info);
}

/* Set with attributes */
const char *attrs[] = {"-x"};
bc_state_set_var(c, "MY_VAR", "hello", attrs, 1);

/* Set without attributes */
bc_state_set_var(c, "MY_VAR", "hello", NULL, 0);

/* Unset */
bc_state_unset_var(c, "MY_VAR");
```

The `bc_var_info_t` struct:

```c
typedef struct {
    char *name;         /* bc_free */
    char *value;        /* bc_free */
    char **attributes;  /* NULL-terminated, bc_free each + array */
    int num_attributes;
} bc_var_info_t;
```

**Functions:**

```c
char *definition;
bc_state_get_func(c, "my_func", &definition);
printf("%s\n", definition);
bc_free(definition);

bc_state_unset_func(c, "my_func");
```

**Aliases:**

```c
char *value;
bc_state_get_alias(c, "ll", &value);
printf("ll=%s\n", value);
bc_free(value);

bc_state_set_alias(c, "ll", "ls -la --color");
bc_state_unset_alias(c, "ll");
```

**Traps:**

```c
bc_state_set_trap(c, "SIGINT", "echo interrupted");
bc_state_unset_trap(c, "SIGINT");
```

**Inspect:**

```c
char *json;
bc_state_inspect(c, "variables", &json);
printf("%s\n", json);
bc_free(json);
```

# OBSERVE channel

```c
/* Subscribe */
bc_observe_subscribe(c, 1);

/* Register callbacks */
void on_pre(const bc_pre_command_event_t *event, void *userdata) {
    printf("[%d] running: %s in %s\n",
           event->seq, event->command, event->cwd);
}

void on_post(const bc_post_command_event_t *event, void *userdata) {
    printf("[%d] done: %s exit=%d duration=%dms\n",
           event->seq, event->command,
           event->exit_status, event->duration_ms);
}

bc_observe_on_pre_command(c, on_pre, NULL);
bc_observe_on_post_command(c, on_post, NULL);

/* Poll for events (non-blocking) */
int processed = bc_poll(c, 0);

/* Poll with timeout (blocks up to 1000ms) */
processed = bc_poll(c, 1000);

/* Unsubscribe */
bc_observe_unsubscribe(c);
```

The event structs:

```c
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
```

Event data (strings) within callback arguments is valid only for the
duration of the callback.  Copy any data you need to retain.

# DEBUG channel

```c
/* Enable/disable */
bc_debug_enable(c);
bc_debug_disable(c);

/* Status */
bc_debug_status_t status;
bc_debug_status(c, &status);
printf("active=%d mode=%s bps=%d\n",
       status.active, status.mode, status.breakpoints);
bc_debug_status_free(&status);

/* Add breakpoints */
int bp_id = bc_debug_add_breakpoint(c, "command", "echo*", -1, NULL);
int bp_id2 = bc_debug_add_breakpoint(c, "line", NULL, 10, NULL);
int bp_id3 = bc_debug_add_breakpoint(c, "function", "my_func", -1, NULL);

/* List breakpoints */
bc_breakpoint_t *bps;
int count;
bc_debug_list_breakpoints(c, &bps, &count);
for (int i = 0; i < count; i++) {
    printf("#%d %s enabled=%d hits=%d\n",
           bps[i].id, bps[i].kind,
           bps[i].enabled, bps[i].hit_count);
}
bc_breakpoint_list_free(bps, count);

/* Break hit callback */
void on_break(const bc_break_hit_event_t *event, void *userdata) {
    printf("break at line %d: %s (depth %d)\n",
           event->line, event->command, event->depth);
}
bc_debug_on_break_hit(c, on_break, NULL);

/* Execution control */
bc_debug_continue(c);
bc_debug_step(c);
bc_debug_next(c);
bc_debug_finish(c);
bc_debug_skip(c);

/* Inspect AST */
char *json_ast;
bc_debug_inspect_ast(c, &json_ast);
printf("%s\n", json_ast);
bc_free(json_ast);

/* Remove breakpoint */
bc_debug_remove_breakpoint(c, bp_id);
```

The breakpoint struct:

```c
typedef struct {
    int id;
    char *kind;        /* "command", "line", "function" */
    int enabled;
    int hit_count;
    char *pattern;     /* may be NULL */
    int line;          /* -1 if not line-based */
    char *condition;   /* may be NULL */
} bc_breakpoint_t;
```

# PTY channel

```c
/* Spawn */
bc_pty_info_t info;
bc_pty_spawn(c, 24, 80, "/bin/bash", 0, &info);
printf("pid=%d %dx%d\n", info.pid, info.rows, info.cols);

/* Output callback */
void on_output(const char *data, size_t len, void *userdata) {
    fwrite(data, 1, len, stdout);
}

void on_exit(int exit_code, void *userdata) {
    printf("\nPTY exited: %d\n", exit_code);
}

bc_pty_on_output(c, on_output, NULL);
bc_pty_on_exit(c, on_exit, NULL);

/* Write input */
bc_pty_write(c, "ls -la\n", 7);

/* Resize */
bc_pty_resize(c, 48, 120);

/* Signal */
bc_pty_signal(c, "SIGINT");

/* Close */
bc_pty_close(c);
```

The PTY info struct:

```c
typedef struct {
    int rows;
    int cols;
    int pid;
    int strip_ansi;
} bc_pty_info_t;
```

# Polling

Since the C binding has no background thread, server-push messages
must be processed explicitly via `bc_poll()`:

```c
/* Non-blocking poll -- returns immediately */
int n = bc_poll(c, 0);
printf("processed %d push messages\n", n);

/* Blocking poll -- waits up to 1000ms */
n = bc_poll(c, 1000);

/* Typical poll loop */
while (running) {
    int n = bc_poll(c, 100);  /* 100ms timeout */
    /* do other work ... */
}
```

`bc_poll()` reads available data from the transport, decodes any
complete NDJSON lines, and dispatches server-push messages to
registered callbacks.  It returns the number of messages processed.

A timeout of 0 performs a non-blocking check.  A positive timeout
blocks up to that many milliseconds waiting for data.

# Callback model

Callbacks are registered as C function pointers with an optional
`void *userdata` parameter:

```c
/* Observe callbacks */
typedef void (*bc_pre_command_cb)(const bc_pre_command_event_t *event,
                                  void *userdata);
typedef void (*bc_post_command_cb)(const bc_post_command_event_t *event,
                                   void *userdata);

/* Debug callback */
typedef void (*bc_break_hit_cb)(const bc_break_hit_event_t *event,
                                 void *userdata);

/* PTY callbacks */
typedef void (*bc_pty_output_cb)(const char *data, size_t len,
                                  void *userdata);
typedef void (*bc_pty_exit_cb)(int exit_code, void *userdata);
```

Each callback registration function takes the client handle, the
function pointer, and the userdata pointer:

```c
bc_observe_on_pre_command(c, my_callback, my_context);
bc_debug_on_break_hit(c, my_break_handler, my_context);
bc_pty_on_output(c, my_output_handler, my_context);
```

Passing `NULL` as the callback function unregisters the callback.
Only one callback per event type can be active at a time (unlike the
Python/TypeScript/Java bindings which support multiple callbacks).

# Protocol limits

```c
#define BC_FRAME_MAX_PAYLOAD  (1024 * 1024)  /* 1 MB */
#define BC_TOKEN_HEXLEN       64
#define BC_DEFAULT_TIMEOUT_MS 30000          /* 30 seconds */
```

# Complete function reference

**Connection:**

| Function                | Description                     |
|-------------------------|---------------------------------|
| `bc_connect()`          | Connect via Unix socket         |
| `bc_connect_stdio()`    | Connect via subprocess          |
| `bc_connect_fd()`       | Connect via file descriptor     |
| `bc_connect_named_pipe()` | Connect via Named Pipe        |
| `bc_auth()`             | Authenticate                    |
| `bc_close()`            | Close and free                  |
| `bc_free()`             | Free API-allocated memory       |
| `bc_error()`            | Last error message              |
| `bc_ping()`             | Keepalive ping                  |
| `bc_poll()`             | Poll for push messages          |

**Command:**

| Function                | Description                     |
|-------------------------|---------------------------------|
| `bc_eval()`             | Evaluate command                |
| `bc_eval_result_free()` | Free eval result                |

**State:**

| Function                | Description                     |
|-------------------------|---------------------------------|
| `bc_state_get_var()`    | Get variable                    |
| `bc_state_set_var()`    | Set variable                    |
| `bc_state_unset_var()`  | Unset variable                  |
| `bc_var_info_free()`    | Free variable info              |
| `bc_state_get_func()`   | Get function definition         |
| `bc_state_unset_func()` | Unset function                  |
| `bc_state_get_alias()`  | Get alias value                 |
| `bc_state_set_alias()`  | Set alias                       |
| `bc_state_unset_alias()`| Unset alias                     |
| `bc_state_set_trap()`   | Set signal trap                 |
| `bc_state_unset_trap()` | Unset signal trap               |
| `bc_state_inspect()`    | Inspect namespace (returns JSON)|

**Observe:**

| Function                      | Description                   |
|-------------------------------|-------------------------------|
| `bc_observe_subscribe()`      | Subscribe to events           |
| `bc_observe_unsubscribe()`    | Unsubscribe                   |
| `bc_observe_on_pre_command()` | Register pre-command callback |
| `bc_observe_on_post_command()`| Register post-command callback|

**Debug:**

| Function                      | Description                   |
|-------------------------------|-------------------------------|
| `bc_debug_enable()`           | Enable debugger               |
| `bc_debug_disable()`          | Disable debugger              |
| `bc_debug_status()`           | Get debugger status           |
| `bc_debug_status_free()`      | Free status struct            |
| `bc_debug_add_breakpoint()`   | Add breakpoint                |
| `bc_debug_remove_breakpoint()`| Remove breakpoint             |
| `bc_debug_list_breakpoints()` | List all breakpoints          |
| `bc_breakpoint_list_free()`   | Free breakpoint array         |
| `bc_debug_continue()`         | Continue execution            |
| `bc_debug_step()`             | Step into                     |
| `bc_debug_next()`             | Step over                     |
| `bc_debug_finish()`           | Step out                      |
| `bc_debug_skip()`             | Skip current command          |
| `bc_debug_inspect_ast()`      | Inspect AST (returns JSON)    |
| `bc_debug_on_break_hit()`     | Register break callback       |

**PTY:**

| Function              | Description                     |
|-----------------------|---------------------------------|
| `bc_pty_spawn()`      | Spawn PTY session               |
| `bc_pty_write()`      | Write input to PTY              |
| `bc_pty_resize()`     | Resize terminal                 |
| `bc_pty_signal()`     | Send signal to PTY              |
| `bc_pty_close()`      | Close PTY session               |
| `bc_pty_on_output()`  | Register output callback        |
| `bc_pty_on_exit()`    | Register exit callback          |

# Complete example

```c
#include <stdio.h>
#include <stdlib.h>
#include "bashclient.h"

static void on_pre(const bc_pre_command_event_t *e, void *ud) {
    printf(">>> %s\n", e->command);
}

static void on_post(const bc_post_command_event_t *e, void *ud) {
    printf("<<< %s (exit %d, %dms)\n",
           e->command, e->exit_status, e->duration_ms);
}

int main(void) {
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock");
    if (!c) { perror("connect"); return 1; }

    if (bc_auth(c, "abcdef0123456789...") != BC_OK) {
        fprintf(stderr, "auth: %s\n", bc_error(c));
        bc_close(c);
        return 1;
    }

    /* Simple eval */
    bc_eval_result_t result;
    if (bc_eval(c, "echo hello world", &result) == BC_OK) {
        printf("stdout: %s", result.stdout_data);
        bc_eval_result_free(&result);
    }

    /* Set and read a variable */
    bc_state_set_var(c, "GREETING", "hello", NULL, 0);
    bc_var_info_t info;
    if (bc_state_get_var(c, "GREETING", &info) == BC_OK) {
        printf("GREETING=%s\n", info.value);
        bc_var_info_free(&info);
    }

    /* Observe */
    bc_observe_on_pre_command(c, on_pre, NULL);
    bc_observe_on_post_command(c, on_post, NULL);
    bc_observe_subscribe(c, 1);

    bc_eval(c, "true", &result);
    bc_eval_result_free(&result);

    bc_poll(c, 100);  /* dispatch pending events */

    bc_close(c);
    return 0;
}
```

# SEE ALSO

**bc_connect**(3),
**bc_auth**(3),
**bc_eval**(3),
**bc_state_get_var**(3),
**bc_observe_subscribe**(3),
**bc_debug_add_breakpoint**(3),
**bc_pty_spawn**(3),
**bc_poll**(3),
**bash-server-client-api**(7),
**bash-server-client-channels**(7),
**bash-server-client-transports**(7),
**bash-server**(1)

# AUTHORS

GNU Bash is Copyright (C) Free Software Foundation, Inc.
The bash-server extension was developed as part of the Cygwin Bash project.

# COPYRIGHT

This is free software; see the GNU General Public License v3 or later
for copying conditions.  There is NO warranty.
