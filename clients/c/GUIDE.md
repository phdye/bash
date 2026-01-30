# libbashclient Usage Guide

A comprehensive guide to using the libbashclient C library for communicating
with bash-server. This guide covers all features of the library with
practical examples.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Core Concepts](#core-concepts)
   - [Opaque Handles](#opaque-handles)
   - [Error Codes](#error-codes)
   - [Memory Management](#memory-management)
   - [Blocking vs Event-Driven](#blocking-vs-event-driven)
3. [Getting Started](#getting-started)
   - [Your First Program](#your-first-program)
   - [Compiling and Running](#compiling-and-running)
4. [Connection and Authentication](#connection-and-authentication)
   - [Unix Socket Transport](#unix-socket-transport)
   - [Stdio Transport](#stdio-transport)
   - [File Descriptor Transport](#file-descriptor-transport)
   - [Named Pipe Transport (Cygwin)](#named-pipe-transport-cygwin)
   - [Connection Options](#connection-options)
5. [Channel: COMMAND (Evaluation)](#channel-command-evaluation)
   - [Simple Evaluation](#simple-evaluation)
   - [Multi-Line Scripts](#multi-line-scripts)
   - [Handling Exit Codes](#handling-exit-codes)
   - [Streaming Output](#streaming-output)
   - [Pre-Parsed Command Execution](#pre-parsed-command-execution)
6. [Channel: STATE (Variables and State)](#channel-state-variables-and-state)
   - [Getting Variables](#getting-variables)
   - [Setting Variables](#setting-variables)
   - [Unsetting Variables](#unsetting-variables)
   - [Array Variables](#array-variables)
   - [Functions](#functions)
   - [Aliases](#aliases)
   - [Traps](#traps)
   - [Inspecting State](#inspecting-state)
7. [Channel: OBSERVE (Event Monitoring)](#channel-observe-event-monitoring)
   - [Setting the Observe Level](#setting-the-observe-level)
   - [Pre-Command Events](#pre-command-events)
   - [Post-Command Events](#post-command-events)
   - [Combined Observer](#combined-observer)
8. [Channel: DEBUG (Debugging)](#channel-debug-debugging)
   - [Enabling the Debugger](#enabling-the-debugger)
   - [Setting Breakpoints](#setting-breakpoints)
   - [Stepping Through Code](#stepping-through-code)
   - [Inspecting the AST](#inspecting-the-ast)
   - [Conditional Breakpoints](#conditional-breakpoints)
   - [Debugger Session Example](#debugger-session-example)
9. [Channel: PTY (Pseudo-Terminal)](#channel-pty-pseudo-terminal)
   - [Spawning a PTY](#spawning-a-pty)
   - [Reading and Writing](#reading-and-writing)
   - [Resizing the Terminal](#resizing-the-terminal)
   - [ANSI Stripping](#ansi-stripping)
   - [Closing the PTY](#closing-the-pty)
10. [Channel: CONTROL](#channel-control)
    - [Ping](#ping)
    - [Configure](#configure)
    - [Disconnect](#disconnect)
11. [Polling for Events](#polling-for-events)
    - [Basic Poll Loop](#basic-poll-loop)
    - [Integrating with Event Loops](#integrating-with-event-loops)
12. [Error Handling Patterns](#error-handling-patterns)
    - [Return Code Checking](#return-code-checking)
    - [Error Messages](#error-messages)
    - [Recovery Strategies](#recovery-strategies)
13. [Memory Management Best Practices](#memory-management-best-practices)
    - [Ownership Rules](#ownership-rules)
    - [String Lifetimes](#string-lifetimes)
    - [Result Struct Cleanup](#result-struct-cleanup)
    - [Avoiding Leaks](#avoiding-leaks)
14. [Recipes](#recipes)
    - [Script Evaluator](#recipe-script-evaluator)
    - [Command Observer](#recipe-command-observer)
    - [Interactive Debugger](#recipe-interactive-debugger)
    - [PTY Terminal Emulator](#recipe-pty-terminal-emulator)

---

## Prerequisites

Before using libbashclient, ensure you have:

- A C99-compatible compiler (GCC, Clang)
- libbashclient installed (see [INSTALL.md](INSTALL.md))
- A running bash-server instance (see bash-server documentation)

Start a bash-server for testing:

```bash
bash-server --name test-session &
```

This creates a Unix socket that libbashclient connects to by default.

---

## Core Concepts

### Opaque Handles

The central type in libbashclient is `bc_client_t *`, an opaque pointer to
the client state. You obtain a handle by calling `bc_connect()` and release
it with `bc_close()`:

```c
bc_client_t *c = bc_connect(NULL, NULL);
if (!c) {
    fprintf(stderr, "Connection failed\n");
    return 1;
}

/* Use the client... */

bc_close(c);  /* Always close when done */
```

The handle is opaque -- you cannot access its fields directly. All
interaction goes through the `bc_*()` function API. This allows the
library's internal representation to change without breaking your code.

### Error Codes

Every function that can fail returns an integer error code. The codes are:

| Code                 | Value | Meaning                           |
|----------------------|-------|-----------------------------------|
| `BC_OK`              | 0     | Success                           |
| `BC_ERR_AUTH`        | -1    | Authentication failed             |
| `BC_ERR_PROTOCOL`    | -2    | Protocol error (malformed data)   |
| `BC_ERR_TIMEOUT`     | -3    | Operation timed out               |
| `BC_ERR_TRANSPORT`   | -4    | Transport error (connection lost) |
| `BC_ERR_SERVER`      | -5    | Server returned an error          |
| `BC_ERR_NOMEM`       | -6    | Memory allocation failed          |
| `BC_ERR_PARAM`       | -7    | Invalid parameter passed          |

Functions that return pointers (like `bc_connect()` and `bc_eval()`) return
`NULL` on failure. Use `bc_last_error()` to retrieve the specific error code
and `bc_last_error_msg()` for a human-readable description:

```c
bc_client_t *c = bc_connect(NULL, NULL);
if (!c) {
    fprintf(stderr, "Connect failed: %s (code %d)\n",
            bc_last_error_msg(NULL), bc_last_error(NULL));
    return 1;
}
```

When called with a valid client handle, these functions return the error
from the last operation on that client. When called with `NULL`, they
return the error from the last `bc_connect()` call.

### Memory Management

libbashclient follows strict ownership rules:

1. **Strings returned by the library** must be freed by calling `bc_free()`:

   ```c
   char *value = bc_var_get(c, "HOME");
   if (value) {
       printf("HOME=%s\n", value);
       bc_free(value);  /* Caller must free */
   }
   ```

2. **Result structs** have dedicated free functions:

   ```c
   bc_eval_result_t *r = bc_eval(c, "ls -la");
   if (r) {
       printf("%s", r->stdout_data);
       bc_eval_result_free(r);  /* Frees struct and all fields */
   }
   ```

3. **Strings passed to the library** are copied internally. You retain
   ownership and can free them immediately after the call:

   ```c
   char *cmd = strdup("echo hello");
   bc_eval_result_t *r = bc_eval(c, cmd);
   free(cmd);  /* Safe: library made its own copy */
   bc_eval_result_free(r);
   ```

4. **The client handle** is freed by `bc_close()`:

   ```c
   bc_close(c);  /* Frees all internal resources */
   c = NULL;     /* Good practice: avoid dangling pointer */
   ```

Never use standard `free()` on strings returned by the library. Always use
`bc_free()`, which ensures the correct allocator is used.

### Blocking vs Event-Driven

The libbashclient API is **synchronous and blocking**. When you call
`bc_eval()`, the function blocks until the server sends the complete
response. This makes the API simple to use in sequential code.

For server-push events (OBSERVE, DEBUG), you must call `bc_poll()` to
process incoming events and dispatch registered callbacks:

```c
/* Register a callback */
bc_on_pre_command(c, my_callback, my_userdata);

/* Poll for events (blocks up to timeout_ms) */
int n = bc_poll(c, 1000);  /* Wait up to 1 second */
/* n = number of events dispatched, 0 if none, <0 on error */
```

The library is **NOT thread-safe**. Each thread that needs a bash-server
connection must create its own `bc_client_t *` handle. Do not share a
handle across threads.

---

## Getting Started

### Your First Program

```c
/* hello.c - Minimal libbashclient example */
#include <bashclient.h>
#include <stdio.h>

int main(void)
{
    /* Connect to bash-server using default socket path */
    bc_client_t *c = bc_connect(NULL, NULL);
    if (!c) {
        fprintf(stderr, "Failed to connect: %s\n",
                bc_last_error_msg(NULL));
        return 1;
    }

    /* Evaluate a command */
    bc_eval_result_t *r = bc_eval(c, "echo 'Hello from bash-server!'");
    if (!r) {
        fprintf(stderr, "Eval failed: %s\n", bc_last_error_msg(c));
        bc_close(c);
        return 1;
    }

    /* Print the output */
    printf("stdout: %s", r->stdout_data);
    printf("stderr: %s", r->stderr_data);
    printf("exit code: %d\n", r->exit_code);

    /* Clean up */
    bc_eval_result_free(r);
    bc_close(c);
    return 0;
}
```

### Compiling and Running

```bash
# Compile
gcc -o hello hello.c -lbashclient

# Start a server (if not already running)
bash-server --name demo &

# Run
./hello
```

Expected output:

```
stdout: Hello from bash-server!
stderr:
exit code: 0
```

---

## Connection and Authentication

### Unix Socket Transport

The default transport. The library connects to a Unix domain socket,
either at a specified path or discovered automatically:

```c
/* Default socket discovery:
   1. BC_OPTS socket_path
   2. $BASH_SERVER_SOCKET environment variable
   3. ~/.bash-serverrc config file
   4. $XDG_RUNTIME_DIR/bash-server/sock
   5. /tmp/bash-server-<uid>/sock
*/
bc_client_t *c = bc_connect(NULL, NULL);

/* Explicit socket path */
bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);

/* With explicit auth token */
bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock",
                            "a3f8b2c1...");
```

Authentication is automatic when the server writes a token file and the
client has read access to it. For explicit auth, pass the token as the
second parameter.

### Stdio Transport

Connect to a bash-server that was started with `--stdio`. The library
communicates over stdin/stdout of a child process:

```c
bc_opts_t opts = BC_OPTS_INIT;
opts.transport = BC_TRANSPORT_STDIO;
opts.stdio_command = "bash-server --stdio";

bc_client_t *c = bc_connect_opts(&opts);
if (!c) {
    fprintf(stderr, "Stdio connect failed: %s\n",
            bc_last_error_msg(NULL));
    return 1;
}

/* Use normally -- the library manages the child process */
bc_eval_result_t *r = bc_eval(c, "echo hello");
bc_eval_result_free(r);

bc_close(c);  /* Terminates the child process */
```

### File Descriptor Transport

Connect using pre-opened file descriptors. Useful when another process
sets up the connection:

```c
bc_opts_t opts = BC_OPTS_INIT;
opts.transport = BC_TRANSPORT_FD;
opts.fd_read = read_fd;   /* FD to read from */
opts.fd_write = write_fd; /* FD to write to */

bc_client_t *c = bc_connect_opts(&opts);
```

### Named Pipe Transport (Cygwin)

On Cygwin, connect via a Windows Named Pipe for better Windows
interoperability:

```c
bc_opts_t opts = BC_OPTS_INIT;
opts.transport = BC_TRANSPORT_PIPE;
opts.pipe_name = "\\\\.\\pipe\\bash-server-demo";

bc_client_t *c = bc_connect_opts(&opts);
```

### Connection Options

The `bc_opts_t` struct provides full control over connection behavior:

```c
bc_opts_t opts = BC_OPTS_INIT;  /* Always initialize with this macro */

/* Transport selection */
opts.transport = BC_TRANSPORT_SOCKET;  /* Default */
opts.socket_path = "/custom/path/sock";
opts.auth_token = "token-string";

/* Timeouts (milliseconds) */
opts.connect_timeout_ms = 5000;   /* Connection timeout (default: 10000) */
opts.read_timeout_ms = 30000;     /* Read timeout (default: 60000) */
opts.write_timeout_ms = 10000;    /* Write timeout (default: 30000) */

/* Protocol options */
opts.protocol_version = 2;        /* Request v2 (default: auto-detect) */
opts.wire_format = BC_WIRE_NDJSON; /* NDJSON framing (default: binary) */

bc_client_t *c = bc_connect_opts(&opts);
```

---

## Channel: COMMAND (Evaluation)

The COMMAND channel (channel 1) executes shell commands and returns their
output, stderr, and exit code.

### Simple Evaluation

```c
bc_eval_result_t *r = bc_eval(c, "uname -a");
if (!r) {
    fprintf(stderr, "Eval failed: %s\n", bc_last_error_msg(c));
    return;
}

printf("Output: %s", r->stdout_data);
if (r->stderr_data[0] != '\0') {
    fprintf(stderr, "Errors: %s", r->stderr_data);
}
printf("Exit code: %d\n", r->exit_code);

bc_eval_result_free(r);
```

### Multi-Line Scripts

Pass multi-line scripts as a single string:

```c
const char *script =
    "for i in 1 2 3; do\n"
    "    echo \"Number: $i\"\n"
    "done\n";

bc_eval_result_t *r = bc_eval(c, script);
if (r) {
    printf("%s", r->stdout_data);
    bc_eval_result_free(r);
}
```

Output:

```
Number: 1
Number: 2
Number: 3
```

### Handling Exit Codes

The exit code of the last command in the evaluated string is captured:

```c
bc_eval_result_t *r;

r = bc_eval(c, "true");
printf("true: exit %d\n", r->exit_code);   /* 0 */
bc_eval_result_free(r);

r = bc_eval(c, "false");
printf("false: exit %d\n", r->exit_code);  /* 1 */
bc_eval_result_free(r);

r = bc_eval(c, "exit 42");
printf("exit 42: exit %d\n", r->exit_code); /* 42 */
bc_eval_result_free(r);

/* Commands that fail */
r = bc_eval(c, "ls /nonexistent");
printf("ls bad: exit %d\n", r->exit_code);  /* 2 */
printf("stderr: %s", r->stderr_data);
bc_eval_result_free(r);
```

### Streaming Output

For long-running commands, use the streaming evaluation API to receive
output incrementally via callbacks:

```c
static void on_stdout(const char *data, size_t len, void *userdata)
{
    fwrite(data, 1, len, stdout);
    fflush(stdout);
}

static void on_stderr(const char *data, size_t len, void *userdata)
{
    fwrite(data, 1, len, stderr);
}

static void on_complete(int exit_code, void *userdata)
{
    int *done = (int *)userdata;
    printf("\nCommand finished with exit code %d\n", exit_code);
    *done = 1;
}

/* Start streaming eval */
int done = 0;
int rc = bc_eval_stream(c, "for i in $(seq 1 10); do echo $i; sleep 0.1; done",
                        on_stdout, on_stderr, on_complete, &done);
if (rc != BC_OK) {
    fprintf(stderr, "Stream eval failed: %s\n", bc_last_error_msg(c));
    return;
}

/* Poll until complete */
while (!done) {
    bc_poll(c, 100);
}
```

### Pre-Parsed Command Execution

If you have a JSON-serialized COMMAND tree (from the debug or observe
channels), you can execute it directly without re-parsing:

```c
const char *cmd_json =
    "{\"type\":\"simple\","
    " \"words\":[{\"word\":\"echo\"},{\"word\":\"hello\"}],"
    " \"redirects\":[]}";

bc_eval_result_t *r = bc_eval_parsed(c, cmd_json);
if (r) {
    printf("%s", r->stdout_data);  /* "hello\n" */
    bc_eval_result_free(r);
}
```

---

## Channel: STATE (Variables and State)

The STATE channel (channel 2) provides direct access to the server's shell
state: variables, functions, aliases, and traps.

### Getting Variables

```c
/* Get a simple variable */
char *home = bc_var_get(c, "HOME");
if (home) {
    printf("HOME=%s\n", home);
    bc_free(home);
}

/* Check if a variable exists */
char *val = bc_var_get(c, "NONEXISTENT");
if (!val) {
    printf("Variable not set\n");
}
```

### Setting Variables

```c
/* Set a string variable */
int rc = bc_var_set(c, "MY_VAR", "hello world");
if (rc != BC_OK) {
    fprintf(stderr, "Set failed: %s\n", bc_last_error_msg(c));
}

/* Verify it was set */
char *val = bc_var_get(c, "MY_VAR");
printf("MY_VAR=%s\n", val);  /* "hello world" */
bc_free(val);

/* Set with attributes (e.g., export, readonly, integer) */
rc = bc_var_set_attr(c, "MY_INT", "42", "i");  /* integer attribute */
if (rc != BC_OK) {
    fprintf(stderr, "Set with attr failed: %s\n", bc_last_error_msg(c));
}
```

### Unsetting Variables

```c
int rc = bc_var_unset(c, "MY_VAR");
if (rc != BC_OK) {
    fprintf(stderr, "Unset failed: %s\n", bc_last_error_msg(c));
}

/* Verify it was removed */
char *val = bc_var_get(c, "MY_VAR");
/* val is NULL */
```

### Array Variables

```c
/* Set array elements by evaluating shell syntax */
bc_eval(c, "my_array=(one two three)");

/* Get array values via state channel */
char *val = bc_var_get(c, "my_array[1]");
if (val) {
    printf("my_array[1]=%s\n", val);  /* "two" */
    bc_free(val);
}

/* Get entire array as space-separated string */
val = bc_var_get(c, "my_array[*]");
if (val) {
    printf("my_array=(%s)\n", val);  /* "one two three" */
    bc_free(val);
}
```

### Functions

```c
/* Define a function */
int rc = bc_func_set(c, "greet", "echo \"Hello, $1!\"");
if (rc != BC_OK) {
    fprintf(stderr, "Function set failed: %s\n", bc_last_error_msg(c));
}

/* Get a function's body */
char *body = bc_func_get(c, "greet");
if (body) {
    printf("greet() { %s; }\n", body);
    bc_free(body);
}

/* Call the function */
bc_eval_result_t *r = bc_eval(c, "greet World");
if (r) {
    printf("%s", r->stdout_data);  /* "Hello, World!\n" */
    bc_eval_result_free(r);
}

/* Remove a function */
rc = bc_func_unset(c, "greet");
```

### Aliases

```c
/* Set an alias */
int rc = bc_alias_set(c, "ll", "ls -la");
if (rc != BC_OK) {
    fprintf(stderr, "Alias set failed: %s\n", bc_last_error_msg(c));
}

/* Get an alias */
char *expansion = bc_alias_get(c, "ll");
if (expansion) {
    printf("alias ll='%s'\n", expansion);  /* "ls -la" */
    bc_free(expansion);
}

/* Remove an alias */
rc = bc_alias_unset(c, "ll");
```

### Traps

```c
/* Set a trap */
int rc = bc_trap_set(c, "SIGINT", "echo 'caught interrupt'");
if (rc != BC_OK) {
    fprintf(stderr, "Trap set failed: %s\n", bc_last_error_msg(c));
}

/* Get a trap */
char *action = bc_trap_get(c, "SIGINT");
if (action) {
    printf("trap -- '%s' SIGINT\n", action);
    bc_free(action);
}

/* Remove a trap */
rc = bc_trap_unset(c, "SIGINT");
```

### Inspecting State

Get a summary of all state in a particular namespace:

```c
/* Inspect all variables */
char *json = bc_inspect(c, "variables");
if (json) {
    printf("Variables:\n%s\n", json);
    bc_free(json);
}

/* Inspect functions */
json = bc_inspect(c, "functions");
if (json) {
    printf("Functions:\n%s\n", json);
    bc_free(json);
}

/* Inspect aliases */
json = bc_inspect(c, "aliases");

/* Inspect traps */
json = bc_inspect(c, "traps");
```

The returned JSON contains all entries in the namespace with their values
and attributes.

---

## Channel: OBSERVE (Event Monitoring)

The OBSERVE channel (channel 3) lets you subscribe to command execution
events. Events are delivered asynchronously via registered callbacks; you
must call `bc_poll()` to dispatch them.

### Setting the Observe Level

```c
/* Level 0: No events (default) */
int rc = bc_observe_set_level(c, 0);

/* Level 1: Pre-command events only */
rc = bc_observe_set_level(c, 1);

/* Level 2: Pre and post-command events */
rc = bc_observe_set_level(c, 2);
```

### Pre-Command Events

Fired before a command executes:

```c
static void on_pre_command(const bc_pre_command_event_t *ev, void *userdata)
{
    printf("[PRE] seq=%d cmd='%s' cwd='%s' line=%d",
           ev->seq, ev->command, ev->cwd, ev->line_number);
    if (ev->is_subshell)
        printf(" (subshell)");
    if (ev->is_async)
        printf(" (async)");
    printf("\n");
}

/* Register the callback */
bc_on_pre_command(c, on_pre_command, NULL);

/* Enable observation */
bc_observe_set_level(c, 2);

/* Events arrive when you poll */
bc_eval(c, "echo hello && echo world");

/* Poll to dispatch pending events */
while (bc_poll(c, 100) > 0)
    ;
```

### Post-Command Events

Fired after a command completes:

```c
static void on_post_command(const bc_post_command_event_t *ev,
                            void *userdata)
{
    printf("[POST] seq=%d cmd='%s' exit=%d duration=%dms",
           ev->seq, ev->command, ev->exit_status, ev->duration_ms);
    if (ev->signal_number)
        printf(" signal=%d", ev->signal_number);
    printf("\n");
}

bc_on_post_command(c, on_post_command, NULL);
bc_observe_set_level(c, 2);
```

### Combined Observer

```c
static void on_pre(const bc_pre_command_event_t *ev, void *userdata)
{
    int *count = (int *)userdata;
    (*count)++;
    printf(">>> [%d] %s\n", ev->seq, ev->command);
}

static void on_post(const bc_post_command_event_t *ev, void *userdata)
{
    printf("<<< [%d] %s (exit %d, %dms)\n",
           ev->seq, ev->command, ev->exit_status, ev->duration_ms);
}

int cmd_count = 0;
bc_on_pre_command(c, on_pre, &cmd_count);
bc_on_post_command(c, on_post, NULL);
bc_observe_set_level(c, 2);

/* Run some commands */
bc_eval_result_t *r = bc_eval(c, "ls /tmp && date");
bc_eval_result_free(r);

/* Drain all events */
while (bc_poll(c, 100) > 0)
    ;

printf("Total commands observed: %d\n", cmd_count);
```

### Unregistering Callbacks

```c
/* Pass NULL to remove a callback */
bc_on_pre_command(c, NULL, NULL);
bc_on_post_command(c, NULL, NULL);

/* Or disable observation entirely */
bc_observe_set_level(c, 0);
```

---

## Channel: DEBUG (Debugging)

The DEBUG channel (channel 4) provides breakpoints, stepping, and AST
inspection for shell scripts.

### Enabling the Debugger

```c
int rc = bc_debug_enable(c);
if (rc != BC_OK) {
    fprintf(stderr, "Debug enable failed: %s\n", bc_last_error_msg(c));
    return;
}

/* Check debugger status */
bc_debug_status_t *st = bc_debug_status(c);
if (st) {
    printf("Debugger active: %d\n", st->active);
    printf("Mode: %s\n", st->mode);
    printf("Breakpoints: %d\n", st->breakpoints);
    bc_debug_status_free(st);
}
```

### Setting Breakpoints

```c
/* Break on a command pattern (glob match) */
int bp_id = bc_debug_break_command(c, "rm *");
if (bp_id < 0) {
    fprintf(stderr, "Break set failed: %s\n", bc_last_error_msg(c));
}
printf("Breakpoint %d set on command 'rm *'\n", bp_id);

/* Break on a specific line number */
bp_id = bc_debug_break_line(c, 10);
printf("Breakpoint %d set on line 10\n", bp_id);

/* Break on function entry */
bp_id = bc_debug_break_function(c, "my_func");
printf("Breakpoint %d set on function 'my_func'\n", bp_id);

/* List all breakpoints */
bc_breakpoint_t *bps = NULL;
int count = 0;
int rc = bc_debug_list_breakpoints(c, &bps, &count);
if (rc == BC_OK) {
    for (int i = 0; i < count; i++) {
        printf("  bp %d: kind=%s pattern='%s' enabled=%d hits=%d\n",
               bps[i].id, bps[i].kind, bps[i].pattern,
               bps[i].enabled, bps[i].hit_count);
    }
    bc_breakpoints_free(bps, count);
}

/* Enable/disable a breakpoint */
bc_debug_enable_breakpoint(c, bp_id, 0);  /* Disable */
bc_debug_enable_breakpoint(c, bp_id, 1);  /* Enable */

/* Remove a breakpoint */
bc_debug_remove_breakpoint(c, bp_id);
```

### Stepping Through Code

When a breakpoint is hit, you can step through execution:

```c
static void on_break_hit(const bc_break_hit_event_t *ev, void *userdata)
{
    printf("BREAK at line %d: %s (depth %d)\n",
           ev->line, ev->command, ev->depth);
    int *hit = (int *)userdata;
    *hit = 1;
}

int hit = 0;
bc_on_break_hit(c, on_break_hit, &hit);

/* Step over: execute current command, stop at next */
int rc = bc_debug_step(c);

/* Step into: enter function calls */
rc = bc_debug_step_into(c);

/* Step out: finish current function, stop at caller */
rc = bc_debug_step_out(c);

/* Continue: run until next breakpoint */
rc = bc_debug_continue(c);

/* Skip: skip current command without executing it */
rc = bc_debug_skip(c);
```

### Inspecting the AST

When stopped at a breakpoint, inspect the current command's AST:

```c
char *ast_json = bc_debug_inspect_ast(c);
if (ast_json) {
    printf("Current AST:\n%s\n", ast_json);
    bc_free(ast_json);
}
```

The returned JSON represents the COMMAND tree structure. See the
bash-server protocol documentation for the full AST schema.

### Conditional Breakpoints

Set a breakpoint that only triggers when a condition is true:

```c
/* Break on line 5 only when x > 10 */
int bp_id = bc_debug_break_line(c, 5);
int rc = bc_debug_set_condition(c, bp_id, "[ $x -gt 10 ]");
if (rc != BC_OK) {
    fprintf(stderr, "Condition set failed: %s\n", bc_last_error_msg(c));
}
```

### Debugger Session Example

A complete debugger interaction:

```c
/* Enable debugging */
bc_debug_enable(c);

/* Set a breakpoint */
int bp = bc_debug_break_line(c, 3);

/* Prepare to receive break events */
int stopped = 0;
bc_on_break_hit(c, on_break_hit, &stopped);

/* Evaluate a script -- execution will pause at line 3 */
const char *script =
    "x=1\n"
    "x=$((x + 1))\n"
    "echo $x\n"        /* Line 3: breakpoint here */
    "x=$((x + 1))\n"
    "echo $x\n";

/* Start evaluation (non-blocking when debugger is active) */
bc_eval_stream(c, script, NULL, NULL, NULL, NULL);

/* Wait for breakpoint hit */
while (!stopped) {
    bc_poll(c, 100);
}

/* Inspect state at the breakpoint */
char *val = bc_var_get(c, "x");
printf("x = %s\n", val);  /* "2" */
bc_free(val);

/* Inspect the AST */
char *ast = bc_debug_inspect_ast(c);
printf("AST: %s\n", ast);
bc_free(ast);

/* Continue execution */
bc_debug_continue(c);

/* Drain remaining events */
while (bc_poll(c, 100) > 0)
    ;

/* Clean up */
bc_debug_disable(c);
```

---

## Channel: PTY (Pseudo-Terminal)

The PTY channel (channel 5) provides a pseudo-terminal for interactive
shell sessions.

### Spawning a PTY

```c
bc_pty_info_t *info = bc_pty_spawn(c, 80, 24, NULL);
if (!info) {
    fprintf(stderr, "PTY spawn failed: %s\n", bc_last_error_msg(c));
    return;
}

printf("PTY spawned: %dx%d, pid=%d\n",
       info->cols, info->rows, info->pid);
bc_pty_info_free(info);
```

Optionally specify an initial command:

```c
/* Spawn with a specific shell */
bc_pty_info_t *info = bc_pty_spawn(c, 120, 40, "/bin/zsh");
```

### Reading and Writing

```c
/* Write to the PTY (as if the user typed it) */
int rc = bc_pty_write(c, "ls -la\n", 7);
if (rc != BC_OK) {
    fprintf(stderr, "PTY write failed: %s\n", bc_last_error_msg(c));
}

/* Read from the PTY (output from the shell) */
char buf[4096];
int n = bc_pty_read(c, buf, sizeof(buf), 1000);  /* 1s timeout */
if (n > 0) {
    fwrite(buf, 1, n, stdout);
} else if (n == 0) {
    printf("(no output within timeout)\n");
} else {
    fprintf(stderr, "PTY read error: %s\n", bc_last_error_msg(c));
}
```

### Resizing the Terminal

```c
int rc = bc_pty_resize(c, 132, 50);  /* 132 columns, 50 rows */
if (rc != BC_OK) {
    fprintf(stderr, "Resize failed: %s\n", bc_last_error_msg(c));
}
```

### ANSI Stripping

Enable ANSI escape sequence stripping to get plain text output:

```c
int rc = bc_pty_set_strip_ansi(c, 1);  /* Enable stripping */

/* Now reads return plain text without color codes, cursor
   movement, or other terminal escape sequences */
char buf[4096];
int n = bc_pty_read(c, buf, sizeof(buf), 1000);
/* buf contains plain text only */

/* Disable stripping */
bc_pty_set_strip_ansi(c, 0);
```

### Closing the PTY

```c
int rc = bc_pty_close(c);
if (rc != BC_OK) {
    fprintf(stderr, "PTY close failed: %s\n", bc_last_error_msg(c));
}
```

Closing the PTY sends SIGHUP to the child process. The PTY channel
returns to its initial state and a new PTY can be spawned.

---

## Channel: CONTROL

The CONTROL channel (channel 0) handles connection management.

### Ping

Test that the server is responsive:

```c
int rc = bc_ping(c);
if (rc == BC_OK) {
    printf("Server is alive\n");
} else {
    fprintf(stderr, "Ping failed: %s\n", bc_last_error_msg(c));
}
```

### Configure

Adjust session settings:

```c
/* Switch to NDJSON wire format */
int rc = bc_configure(c, "wire_format", "ndjson");

/* Set observe level via control channel */
rc = bc_configure(c, "observe_level", "2");
```

### Disconnect

Gracefully disconnect from the server:

```c
int rc = bc_disconnect(c);
/* After disconnect, the handle is still valid but operations will
   return BC_ERR_TRANSPORT. Call bc_close() to free the handle. */
bc_close(c);
```

---

## Polling for Events

Server-push events (from OBSERVE and DEBUG channels) are buffered by the
library until you call `bc_poll()`.

### Basic Poll Loop

```c
/* Register callbacks first */
bc_on_pre_command(c, my_pre_cb, NULL);
bc_on_post_command(c, my_post_cb, NULL);
bc_on_break_hit(c, my_break_cb, NULL);
bc_observe_set_level(c, 2);

/* Poll loop */
while (running) {
    int n = bc_poll(c, 500);  /* 500ms timeout */
    if (n < 0) {
        fprintf(stderr, "Poll error: %s\n", bc_last_error_msg(c));
        break;
    }
    /* n = number of events dispatched */
}
```

### Integrating with Event Loops

To integrate with external event loops (select/poll/epoll), obtain the
underlying file descriptor:

```c
int fd = bc_get_fd(c);
if (fd < 0) {
    fprintf(stderr, "Cannot get fd (not a socket transport)\n");
    return;
}

/* Use with poll() */
struct pollfd pfd = { .fd = fd, .events = POLLIN };

while (running) {
    int ret = poll(&pfd, 1, 500);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        /* Data available -- dispatch events */
        bc_poll(c, 0);  /* Non-blocking: process buffered data */
    }
}
```

This works with `select()`, `epoll`, `kqueue`, or any event loop that
accepts file descriptors.

---

## Error Handling Patterns

### Return Code Checking

The recommended pattern is to check every return value:

```c
bc_client_t *c = bc_connect(NULL, NULL);
if (!c) {
    fprintf(stderr, "Connect: %s\n", bc_last_error_msg(NULL));
    return 1;
}

bc_eval_result_t *r = bc_eval(c, "echo test");
if (!r) {
    fprintf(stderr, "Eval: %s\n", bc_last_error_msg(c));
    bc_close(c);
    return 1;
}

bc_eval_result_free(r);
bc_close(c);
return 0;
```

### Error Messages

`bc_last_error_msg()` returns a human-readable string describing the most
recent error. The string is owned by the library and valid until the next
operation on the same handle (or the next `bc_connect()` for NULL handles):

```c
const char *msg = bc_last_error_msg(c);
/* msg is valid until the next bc_*() call on c */
printf("Error: %s\n", msg);
```

The error code from `bc_last_error()` is useful for programmatic error
handling:

```c
int err = bc_last_error(c);
switch (err) {
case BC_ERR_AUTH:
    fprintf(stderr, "Authentication failed. Check token.\n");
    break;
case BC_ERR_TIMEOUT:
    fprintf(stderr, "Operation timed out. Server may be busy.\n");
    break;
case BC_ERR_TRANSPORT:
    fprintf(stderr, "Connection lost. Reconnecting...\n");
    bc_close(c);
    c = bc_connect(NULL, NULL);
    break;
case BC_ERR_PROTOCOL:
    fprintf(stderr, "Protocol error. Server version mismatch?\n");
    break;
default:
    fprintf(stderr, "Error %d: %s\n", err, bc_last_error_msg(c));
}
```

### Recovery Strategies

**Reconnection on transport error:**

```c
bc_eval_result_t *safe_eval(bc_client_t **c, const char *cmd)
{
    bc_eval_result_t *r = bc_eval(*c, cmd);
    if (!r && bc_last_error(*c) == BC_ERR_TRANSPORT) {
        /* Connection lost -- try to reconnect */
        bc_close(*c);
        *c = bc_connect(NULL, NULL);
        if (*c) {
            r = bc_eval(*c, cmd);
        }
    }
    return r;
}
```

**Timeout with retry:**

```c
bc_eval_result_t *eval_with_retry(bc_client_t *c, const char *cmd,
                                  int max_retries)
{
    for (int i = 0; i <= max_retries; i++) {
        bc_eval_result_t *r = bc_eval(c, cmd);
        if (r)
            return r;
        if (bc_last_error(c) != BC_ERR_TIMEOUT)
            break;
        fprintf(stderr, "Timeout, retry %d/%d...\n", i + 1, max_retries);
    }
    return NULL;
}
```

---

## Memory Management Best Practices

### Ownership Rules

The library uses a clear ownership model:

| Function Return     | Owner  | Free With                         |
|---------------------|--------|-----------------------------------|
| `bc_connect()`      | Caller | `bc_close(c)`                     |
| `bc_eval()`         | Caller | `bc_eval_result_free(r)`          |
| `bc_var_get()`      | Caller | `bc_free(str)`                    |
| `bc_func_get()`     | Caller | `bc_free(str)`                    |
| `bc_alias_get()`    | Caller | `bc_free(str)`                    |
| `bc_trap_get()`     | Caller | `bc_free(str)`                    |
| `bc_inspect()`      | Caller | `bc_free(str)`                    |
| `bc_debug_inspect_ast()` | Caller | `bc_free(str)`               |
| `bc_debug_status()` | Caller | `bc_debug_status_free(st)`        |
| `bc_debug_list_breakpoints()` | Caller | `bc_breakpoints_free(bps, n)` |
| `bc_pty_spawn()`    | Caller | `bc_pty_info_free(info)`          |
| `bc_last_error_msg()`| Library | Do not free                      |
| `bc_version()`      | Library | Do not free                       |

### String Lifetimes

```c
/* CORRECT: use the string, then free it */
char *val = bc_var_get(c, "PATH");
printf("PATH=%s\n", val);
bc_free(val);

/* CORRECT: copy if you need it longer */
char *val = bc_var_get(c, "HOME");
char *home = strdup(val);
bc_free(val);
/* ... use home later ... */
free(home);

/* WRONG: using after free */
char *val = bc_var_get(c, "HOME");
bc_free(val);
printf("HOME=%s\n", val);  /* BUG: use after free */

/* WRONG: using standard free */
char *val = bc_var_get(c, "HOME");
free(val);  /* BUG: wrong allocator */
```

### Result Struct Cleanup

Result structs contain multiple allocated fields. The free functions handle
all of them:

```c
/* bc_eval_result_free() frees:
   - r->stdout_data
   - r->stderr_data
   - r itself
*/
bc_eval_result_t *r = bc_eval(c, "echo test");
/* DO NOT free individual fields: */
/* bc_free(r->stdout_data);   <-- WRONG */
/* Just free the whole struct: */
bc_eval_result_free(r);
```

### Avoiding Leaks

Use a goto-based cleanup pattern for functions with multiple allocations:

```c
int process_vars(bc_client_t *c)
{
    int rc = -1;
    char *home = NULL;
    char *path = NULL;
    char *user = NULL;

    home = bc_var_get(c, "HOME");
    if (!home) goto cleanup;

    path = bc_var_get(c, "PATH");
    if (!path) goto cleanup;

    user = bc_var_get(c, "USER");
    if (!user) goto cleanup;

    printf("User %s at %s\n", user, home);
    rc = 0;

cleanup:
    if (home) bc_free(home);
    if (path) bc_free(path);
    if (user) bc_free(user);
    return rc;
}
```

---

## Recipes

### Recipe: Script Evaluator

A program that reads a shell script from a file and evaluates it line by
line, printing output as it goes:

```c
/* eval_script.c - Line-by-line script evaluator */
#include <bashclient.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <script-file>\n", argv[0]);
        return 1;
    }

    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        perror("fopen");
        return 1;
    }

    bc_client_t *c = bc_connect(NULL, NULL);
    if (!c) {
        fprintf(stderr, "Connect failed: %s\n", bc_last_error_msg(NULL));
        fclose(fp);
        return 1;
    }

    char line[4096];
    int lineno = 0;
    int errors = 0;

    while (fgets(line, sizeof(line), fp)) {
        lineno++;

        /* Skip empty lines and comments */
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\n' || *p == '#')
            continue;

        /* Remove trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n')
            line[len - 1] = '\0';

        printf("[%d] $ %s\n", lineno, line);

        bc_eval_result_t *r = bc_eval(c, line);
        if (!r) {
            fprintf(stderr, "[%d] EVAL ERROR: %s\n",
                    lineno, bc_last_error_msg(c));
            errors++;
            continue;
        }

        if (r->stdout_data[0])
            printf("%s", r->stdout_data);
        if (r->stderr_data[0])
            fprintf(stderr, "%s", r->stderr_data);
        if (r->exit_code != 0) {
            printf("[%d] exit code: %d\n", lineno, r->exit_code);
            errors++;
        }

        bc_eval_result_free(r);
    }

    printf("\n--- %d lines executed, %d errors ---\n", lineno, errors);

    fclose(fp);
    bc_close(c);
    return errors > 0 ? 1 : 0;
}
```

Build and run:

```bash
gcc -o eval_script eval_script.c -lbashclient
./eval_script myscript.sh
```

### Recipe: Command Observer

A monitoring tool that logs all commands executed in a bash-server session:

```c
/* observer.c - Command execution monitor */
#include <bashclient.h>
#include <stdio.h>
#include <signal.h>
#include <time.h>

static volatile int running = 1;

static void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
}

static void on_pre(const bc_pre_command_event_t *ev, void *userdata)
{
    FILE *log = (FILE *)userdata;
    time_t t = ev->timestamp / 1000;
    struct tm *tm = localtime(&t);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    fprintf(log, "%s PRE  [%d] %s (cwd=%s",
            ts, ev->seq, ev->command, ev->cwd);
    if (ev->is_subshell) fprintf(log, " subshell");
    if (ev->is_async) fprintf(log, " async");
    fprintf(log, ")\n");
    fflush(log);
}

static void on_post(const bc_post_command_event_t *ev, void *userdata)
{
    FILE *log = (FILE *)userdata;
    time_t t = ev->timestamp / 1000;
    struct tm *tm = localtime(&t);
    char ts[32];
    strftime(ts, sizeof(ts), "%H:%M:%S", tm);

    fprintf(log, "%s POST [%d] %s exit=%d %dms",
            ts, ev->seq, ev->command, ev->exit_status,
            ev->duration_ms);
    if (ev->signal_number)
        fprintf(log, " signal=%d", ev->signal_number);
    fprintf(log, "\n");
    fflush(log);
}

int main(int argc, char *argv[])
{
    const char *socket_path = argc > 1 ? argv[1] : NULL;
    FILE *log = argc > 2 ? fopen(argv[2], "a") : stdout;
    if (!log) {
        perror("fopen log");
        return 1;
    }

    signal(SIGINT, sigint_handler);

    bc_client_t *c = bc_connect(socket_path, NULL);
    if (!c) {
        fprintf(stderr, "Connect failed: %s\n", bc_last_error_msg(NULL));
        return 1;
    }

    bc_on_pre_command(c, on_pre, log);
    bc_on_post_command(c, on_post, log);
    bc_observe_set_level(c, 2);

    fprintf(log, "--- Observer started ---\n");
    fflush(log);

    while (running) {
        int n = bc_poll(c, 1000);
        if (n < 0) {
            fprintf(stderr, "Poll error: %s\n", bc_last_error_msg(c));
            break;
        }
    }

    fprintf(log, "--- Observer stopped ---\n");
    bc_observe_set_level(c, 0);
    bc_close(c);
    if (log != stdout) fclose(log);
    return 0;
}
```

Build and run:

```bash
gcc -o observer observer.c -lbashclient
./observer /tmp/bash-server-1000/sock observer.log
```

### Recipe: Interactive Debugger

A simple command-line debugger for shell scripts:

```c
/* debugger.c - Interactive shell script debugger */
#include <bashclient.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int stopped = 0;
static bc_break_hit_event_t last_hit;

static void on_break(const bc_break_hit_event_t *ev, void *userdata)
{
    (void)userdata;
    printf("\n==> Stopped at line %d: %s (depth %d)\n",
           ev->line, ev->command, ev->depth);
    stopped = 1;
}

static void show_help(void)
{
    printf("Commands:\n"
           "  b <line>    Set breakpoint at line\n"
           "  bf <name>   Set breakpoint on function\n"
           "  bc <pat>    Set breakpoint on command pattern\n"
           "  d <id>      Delete breakpoint\n"
           "  bl          List breakpoints\n"
           "  s           Step (next command)\n"
           "  si          Step into function\n"
           "  so          Step out of function\n"
           "  c           Continue\n"
           "  sk          Skip current command\n"
           "  p <var>     Print variable\n"
           "  ast         Show current AST\n"
           "  st          Show debug status\n"
           "  q           Quit\n");
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <script> [socket]\n", argv[0]);
        return 1;
    }

    const char *socket_path = argc > 2 ? argv[2] : NULL;
    bc_client_t *c = bc_connect(socket_path, NULL);
    if (!c) {
        fprintf(stderr, "Connect failed: %s\n", bc_last_error_msg(NULL));
        return 1;
    }

    /* Enable debugger and set initial breakpoint at line 1 */
    bc_debug_enable(c);
    bc_debug_break_line(c, 1);
    bc_on_break_hit(c, on_break, NULL);

    /* Load and start the script */
    char cmd[8192];
    snprintf(cmd, sizeof(cmd), "source '%s'", argv[1]);
    bc_eval_stream(c, cmd, NULL, NULL, NULL, NULL);

    /* Wait for first breakpoint */
    while (!stopped) {
        bc_poll(c, 100);
    }

    /* Interactive debug loop */
    char input[256];
    for (;;) {
        printf("(debug) ");
        fflush(stdout);
        if (!fgets(input, sizeof(input), stdin))
            break;

        /* Strip newline */
        size_t len = strlen(input);
        if (len > 0 && input[len - 1] == '\n')
            input[len - 1] = '\0';

        if (strcmp(input, "q") == 0) {
            break;
        } else if (strcmp(input, "h") == 0 || strcmp(input, "help") == 0) {
            show_help();
        } else if (strcmp(input, "s") == 0) {
            stopped = 0;
            bc_debug_step(c);
            while (!stopped) bc_poll(c, 100);
        } else if (strcmp(input, "si") == 0) {
            stopped = 0;
            bc_debug_step_into(c);
            while (!stopped) bc_poll(c, 100);
        } else if (strcmp(input, "so") == 0) {
            stopped = 0;
            bc_debug_step_out(c);
            while (!stopped) bc_poll(c, 100);
        } else if (strcmp(input, "c") == 0) {
            stopped = 0;
            bc_debug_continue(c);
            while (!stopped) {
                if (bc_poll(c, 100) < 0) break;
            }
            if (!stopped)
                printf("Script completed.\n");
        } else if (strcmp(input, "sk") == 0) {
            stopped = 0;
            bc_debug_skip(c);
            while (!stopped) bc_poll(c, 100);
        } else if (strncmp(input, "b ", 2) == 0) {
            int line = atoi(input + 2);
            int id = bc_debug_break_line(c, line);
            if (id >= 0) printf("Breakpoint %d at line %d\n", id, line);
            else printf("Failed to set breakpoint\n");
        } else if (strncmp(input, "bf ", 3) == 0) {
            int id = bc_debug_break_function(c, input + 3);
            if (id >= 0) printf("Breakpoint %d on '%s'\n", id, input + 3);
            else printf("Failed to set breakpoint\n");
        } else if (strncmp(input, "bc ", 3) == 0) {
            int id = bc_debug_break_command(c, input + 3);
            if (id >= 0) printf("Breakpoint %d on '%s'\n", id, input + 3);
            else printf("Failed to set breakpoint\n");
        } else if (strncmp(input, "d ", 2) == 0) {
            int id = atoi(input + 2);
            bc_debug_remove_breakpoint(c, id);
            printf("Breakpoint %d deleted\n", id);
        } else if (strcmp(input, "bl") == 0) {
            bc_breakpoint_t *bps = NULL;
            int count = 0;
            if (bc_debug_list_breakpoints(c, &bps, &count) == BC_OK) {
                for (int i = 0; i < count; i++) {
                    printf("  %d: %s '%s' %s hits=%d\n",
                           bps[i].id, bps[i].kind, bps[i].pattern,
                           bps[i].enabled ? "enabled" : "disabled",
                           bps[i].hit_count);
                }
                bc_breakpoints_free(bps, count);
            }
        } else if (strncmp(input, "p ", 2) == 0) {
            char *val = bc_var_get(c, input + 2);
            if (val) {
                printf("%s = %s\n", input + 2, val);
                bc_free(val);
            } else {
                printf("%s: not set\n", input + 2);
            }
        } else if (strcmp(input, "ast") == 0) {
            char *ast = bc_debug_inspect_ast(c);
            if (ast) {
                printf("%s\n", ast);
                bc_free(ast);
            }
        } else if (strcmp(input, "st") == 0) {
            bc_debug_status_t *st = bc_debug_status(c);
            if (st) {
                printf("active=%d mode=%s bps=%d depth=%d\n",
                       st->active, st->mode, st->breakpoints, st->depth);
                bc_debug_status_free(st);
            }
        } else if (input[0] != '\0') {
            printf("Unknown command. Type 'h' for help.\n");
        }
    }

    bc_debug_disable(c);
    bc_close(c);
    return 0;
}
```

Build and run:

```bash
gcc -o debugger debugger.c -lbashclient
./debugger myscript.sh
```

### Recipe: PTY Terminal Emulator

A minimal terminal emulator that connects to a bash-server PTY:

```c
/* pty_term.c - Minimal PTY terminal */
#include <bashclient.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <poll.h>

static volatile int running = 1;
static bc_client_t *client = NULL;

static void sigint_handler(int sig)
{
    (void)sig;
    running = 0;
}

static void sigwinch_handler(int sig)
{
    (void)sig;
    /* Terminal resized -- forward new size to the PTY */
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0 && client) {
        bc_pty_resize(client, ws.ws_col, ws.ws_row);
    }
}

int main(int argc, char *argv[])
{
    const char *socket_path = argc > 1 ? argv[1] : NULL;

    client = bc_connect(socket_path, NULL);
    if (!client) {
        fprintf(stderr, "Connect failed: %s\n", bc_last_error_msg(NULL));
        return 1;
    }

    /* Get current terminal size */
    struct winsize ws;
    int cols = 80, rows = 24;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == 0) {
        cols = ws.ws_col;
        rows = ws.ws_row;
    }

    /* Spawn a PTY */
    bc_pty_info_t *info = bc_pty_spawn(client, cols, rows, NULL);
    if (!info) {
        fprintf(stderr, "PTY spawn failed: %s\n",
                bc_last_error_msg(client));
        bc_close(client);
        return 1;
    }
    bc_pty_info_free(info);

    /* Put local terminal in raw mode */
    struct termios orig, raw;
    tcgetattr(STDIN_FILENO, &orig);
    raw = orig;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);

    /* Set up signal handlers */
    signal(SIGINT, sigint_handler);
    signal(SIGWINCH, sigwinch_handler);

    int server_fd = bc_get_fd(client);
    struct pollfd fds[2];
    fds[0].fd = STDIN_FILENO;
    fds[0].events = POLLIN;
    fds[1].fd = server_fd;
    fds[1].events = POLLIN;

    printf("Connected. Press Ctrl-] to disconnect.\r\n");

    while (running) {
        int ret = poll(fds, 2, 500);
        if (ret < 0) continue;

        /* Local input -> PTY */
        if (fds[0].revents & POLLIN) {
            char buf[256];
            int n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) {
                /* Check for escape character (Ctrl-]) */
                for (int i = 0; i < n; i++) {
                    if (buf[i] == '\x1d') {  /* Ctrl-] */
                        running = 0;
                        break;
                    }
                }
                if (running)
                    bc_pty_write(client, buf, n);
            }
        }

        /* PTY output -> local terminal */
        if (fds[1].revents & POLLIN) {
            char buf[4096];
            int n = bc_pty_read(client, buf, sizeof(buf), 0);
            if (n > 0) {
                write(STDOUT_FILENO, buf, n);
            } else if (n < 0) {
                break;
            }
        }
    }

    /* Restore terminal */
    tcsetattr(STDIN_FILENO, TCSANOW, &orig);
    printf("\r\nDisconnected.\r\n");

    bc_pty_close(client);
    bc_close(client);
    return 0;
}
```

Build and run:

```bash
gcc -o pty_term pty_term.c -lbashclient
./pty_term
```

This gives you a fully interactive terminal connected to the bash-server's
PTY. Press Ctrl-] to disconnect.

---

## Thread Safety

libbashclient is **NOT thread-safe**. The following rules apply:

1. **One client per thread.** Each thread must create its own `bc_client_t *`
   via `bc_connect()` or `bc_connect_opts()`.

2. **No sharing of handles.** Do not pass a `bc_client_t *` to another
   thread without synchronization.

3. **No sharing of result structs.** The `bc_eval_result_t *` and other
   result types should be used and freed in the same thread that called the
   originating function.

4. **bc_free() is thread-safe.** Strings obtained from the library can be
   freed via `bc_free()` from any thread, provided the string is not
   accessed concurrently.

5. **bc_version() is thread-safe.** It returns a static string.

If you need multi-threaded access, create a separate connection per thread:

```c
#include <pthread.h>
#include <bashclient.h>

static void *worker(void *arg)
{
    const char *cmd = (const char *)arg;

    bc_client_t *c = bc_connect(NULL, NULL);
    if (!c) return NULL;

    bc_eval_result_t *r = bc_eval(c, cmd);
    if (r) {
        printf("[thread %lu] %s", pthread_self(), r->stdout_data);
        bc_eval_result_free(r);
    }

    bc_close(c);
    return NULL;
}

int main(void)
{
    pthread_t t1, t2;
    pthread_create(&t1, NULL, worker, "echo thread-1");
    pthread_create(&t2, NULL, worker, "echo thread-2");
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);
    return 0;
}
```
