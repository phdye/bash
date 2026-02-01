# libbashclient API Reference

Complete reference for every public function in `bashclient.h`.

**Header**: `#include <bashclient.h>`
**Link**: `-lbashclient`

---

## Table of Contents

1. [Error Codes](#error-codes)
2. [Constants](#constants)
3. [Types](#types)
4. [Connection Functions](#connection-functions)
   - [bc_connect](#bc_connect)
   - [bc_connect_opts](#bc_connect_opts)
   - [bc_close](#bc_close)
   - [bc_ping](#bc_ping)
   - [bc_configure](#bc_configure)
   - [bc_disconnect](#bc_disconnect)
   - [bc_get_fd](#bc_get_fd)
   - [bc_last_error](#bc_last_error)
   - [bc_last_error_msg](#bc_last_error_msg)
5. [Command Functions](#command-functions)
   - [bc_eval](#bc_eval)
   - [bc_eval_stream](#bc_eval_stream)
6. [State Functions](#state-functions)
   - [bc_var_get](#bc_var_get)
   - [bc_var_set](#bc_var_set)
   - [bc_var_set_attr](#bc_var_set_attr)
   - [bc_var_unset](#bc_var_unset)
   - [bc_func_get](#bc_func_get)
   - [bc_func_set](#bc_func_set)
   - [bc_func_unset](#bc_func_unset)
   - [bc_alias_get](#bc_alias_get)
   - [bc_alias_set](#bc_alias_set)
   - [bc_alias_unset](#bc_alias_unset)
   - [bc_trap_get](#bc_trap_get)
   - [bc_trap_set](#bc_trap_set)
   - [bc_trap_unset](#bc_trap_unset)
   - [bc_inspect](#bc_inspect)
7. [Observe Functions](#observe-functions)
   - [bc_observe_set_level](#bc_observe_set_level)
   - [bc_on_pre_command](#bc_on_pre_command)
   - [bc_on_post_command](#bc_on_post_command)
   - [bc_poll](#bc_poll)
   - [bc_observe_cleanup](#bc_observe_cleanup)
8. [Debug Functions](#debug-functions)
   - [bc_debug_enable](#bc_debug_enable)
   - [bc_debug_disable](#bc_debug_disable)
   - [bc_debug_status](#bc_debug_status)
   - [bc_debug_break_command](#bc_debug_break_command)
   - [bc_debug_break_line](#bc_debug_break_line)
   - [bc_debug_break_function](#bc_debug_break_function)
   - [bc_debug_set_condition](#bc_debug_set_condition)
   - [bc_debug_enable_breakpoint](#bc_debug_enable_breakpoint)
   - [bc_debug_remove_breakpoint](#bc_debug_remove_breakpoint)
   - [bc_debug_list_breakpoints](#bc_debug_list_breakpoints)
   - [bc_debug_step](#bc_debug_step)
   - [bc_debug_step_into](#bc_debug_step_into)
   - [bc_debug_step_out](#bc_debug_step_out)
   - [bc_debug_continue](#bc_debug_continue)
   - [bc_debug_skip](#bc_debug_skip)
   - [bc_debug_inspect_ast](#bc_debug_inspect_ast)
   - [bc_on_break_hit](#bc_on_break_hit)
9. [PTY Functions](#pty-functions)
   - [bc_pty_spawn](#bc_pty_spawn)
   - [bc_pty_read](#bc_pty_read)
   - [bc_pty_write](#bc_pty_write)
   - [bc_pty_resize](#bc_pty_resize)
   - [bc_pty_set_strip_ansi](#bc_pty_set_strip_ansi)
   - [bc_pty_close](#bc_pty_close)
   - [bc_pty_info_free](#bc_pty_info_free)
10. [Utility Functions](#utility-functions)
    - [bc_free](#bc_free)
    - [bc_version](#bc_version)
    - [bc_eval_result_free](#bc_eval_result_free)
    - [bc_debug_status_free](#bc_debug_status_free)
    - [bc_breakpoints_free](#bc_breakpoints_free)

---

## Error Codes

| Code                | Value | Description                                    |
|---------------------|-------|------------------------------------------------|
| `BC_OK`             | 0     | Operation completed successfully               |
| `BC_ERR_AUTH`       | -1    | Authentication failed (bad token, denied)      |
| `BC_ERR_PROTOCOL`   | -2    | Protocol error (malformed frame, bad JSON)     |
| `BC_ERR_TIMEOUT`    | -3    | Operation timed out                            |
| `BC_ERR_TRANSPORT`  | -4    | Transport error (connection lost, pipe broken) |
| `BC_ERR_SERVER`     | -5    | Server returned an error response              |
| `BC_ERR_NOMEM`      | -6    | Memory allocation failed                       |
| `BC_ERR_PARAM`      | -7    | Invalid parameter (NULL pointer, bad value)    |

---

## Constants

| Constant              | Value | Description                       |
|-----------------------|-------|-----------------------------------|
| `BC_API_VERSION`      | 1     | API version for compile-time checks |
| `BC_VERSION_STRING`   | "0.1.0" | Library version string          |
| `BC_TRANSPORT_SOCKET` | 0     | Unix domain socket transport      |
| `BC_TRANSPORT_STDIO`  | 1     | Stdio (fork+exec) transport       |
| `BC_TRANSPORT_FD`     | 2     | Pre-opened file descriptor transport |
| `BC_TRANSPORT_PIPE`   | 3     | Windows Named Pipe transport      |
| `BC_WIRE_BINARY`      | 0     | Binary v2 framing (6-byte header) |
| `BC_WIRE_NDJSON`      | 1     | NDJSON framing (newline-delimited)|

---

## Types

### bc_client_t

```c
typedef struct bc_client bc_client_t;
```

Opaque client handle. Allocated by `bc_connect()` or `bc_connect_opts()`,
freed by `bc_close()`. All API functions take this as their first
parameter.

### bc_opts_t

```c
typedef struct {
    int         transport;          /* BC_TRANSPORT_* (default: SOCKET) */
    const char *socket_path;        /* Unix socket path (SOCKET transport) */
    const char *auth_token;         /* Authentication token */
    const char *stdio_command;      /* Command to exec (STDIO transport) */
    int         fd_read;            /* Read fd (FD transport) */
    int         fd_write;           /* Write fd (FD transport) */
    const char *pipe_name;          /* Named pipe path (PIPE transport) */
    int         connect_timeout_ms; /* Connection timeout (default: 10000) */
    int         read_timeout_ms;    /* Read timeout (default: 60000) */
    int         write_timeout_ms;   /* Write timeout (default: 30000) */
    int         protocol_version;   /* Requested version (default: 0 = auto) */
    int         wire_format;        /* BC_WIRE_* (default: NDJSON) */
} bc_opts_t;

#define BC_OPTS_INIT {0}
```

Connection options struct. Always initialize with `BC_OPTS_INIT` to
ensure all fields default to zero/NULL. Zero values select defaults.

### bc_eval_result_t

```c
typedef struct {
    char *stdout_data;    /* Captured stdout (never NULL, may be "") */
    char *stderr_data;    /* Captured stderr (never NULL, may be "") */
    int   exit_code;      /* Exit code of the last command */
} bc_eval_result_t;
```

Result of a command evaluation. Free with `bc_eval_result_free()`.

### bc_var_info_t

```c
typedef struct {
    char  *name;           /* Variable name */
    char  *value;          /* Variable value */
    char **attributes;     /* Array of attribute strings (e.g., "i", "r", "x") */
    int    num_attributes; /* Number of attributes */
} bc_var_info_t;
```

Detailed variable information including attributes. Used by inspection
functions.

### bc_pre_command_event_t

```c
typedef struct {
    int        seq;          /* Sequence number */
    long long  timestamp;    /* Unix timestamp in milliseconds */
    char      *command;      /* Command text */
    char      *cwd;          /* Current working directory */
    int        line_number;  /* Source line number */
    int        is_subshell;  /* 1 if executing in a subshell */
    int        is_async;     /* 1 if an asynchronous command */
} bc_pre_command_event_t;
```

Delivered to pre-command callbacks. Valid only during the callback
invocation. Copy any data you need to retain.

### bc_post_command_event_t

```c
typedef struct {
    int        seq;            /* Sequence number (matches pre event) */
    long long  timestamp;      /* Unix timestamp in milliseconds */
    char      *command;        /* Command text */
    int        exit_status;    /* Exit status */
    int        signal_number;  /* Signal that killed the command (0 if none) */
    int        duration_ms;    /* Execution duration in milliseconds */
} bc_post_command_event_t;
```

Delivered to post-command callbacks. Valid only during the callback.

### bc_breakpoint_t

```c
typedef struct {
    int   id;          /* Breakpoint ID (unique, assigned by server) */
    char *kind;        /* "command", "line", or "function" */
    int   enabled;     /* 1 if enabled, 0 if disabled */
    int   hit_count;   /* Number of times this breakpoint was hit */
    char *pattern;     /* Match pattern (command glob, function name) */
    int   line;        /* Line number (for line breakpoints, 0 otherwise) */
    char *condition;   /* Condition expression (NULL if unconditional) */
} bc_breakpoint_t;
```

Breakpoint descriptor returned by `bc_debug_list_breakpoints()`. Free
arrays with `bc_breakpoints_free()`.

### bc_break_hit_event_t

```c
typedef struct {
    int   line;      /* Line number where execution stopped */
    char *command;   /* Command at the break point */
    int   depth;     /* Function call depth */
} bc_break_hit_event_t;
```

Delivered to break-hit callbacks. Valid only during the callback.

### bc_debug_status_t

```c
typedef struct {
    int   active;       /* 1 if debugger is enabled */
    char *mode;         /* "running", "stopped", or "disabled" */
    int   breakpoints;  /* Number of breakpoints set */
    int   depth;        /* Current function call depth */
} bc_debug_status_t;
```

Debugger status. Free with `bc_debug_status_free()`.

### bc_pty_info_t

```c
typedef struct {
    int rows;        /* Terminal rows */
    int cols;        /* Terminal columns */
    int pid;         /* Child process PID */
    int strip_ansi;  /* 1 if ANSI stripping is enabled */
} bc_pty_info_t;
```

PTY session information. Free with `bc_pty_info_free()`.

---

## Connection Functions

### bc_connect

```c
bc_client_t *bc_connect(const char *socket_path, const char *auth_token);
```

Connect to a bash-server via Unix domain socket.

**Parameters:**

| Parameter     | Type           | Description                              |
|---------------|----------------|------------------------------------------|
| `socket_path` | `const char *` | Socket path, or NULL for auto-discovery |
| `auth_token`  | `const char *` | Auth token, or NULL for auto-discovery  |

**Return value:**

A new client handle on success, or NULL on failure.

**Auto-discovery order** (when `socket_path` is NULL):
1. `$BASH_SERVER_SOCKET` environment variable
2. `~/.bash-serverrc` configuration file
3. `$XDG_RUNTIME_DIR/bash-server/sock`
4. `/tmp/bash-server-<uid>/sock`

**Auto-discovery order** (when `auth_token` is NULL):
1. Token file next to socket (`.token` suffix)
2. `~/.bash-serverrc` configuration file

**Errors:** `BC_ERR_TRANSPORT`, `BC_ERR_AUTH`, `BC_ERR_TIMEOUT`, `BC_ERR_NOMEM`

Use `bc_last_error(NULL)` and `bc_last_error_msg(NULL)` to get error
details after a failed connect.

**Example:**

```c
bc_client_t *c = bc_connect(NULL, NULL);
if (!c) {
    fprintf(stderr, "Connect: %s\n", bc_last_error_msg(NULL));
    return 1;
}
/* ... use c ... */
bc_close(c);
```

---

### bc_connect_opts

```c
bc_client_t *bc_connect_opts(const bc_opts_t *opts);
```

Connect to a bash-server with full control over transport and options.

**Parameters:**

| Parameter | Type              | Description        |
|-----------|-------------------|--------------------|
| `opts`    | `const bc_opts_t *` | Connection options |

**Return value:**

A new client handle on success, or NULL on failure.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_AUTH`, `BC_ERR_TIMEOUT`, `BC_ERR_NOMEM`

**Example:**

```c
bc_opts_t opts = BC_OPTS_INIT;
opts.transport = BC_TRANSPORT_STDIO;
opts.stdio_command = "bash-server --stdio";
opts.read_timeout_ms = 5000;

bc_client_t *c = bc_connect_opts(&opts);
if (!c) {
    fprintf(stderr, "Connect: %s\n", bc_last_error_msg(NULL));
    return 1;
}
bc_close(c);
```

---

### bc_close

```c
void bc_close(bc_client_t *c);
```

Close the connection and free all resources associated with the client
handle.

**Parameters:**

| Parameter | Type           | Description                       |
|-----------|----------------|-----------------------------------|
| `c`       | `bc_client_t *` | Client handle, or NULL (no-op)   |

After calling `bc_close()`, the handle is invalid and must not be used.
Passing NULL is safe.

For stdio transport, this terminates the child process. For socket and
pipe transports, this closes the file descriptors. For fd transport, the
caller-provided fds are NOT closed.

**Example:**

```c
bc_client_t *c = bc_connect(NULL, NULL);
/* ... */
bc_close(c);
c = NULL;  /* Avoid dangling pointer */
```

---

### bc_ping

```c
int bc_ping(bc_client_t *c);
```

Send a ping to the server and wait for a pong response. Useful for
checking that the server is responsive.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_TIMEOUT`

**Example:**

```c
if (bc_ping(c) == BC_OK)
    printf("Server is responsive\n");
```

---

### bc_configure

```c
int bc_configure(bc_client_t *c, const char *key, const char *value);
```

Set a session configuration parameter on the server.

**Parameters:**

| Parameter | Type           | Description                     |
|-----------|----------------|---------------------------------|
| `c`       | `bc_client_t *` | Client handle                  |
| `key`     | `const char *` | Configuration key               |
| `value`   | `const char *` | Configuration value             |

**Known keys:**

| Key             | Values           | Description                   |
|-----------------|------------------|-------------------------------|
| `wire_format`   | `"binary"`, `"ndjson"` | Switch wire format       |
| `observe_level` | `"0"`, `"1"`, `"2"` | Set observe level          |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_SERVER`, `BC_ERR_TRANSPORT`

**Example:**

```c
bc_configure(c, "wire_format", "ndjson");
```

---

### bc_disconnect

```c
int bc_disconnect(bc_client_t *c);
```

Send a graceful disconnect message to the server. The server closes its
end of the connection. After disconnect, the handle is still valid but
all operations will return `BC_ERR_TRANSPORT`. Call `bc_close()` to free
the handle.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`

**Example:**

```c
bc_disconnect(c);
bc_close(c);
```

---

### bc_get_fd

```c
int bc_get_fd(bc_client_t *c);
```

Get the underlying file descriptor for the connection. Useful for
integrating with `select()`, `poll()`, or `epoll()` event loops.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:**

The file descriptor on success, or -1 if the transport does not expose
an fd (e.g., some pipe transports).

**Example:**

```c
int fd = bc_get_fd(c);
struct pollfd pfd = { .fd = fd, .events = POLLIN };
poll(&pfd, 1, 1000);
if (pfd.revents & POLLIN)
    bc_poll(c, 0);
```

---

### bc_last_error

```c
int bc_last_error(bc_client_t *c);
```

Get the error code from the last operation.

**Parameters:**

| Parameter | Type           | Description                                    |
|-----------|----------------|------------------------------------------------|
| `c`       | `bc_client_t *` | Client handle, or NULL for last connect error |

**Return value:**

A `BC_OK` or `BC_ERR_*` error code.

---

### bc_last_error_msg

```c
const char *bc_last_error_msg(bc_client_t *c);
```

Get a human-readable error message for the last operation.

**Parameters:**

| Parameter | Type           | Description                                    |
|-----------|----------------|------------------------------------------------|
| `c`       | `bc_client_t *` | Client handle, or NULL for last connect error |

**Return value:**

A pointer to an internal string. Valid until the next API call on the
same handle. Do NOT free this string.

**Example:**

```c
bc_eval_result_t *r = bc_eval(c, "bad command");
if (!r) {
    fprintf(stderr, "Error %d: %s\n",
            bc_last_error(c), bc_last_error_msg(c));
}
```

---

## Command Functions

### bc_eval

```c
bc_eval_result_t *bc_eval(bc_client_t *c, const char *command);
```

Evaluate a shell command string and return the captured output.

This function blocks until the command completes and the server sends
the result. The command is executed in the server's shell context,
so variables, functions, and aliases from previous evaluations persist.

**Parameters:**

| Parameter | Type           | Description                    |
|-----------|----------------|--------------------------------|
| `c`       | `bc_client_t *` | Client handle                 |
| `command` | `const char *` | Shell command string (not NULL)|

**Return value:**

A heap-allocated `bc_eval_result_t *` on success, or NULL on failure.
The caller must free the result with `bc_eval_result_free()`.

The `stdout_data` and `stderr_data` fields are never NULL. They point
to empty strings (`""`) if the command produced no output.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_TIMEOUT`, `BC_ERR_SERVER`, `BC_ERR_NOMEM`

**Example:**

```c
bc_eval_result_t *r = bc_eval(c, "echo hello && echo world");
if (r) {
    printf("stdout: %s", r->stdout_data);
    printf("exit: %d\n", r->exit_code);
    bc_eval_result_free(r);
}
```

---

### bc_eval_stream

```c
int bc_eval_stream(bc_client_t *c, const char *command,
                   void (*on_stdout)(const char *data, size_t len, void *ud),
                   void (*on_stderr)(const char *data, size_t len, void *ud),
                   void (*on_complete)(int exit_code, void *ud),
                   void *userdata);
```

Start a streaming evaluation. Output is delivered incrementally via
callbacks as data arrives. You must call `bc_poll()` in a loop to
receive the callbacks.

**Parameters:**

| Parameter     | Type           | Description                          |
|---------------|----------------|--------------------------------------|
| `c`           | `bc_client_t *` | Client handle                       |
| `command`     | `const char *` | Shell command string                  |
| `on_stdout`   | callback       | Called with stdout chunks (may be NULL) |
| `on_stderr`   | callback       | Called with stderr chunks (may be NULL) |
| `on_complete` | callback       | Called when command finishes (may be NULL) |
| `userdata`    | `void *`       | Passed to all callbacks               |

**Return value:** `BC_OK` if the command was sent, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`

**Example:**

```c
static void on_out(const char *data, size_t len, void *ud) {
    fwrite(data, 1, len, stdout);
}
static void on_done(int code, void *ud) {
    *(int *)ud = 1;
}

int done = 0;
bc_eval_stream(c, "seq 1 100", on_out, NULL, on_done, &done);
while (!done)
    bc_poll(c, 100);
```

---

## State Functions

### bc_var_get

```c
char *bc_var_get(bc_client_t *c, const char *name);
```

Get the value of a shell variable.

**Parameters:**

| Parameter | Type           | Description                                  |
|-----------|----------------|----------------------------------------------|
| `c`       | `bc_client_t *` | Client handle                               |
| `name`    | `const char *` | Variable name (e.g., `"HOME"`, `"arr[0]"`)   |

**Return value:**

A heap-allocated string containing the variable's value, or NULL if the
variable is not set or an error occurred. Free with `bc_free()`.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_TIMEOUT`

**Example:**

```c
char *val = bc_var_get(c, "PATH");
if (val) {
    printf("PATH=%s\n", val);
    bc_free(val);
}
```

---

### bc_var_set

```c
int bc_var_set(bc_client_t *c, const char *name, const char *value);
```

Set a shell variable to a string value.

**Parameters:**

| Parameter | Type           | Description        |
|-----------|----------------|--------------------|
| `c`       | `bc_client_t *` | Client handle     |
| `name`    | `const char *` | Variable name      |
| `value`   | `const char *` | Value to set       |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

**Example:**

```c
bc_var_set(c, "MY_VAR", "hello");
```

---

### bc_var_set_attr

```c
int bc_var_set_attr(bc_client_t *c, const char *name, const char *value,
                    const char *attributes);
```

Set a shell variable with attributes.

**Parameters:**

| Parameter    | Type           | Description                                |
|--------------|----------------|--------------------------------------------|
| `c`          | `bc_client_t *` | Client handle                             |
| `name`       | `const char *` | Variable name                              |
| `value`      | `const char *` | Value to set                               |
| `attributes` | `const char *` | Attribute string (e.g., `"ix"` for integer+export) |

**Attribute characters:**

| Char | Meaning      | Equivalent    |
|------|-------------|---------------|
| `i`  | Integer     | `declare -i`  |
| `r`  | Readonly    | `declare -r`  |
| `x`  | Export      | `declare -x`  |
| `l`  | Lowercase   | `declare -l`  |
| `u`  | Uppercase   | `declare -u`  |
| `a`  | Array       | `declare -a`  |
| `A`  | Assoc array | `declare -A`  |
| `n`  | Nameref     | `declare -n`  |
| `t`  | Trace       | `declare -t`  |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

**Example:**

```c
bc_var_set_attr(c, "COUNT", "42", "ix");  /* integer, exported */
```

---

### bc_var_unset

```c
int bc_var_unset(bc_client_t *c, const char *name);
```

Unset (remove) a shell variable.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |
| `name`    | `const char *` | Variable name |

**Return value:** `BC_OK` on success, or an error code. Returns `BC_OK`
even if the variable did not exist.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_func_get

```c
char *bc_func_get(bc_client_t *c, const char *name);
```

Get the body of a shell function.

**Parameters:**

| Parameter | Type           | Description    |
|-----------|----------------|----------------|
| `c`       | `bc_client_t *` | Client handle |
| `name`    | `const char *` | Function name  |

**Return value:**

A heap-allocated string containing the function body, or NULL if the
function is not defined. Free with `bc_free()`.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_TIMEOUT`

**Example:**

```c
char *body = bc_func_get(c, "greet");
if (body) {
    printf("greet() { %s; }\n", body);
    bc_free(body);
}
```

---

### bc_func_set

```c
int bc_func_set(bc_client_t *c, const char *name, const char *body);
```

Define or replace a shell function.

**Parameters:**

| Parameter | Type           | Description                           |
|-----------|----------------|---------------------------------------|
| `c`       | `bc_client_t *` | Client handle                        |
| `name`    | `const char *` | Function name                         |
| `body`    | `const char *` | Function body (shell commands)        |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

**Example:**

```c
bc_func_set(c, "greet", "echo \"Hello, $1!\"");
```

---

### bc_func_unset

```c
int bc_func_unset(bc_client_t *c, const char *name);
```

Remove a shell function definition.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |
| `name`    | `const char *` | Function name |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_alias_get

```c
char *bc_alias_get(bc_client_t *c, const char *name);
```

Get the expansion text of a shell alias.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |
| `name`    | `const char *` | Alias name    |

**Return value:**

A heap-allocated string with the alias expansion, or NULL if the alias
is not defined. Free with `bc_free()`.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_TIMEOUT`

**Example:**

```c
char *exp = bc_alias_get(c, "ll");
if (exp) {
    printf("alias ll='%s'\n", exp);
    bc_free(exp);
}
```

---

### bc_alias_set

```c
int bc_alias_set(bc_client_t *c, const char *name, const char *expansion);
```

Define or replace a shell alias.

**Parameters:**

| Parameter   | Type           | Description             |
|-------------|----------------|-------------------------|
| `c`         | `bc_client_t *` | Client handle          |
| `name`      | `const char *` | Alias name              |
| `expansion` | `const char *` | Alias expansion text    |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

**Example:**

```c
bc_alias_set(c, "ll", "ls -la --color=auto");
```

---

### bc_alias_unset

```c
int bc_alias_unset(bc_client_t *c, const char *name);
```

Remove a shell alias.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |
| `name`    | `const char *` | Alias name    |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_trap_get

```c
char *bc_trap_get(bc_client_t *c, const char *signal_name);
```

Get the trap action for a signal.

**Parameters:**

| Parameter     | Type           | Description                             |
|---------------|----------------|-----------------------------------------|
| `c`           | `bc_client_t *` | Client handle                          |
| `signal_name` | `const char *` | Signal name (e.g., `"SIGINT"`, `"EXIT"`) |

**Return value:**

A heap-allocated string with the trap action, or NULL if no trap is set.
Free with `bc_free()`.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_TIMEOUT`

**Example:**

```c
char *action = bc_trap_get(c, "SIGINT");
if (action) {
    printf("trap -- '%s' SIGINT\n", action);
    bc_free(action);
}
```

---

### bc_trap_set

```c
int bc_trap_set(bc_client_t *c, const char *signal_name,
                const char *action);
```

Set a trap action for a signal.

**Parameters:**

| Parameter     | Type           | Description                   |
|---------------|----------------|-------------------------------|
| `c`           | `bc_client_t *` | Client handle                |
| `signal_name` | `const char *` | Signal name                   |
| `action`      | `const char *` | Shell command to execute      |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

**Example:**

```c
bc_trap_set(c, "EXIT", "echo 'Goodbye'");
```

---

### bc_trap_unset

```c
int bc_trap_unset(bc_client_t *c, const char *signal_name);
```

Remove a trap for a signal (reset to default handling).

**Parameters:**

| Parameter     | Type           | Description   |
|---------------|----------------|---------------|
| `c`           | `bc_client_t *` | Client handle |
| `signal_name` | `const char *` | Signal name   |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_inspect

```c
char *bc_inspect(bc_client_t *c, const char *namespace);
```

Get a JSON dump of all entries in a namespace.

**Parameters:**

| Parameter   | Type           | Description                                    |
|-------------|----------------|------------------------------------------------|
| `c`         | `bc_client_t *` | Client handle                                 |
| `namespace` | `const char *` | `"variables"`, `"functions"`, `"aliases"`, or `"traps"` |

**Return value:**

A heap-allocated JSON string, or NULL on error. Free with `bc_free()`.

The JSON format varies by namespace but generally returns an array of
objects with name, value, and attribute fields.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_TIMEOUT`, `BC_ERR_NOMEM`

**Example:**

```c
char *json = bc_inspect(c, "variables");
if (json) {
    printf("%s\n", json);
    bc_free(json);
}
```

---

## Observe Functions

### bc_observe_set_level

```c
int bc_observe_set_level(bc_client_t *c, int level);
```

Set the observation level on the server.

**Parameters:**

| Parameter | Type           | Description                                |
|-----------|----------------|--------------------------------------------|
| `c`       | `bc_client_t *` | Client handle                             |
| `level`   | `int`          | 0 = off, 1 = pre only, 2 = pre and post   |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

**Example:**

```c
bc_observe_set_level(c, 2);  /* Enable pre+post events */
```

---

### bc_on_pre_command

```c
void bc_on_pre_command(bc_client_t *c,
                       void (*fn)(const bc_pre_command_event_t *ev,
                                  void *userdata),
                       void *userdata);
```

Register a callback for pre-command events. Pass `fn = NULL` to
unregister.

**Parameters:**

| Parameter  | Type       | Description                              |
|------------|------------|------------------------------------------|
| `c`        | `bc_client_t *` | Client handle                       |
| `fn`       | callback   | Callback function, or NULL to remove     |
| `userdata` | `void *`   | Passed to callback                       |

The callback receives a pointer to a `bc_pre_command_event_t` that is
valid only during the callback invocation. Copy any needed data.

**Example:**

```c
static void on_pre(const bc_pre_command_event_t *ev, void *ud) {
    printf(">>> %s\n", ev->command);
}
bc_on_pre_command(c, on_pre, NULL);
```

---

### bc_on_post_command

```c
void bc_on_post_command(bc_client_t *c,
                        void (*fn)(const bc_post_command_event_t *ev,
                                   void *userdata),
                        void *userdata);
```

Register a callback for post-command events. Pass `fn = NULL` to
unregister.

**Parameters:**

| Parameter  | Type       | Description                              |
|------------|------------|------------------------------------------|
| `c`        | `bc_client_t *` | Client handle                       |
| `fn`       | callback   | Callback function, or NULL to remove     |
| `userdata` | `void *`   | Passed to callback                       |

**Example:**

```c
static void on_post(const bc_post_command_event_t *ev, void *ud) {
    printf("<<< %s (exit %d)\n", ev->command, ev->exit_status);
}
bc_on_post_command(c, on_post, NULL);
```

---

### bc_poll

```c
int bc_poll(bc_client_t *c, int timeout_ms);
```

Poll for and dispatch server-push events (OBSERVE, DEBUG, PTY).

Reads available data from the transport and dispatches any complete
events to registered callbacks. Blocks for up to `timeout_ms`
milliseconds if no data is immediately available.

**Parameters:**

| Parameter    | Type           | Description                          |
|--------------|----------------|--------------------------------------|
| `c`          | `bc_client_t *` | Client handle                       |
| `timeout_ms` | `int`          | Maximum wait time (0 = non-blocking)|

**Return value:**

The number of events dispatched (>= 0), or a negative error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_PROTOCOL`

**Example:**

```c
while (running) {
    int n = bc_poll(c, 500);
    if (n < 0) break;
    /* n events were dispatched to callbacks */
}
```

---

### bc_observe_cleanup

```c
void bc_observe_cleanup(bc_client_t *c);
```

Unregister all observe callbacks and set the observe level to 0.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

Equivalent to:

```c
bc_on_pre_command(c, NULL, NULL);
bc_on_post_command(c, NULL, NULL);
bc_observe_set_level(c, 0);
```

---

## Debug Functions

### bc_debug_enable

```c
int bc_debug_enable(bc_client_t *c);
```

Enable the debugger on the server. Must be called before setting
breakpoints or stepping.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_debug_disable

```c
int bc_debug_disable(bc_client_t *c);
```

Disable the debugger. Removes all breakpoints and resumes normal
execution.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_debug_status

```c
bc_debug_status_t *bc_debug_status(bc_client_t *c);
```

Get the current debugger status.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:**

A heap-allocated `bc_debug_status_t *`, or NULL on error. Free with
`bc_debug_status_free()`.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_NOMEM`

**Example:**

```c
bc_debug_status_t *st = bc_debug_status(c);
if (st) {
    printf("active=%d mode=%s bps=%d\n",
           st->active, st->mode, st->breakpoints);
    bc_debug_status_free(st);
}
```

---

### bc_debug_break_command

```c
int bc_debug_break_command(bc_client_t *c, const char *pattern);
```

Set a breakpoint on commands matching a glob pattern.

**Parameters:**

| Parameter | Type           | Description                   |
|-----------|----------------|-------------------------------|
| `c`       | `bc_client_t *` | Client handle                |
| `pattern` | `const char *` | Glob pattern (e.g., `"rm *"`)|

**Return value:**

The breakpoint ID (>= 0) on success, or a negative error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_debug_break_line

```c
int bc_debug_break_line(bc_client_t *c, int line);
```

Set a breakpoint on a specific source line number.

**Parameters:**

| Parameter | Type           | Description    |
|-----------|----------------|----------------|
| `c`       | `bc_client_t *` | Client handle |
| `line`    | `int`          | Line number (>= 1) |

**Return value:**

The breakpoint ID (>= 0) on success, or a negative error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_debug_break_function

```c
int bc_debug_break_function(bc_client_t *c, const char *name);
```

Set a breakpoint on entry to a shell function.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |
| `name`    | `const char *` | Function name |

**Return value:**

The breakpoint ID (>= 0) on success, or a negative error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_debug_set_condition

```c
int bc_debug_set_condition(bc_client_t *c, int bp_id,
                           const char *condition);
```

Set a condition on an existing breakpoint. The breakpoint only triggers
when the condition evaluates to true (exit code 0).

**Parameters:**

| Parameter   | Type           | Description                            |
|-------------|----------------|----------------------------------------|
| `c`         | `bc_client_t *` | Client handle                         |
| `bp_id`     | `int`          | Breakpoint ID                          |
| `condition` | `const char *` | Shell expression, or NULL to clear     |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

**Example:**

```c
int bp = bc_debug_break_line(c, 10);
bc_debug_set_condition(c, bp, "[ $x -gt 100 ]");
```

---

### bc_debug_enable_breakpoint

```c
int bc_debug_enable_breakpoint(bc_client_t *c, int bp_id, int enabled);
```

Enable or disable a breakpoint without removing it.

**Parameters:**

| Parameter | Type           | Description                   |
|-----------|----------------|-------------------------------|
| `c`       | `bc_client_t *` | Client handle                |
| `bp_id`   | `int`          | Breakpoint ID                 |
| `enabled` | `int`          | 1 to enable, 0 to disable    |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_debug_remove_breakpoint

```c
int bc_debug_remove_breakpoint(bc_client_t *c, int bp_id);
```

Remove a breakpoint permanently.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |
| `bp_id`   | `int`          | Breakpoint ID |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_debug_list_breakpoints

```c
int bc_debug_list_breakpoints(bc_client_t *c, bc_breakpoint_t **out,
                              int *count);
```

List all breakpoints.

**Parameters:**

| Parameter | Type               | Description                        |
|-----------|--------------------|------------------------------------|
| `c`       | `bc_client_t *`    | Client handle                      |
| `out`     | `bc_breakpoint_t **` | Output: array of breakpoints     |
| `count`   | `int *`            | Output: number of breakpoints      |

**Return value:** `BC_OK` on success, or an error code.

On success, `*out` points to a heap-allocated array of `*count`
breakpoints. Free with `bc_breakpoints_free(*out, *count)`.

If there are no breakpoints, `*out` is NULL and `*count` is 0.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_NOMEM`

**Example:**

```c
bc_breakpoint_t *bps = NULL;
int count = 0;
if (bc_debug_list_breakpoints(c, &bps, &count) == BC_OK) {
    for (int i = 0; i < count; i++)
        printf("bp %d: %s\n", bps[i].id, bps[i].kind);
    bc_breakpoints_free(bps, count);
}
```

---

### bc_debug_step

```c
int bc_debug_step(bc_client_t *c);
```

Step to the next command (step over). Does not enter function calls.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_debug_step_into

```c
int bc_debug_step_into(bc_client_t *c);
```

Step into the next command. Enters function calls.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_debug_step_out

```c
int bc_debug_step_out(bc_client_t *c);
```

Continue execution until the current function returns.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_debug_continue

```c
int bc_debug_continue(bc_client_t *c);
```

Continue execution until the next breakpoint or script completion.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_debug_skip

```c
int bc_debug_skip(bc_client_t *c);
```

Skip the current command without executing it and advance to the next.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_debug_inspect_ast

```c
char *bc_debug_inspect_ast(bc_client_t *c);
```

Get a JSON representation of the current command's AST when stopped at
a breakpoint.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:**

A heap-allocated JSON string, or NULL on error. Free with `bc_free()`.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`, `BC_ERR_NOMEM`

**Example:**

```c
char *ast = bc_debug_inspect_ast(c);
if (ast) {
    printf("AST: %s\n", ast);
    bc_free(ast);
}
```

---

### bc_on_break_hit

```c
void bc_on_break_hit(bc_client_t *c,
                     void (*fn)(const bc_break_hit_event_t *ev,
                                void *userdata),
                     void *userdata);
```

Register a callback for breakpoint hit events. Pass `fn = NULL` to
unregister.

**Parameters:**

| Parameter  | Type       | Description                              |
|------------|------------|------------------------------------------|
| `c`        | `bc_client_t *` | Client handle                       |
| `fn`       | callback   | Callback function, or NULL to remove     |
| `userdata` | `void *`   | Passed to callback                       |

**Example:**

```c
static void on_break(const bc_break_hit_event_t *ev, void *ud) {
    printf("Break at line %d: %s\n", ev->line, ev->command);
}
bc_on_break_hit(c, on_break, NULL);
```

---

## PTY Functions

### bc_pty_spawn

```c
bc_pty_info_t *bc_pty_spawn(bc_client_t *c, int cols, int rows,
                            const char *command);
```

Spawn a pseudo-terminal session on the server.

**Parameters:**

| Parameter | Type           | Description                              |
|-----------|----------------|------------------------------------------|
| `c`       | `bc_client_t *` | Client handle                           |
| `cols`    | `int`          | Terminal width in columns                 |
| `rows`    | `int`          | Terminal height in rows                   |
| `command` | `const char *` | Command to run, or NULL for default shell|

**Return value:**

A heap-allocated `bc_pty_info_t *` on success, or NULL on error. Free
with `bc_pty_info_free()`.

Only one PTY session is active at a time. Close the current PTY before
spawning a new one.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`, `BC_ERR_NOMEM`

**Example:**

```c
bc_pty_info_t *info = bc_pty_spawn(c, 80, 24, NULL);
if (info) {
    printf("PTY pid=%d %dx%d\n", info->pid, info->cols, info->rows);
    bc_pty_info_free(info);
}
```

---

### bc_pty_read

```c
int bc_pty_read(bc_client_t *c, char *buf, size_t buflen, int timeout_ms);
```

Read output from the PTY.

**Parameters:**

| Parameter    | Type           | Description                           |
|--------------|----------------|---------------------------------------|
| `c`          | `bc_client_t *` | Client handle                        |
| `buf`        | `char *`       | Output buffer                         |
| `buflen`     | `size_t`       | Buffer size                           |
| `timeout_ms` | `int`          | Maximum wait time (0 = non-blocking)  |

**Return value:**

Number of bytes read (> 0), 0 if no data within timeout, or a negative
error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`

**Example:**

```c
char buf[4096];
int n = bc_pty_read(c, buf, sizeof(buf), 1000);
if (n > 0)
    fwrite(buf, 1, n, stdout);
```

---

### bc_pty_write

```c
int bc_pty_write(bc_client_t *c, const char *data, size_t len);
```

Write input to the PTY (as if typed by the user).

**Parameters:**

| Parameter | Type           | Description       |
|-----------|----------------|-------------------|
| `c`       | `bc_client_t *` | Client handle    |
| `data`    | `const char *` | Data to send      |
| `len`     | `size_t`       | Number of bytes   |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`

**Example:**

```c
bc_pty_write(c, "ls -la\n", 7);
```

---

### bc_pty_resize

```c
int bc_pty_resize(bc_client_t *c, int cols, int rows);
```

Resize the PTY terminal.

**Parameters:**

| Parameter | Type           | Description              |
|-----------|----------------|--------------------------|
| `c`       | `bc_client_t *` | Client handle           |
| `cols`    | `int`          | New width in columns     |
| `rows`    | `int`          | New height in rows       |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_pty_set_strip_ansi

```c
int bc_pty_set_strip_ansi(bc_client_t *c, int enable);
```

Enable or disable ANSI escape sequence stripping on PTY output.

When enabled, `bc_pty_read()` returns plain text with all escape
sequences removed (colors, cursor movement, etc.).

**Parameters:**

| Parameter | Type           | Description                    |
|-----------|----------------|--------------------------------|
| `c`       | `bc_client_t *` | Client handle                 |
| `enable`  | `int`          | 1 to enable, 0 to disable     |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`, `BC_ERR_SERVER`

---

### bc_pty_close

```c
int bc_pty_close(bc_client_t *c);
```

Close the PTY session. Sends SIGHUP to the child process.

**Parameters:**

| Parameter | Type           | Description   |
|-----------|----------------|---------------|
| `c`       | `bc_client_t *` | Client handle |

**Return value:** `BC_OK` on success, or an error code.

**Errors:** `BC_ERR_PARAM`, `BC_ERR_TRANSPORT`

---

### bc_pty_info_free

```c
void bc_pty_info_free(bc_pty_info_t *info);
```

Free a PTY info struct returned by `bc_pty_spawn()`.

**Parameters:**

| Parameter | Type             | Description                     |
|-----------|------------------|---------------------------------|
| `info`    | `bc_pty_info_t *` | PTY info struct, or NULL (no-op) |

---

## Utility Functions

### bc_free

```c
void bc_free(void *ptr);
```

Free memory allocated by the library. Use this instead of standard
`free()` for all strings returned by `bc_var_get()`, `bc_func_get()`,
`bc_alias_get()`, `bc_trap_get()`, `bc_inspect()`, and
`bc_debug_inspect_ast()`.

**Parameters:**

| Parameter | Type     | Description                    |
|-----------|----------|--------------------------------|
| `ptr`     | `void *` | Pointer to free, or NULL (no-op) |

---

### bc_version

```c
const char *bc_version(void);
```

Get the library version string.

**Return value:**

A pointer to a static string (e.g., `"0.1.0"`). Do NOT free. Thread-safe.

**Example:**

```c
printf("libbashclient %s\n", bc_version());
```

---

### bc_eval_result_free

```c
void bc_eval_result_free(bc_eval_result_t *r);
```

Free an eval result struct and all its fields.

**Parameters:**

| Parameter | Type                | Description                        |
|-----------|---------------------|------------------------------------|
| `r`       | `bc_eval_result_t *` | Result struct, or NULL (no-op)    |

This frees `r->stdout_data`, `r->stderr_data`, and `r` itself.

---

### bc_debug_status_free

```c
void bc_debug_status_free(bc_debug_status_t *st);
```

Free a debug status struct and all its fields.

**Parameters:**

| Parameter | Type                 | Description                        |
|-----------|----------------------|------------------------------------|
| `st`      | `bc_debug_status_t *` | Status struct, or NULL (no-op)    |

---

### bc_breakpoints_free

```c
void bc_breakpoints_free(bc_breakpoint_t *bps, int count);
```

Free an array of breakpoint descriptors returned by
`bc_debug_list_breakpoints()`.

**Parameters:**

| Parameter | Type               | Description                      |
|-----------|--------------------|----------------------------------|
| `bps`     | `bc_breakpoint_t *` | Breakpoint array, or NULL (no-op) |
| `count`   | `int`              | Number of elements in the array  |

This frees all string fields in each element, then the array itself.
