# bash-server Developer and Contributor Guide

**Audience:** Developers and contributors working on the bash-server codebase.

## Building

### Prerequisites

- Cygwin (x86_64) with GCC, make, and standard development packages
- GNU Bash 5.1 source tree (this repository)
- Readline, ncursesw, intl development libraries

### Build Commands

From the repository root:

```bash
# Configure with bash-server support
./configure --enable-bash-server

# Build everything (bash, cygbash-5.1.dll, bash-server, bashclient)
make -j12

# Build bash-server and bashclient explicitly
make bash-server
make bashclient
```

### Build Outputs

| Artifact | Description | Links against |
|----------|-------------|---------------|
| `bash.exe` | Bash shell (68 KB stub) | `cygbash-5.1.dll`, `cygwin1.dll` |
| `cygbash-5.1.dll` | Shared Bash library (4.6 MB) | readline, history, ncursesw, intl |
| `bash-server/bash-server.exe` | Persistent evaluation daemon | `cygbash-5.1.dll`, `cygwin1.dll` |
| `bashclient/bashclient.exe` | CLI client | `cygwin1.dll` only (standalone) |

### Clean Build

```bash
make clean && make -j12
```

### Install

```bash
# Install bash-server and bashclient to $(bindir)
make install-bash-server
```

## Test Suite

### Running Tests

```bash
cd bash-server/tests && make check
```

This compiles and runs all test binaries.  Each binary prints a summary
of passed/failed tests.  Any failure causes `make check` to return a
non-zero exit code.

### Test Binaries

| Binary | Tests | Covers |
|--------|-------|--------|
| `test_protocol` | 30 | base64 encode/decode, secure_compare, parse_command, read/write line |
| `test_socket` | 8 | socket create/close, permissions, stale replacement, accept, nonblocking |
| `test_config` | 19 | directory creation, config file parsing, socket path resolution, token generation, CLI parsing |
| `test_session` | — | Session lifecycle: init, auth, command dispatch, bash initialization |
| `test_state` | — | State operations: get/set/unset variables, functions, aliases, traps, inspect |
| `test_transport` | — | Transport modes: stdio, fd, socket, protocol version detection |
| `test_observe` | — | Observability hooks: level management, event emission, JSON escaping |
| `test_serialize` | — | COMMAND tree serialization/deserialization round-trips |
| `test_winpipe` | — | Windows Named Pipe transport (Cygwin only) |

### Test Patterns

All tests follow these conventions:

**Timeout enforcement:**  Every test uses `alarm()` to set a timeout
(10--30 seconds).  No test is allowed to block indefinitely.

**Fork-based isolation:**  Tests that perform blocking operations (socket
I/O, pipe reads) fork a child process.  The parent polls with
`waitpid(pid, &status, WNOHANG)` in a bounded loop:

```c
pid_t child = fork();
if (child == 0) {
    /* Do the risky operation */
    _exit(result);
}
/* Parent: poll with bounded timeout */
for (int i = 0; i < MAX_POLLS; i++) {
    if (waitpid(child, &status, WNOHANG) == child)
        goto done;
    usleep(POLL_INTERVAL);
}
kill(child, SIGKILL);
waitpid(child, NULL, 0);
/* Report timeout failure */
```

**Assert macros:**  Tests use assert-style macros that print the test
name and condition on failure, then call `_exit(1)` in the child or
set a failure flag in the parent.

**Expected output comparison:**  Where applicable, tests compare actual
output against expected strings using `strcmp()` or
`protocol_secure_compare()`.

### Integration Tests

```bash
cd bash-server/tests && bash run_integration.sh
```

Integration tests exercise the full client-server stack: starting a
server, connecting a client, running commands, and verifying output.

## How to Add a New Channel

Adding a new channel requires changes in several places.  Follow this
checklist:

### 1. Define the Channel Constant

In `server.h`, add the new channel ID and update `CHAN_MAX`:

```c
#define CHAN_NEW    6   /* Description of the new channel */
#define CHAN_MAX    6
```

### 2. Create the Channel Module

Create `bash-server/server_new.c` with at minimum:

```c
#include "server.h"

void new_init(int rfd, int wfd)
{
    /* Initialize channel state */
}

void new_cleanup(void)
{
    /* Free channel resources */
}

int new_handle_message(int rfd, int wfd, const char *payload)
{
    /* Parse and dispatch channel messages */
    /* Return 0 on success, -1 on error */
}
```

### 3. Add Dispatch in server_json.c

In `json_session_handle()`, add a case for the new channel:

```c
case CHAN_NEW:
    rc = new_handle_message(session->fd, wfd, payload);
    break;
```

### 4. Add Declarations to server.h

```c
/* Function declarations - server_new.c */
void new_init(int rfd, int wfd);
void new_cleanup(void);
int  new_handle_message(int rfd, int wfd, const char *payload);
```

### 5. Update the Build System

In `bash-server/Makefile.in`, add the new source file to the object list
and add its compilation rule.

### 6. Write Tests

Create `bash-server/tests/test_new.c` following the established patterns
(alarm timeout, fork isolation, assert macros).  Add it to
`bash-server/tests/Makefile`.

### 7. Document

Add the channel to [channels.md](channels.md) with message formats,
field descriptions, and example flows.

## How to Add a New Message Type

Within an existing channel:

1. **Add a case in the channel's handle function.**  For example, in
   `debug_handle_message()`, add parsing for the new `"type"` value.

2. **Add the JSON response format.**  Use `json_frame_write_fmt()` to
   send the response:

   ```c
   json_frame_write_fmt(wfd, CHAN_DEBUG,
       "{\"type\":\"new_response\",\"field\":\"%s\"}", value);
   ```

3. **Update channels.md.**  Document the request, response, fields,
   and any error conditions.

4. **Add test cases.**  In the corresponding test file, add tests that
   send the new message type and verify the response.

## How to Add a New CLI Option

### 1. Add to server_config_t

In `server.h`:

```c
typedef struct server_config {
    /* ... existing fields ... */
    int new_option;    /* --new-option: description */
} server_config_t;
```

### 2. Add to parse_arguments()

In `server_main.c`, add the long option and its case:

```c
static struct option long_options[] = {
    /* ... existing options ... */
    {"new-option", required_argument, 0, 'N'},
    {0, 0, 0, 0}
};

/* In the switch: */
case 'N':
    config->new_option = atoi(optarg);
    break;
```

### 3. Add to print_usage()

```c
fprintf(stderr, "  --new-option N    Description of the option\n");
```

### 4. Initialize the Default

In `parse_arguments()`, set the default value before the `getopt_long`
loop:

```c
config->new_option = DEFAULT_VALUE;
```

### 5. Use in the Appropriate Module

Access the config field where needed:

```c
if (config->new_option) {
    /* ... */
}
```

### 6. Update Documentation

Update the CLI reference table in [configuration.md](configuration.md)
and add the option to the man page at `doc/server/man1/bash-server.1.md`.

## Code Conventions

### Error Handling: goto cleanup

All functions that allocate resources use the `goto cleanup` pattern to
ensure resources are freed on both success and error paths:

```c
static int
some_function(int fd, const char *arg)
{
    int result = -1;
    char *buf = NULL;
    int pipe_fd[2] = {-1, -1};

    buf = malloc(SIZE);
    if (!buf)
        goto cleanup;

    if (pipe(pipe_fd) < 0)
        goto cleanup;

    /* ... work ... */

    result = 0;  /* success */

cleanup:
    free(buf);
    if (pipe_fd[0] >= 0) close(pipe_fd[0]);
    if (pipe_fd[1] >= 0) close(pipe_fd[1]);
    return result;
}
```

### Memory Management

- Use `malloc()`, `calloc()`, `strdup()` and `free()`.  No custom
  allocators.
- Always check return values of allocation functions.
- Initialize pointers to `NULL` and file descriptors to `-1` for safe
  cleanup.

### String Handling

- NUL-terminated strings throughout.
- Size-bounded output with `snprintf()`.  Never use `sprintf()`.
- `strncpy()` used only when explicit truncation is intended; otherwise
  `strdup()` + length checks.

### File Descriptors

- Close on all cleanup paths.
- Check return values of `close()`, `read()`, `write()`.
- Use `{-1, -1}` initialization for pipe arrays.

### Signals

- Signal handlers set `volatile sig_atomic_t` flags only.
- No complex logic in signal handlers (no malloc, no stdio, no
  non-async-signal-safe functions).
- Main loop checks flags and acts accordingly.

### JSON Handling

The codebase uses a simple, purpose-built JSON parser (`json_get_string()`,
`json_get_int()`) rather than a full JSON library.  This parser handles
the subset of JSON needed by the protocol: flat key-value objects with
string and integer values.

- `json_get_string()`: Extracts a string value by key.  Handles escaped
  characters.
- `json_get_int()`: Extracts an integer value by key.
- For output, use `snprintf()` or `json_frame_write_fmt()` to construct
  JSON strings.

### errno Preservation

When a system call fails and subsequent cleanup calls might clobber
`errno`, save and restore it:

```c
if (bind(fd, ...) < 0) {
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return -1;
}
```

## Module Dependencies

```
server_main.c ──────────► server_socket.c
                ──────────► server_session.c
                ──────────► server_winpipe.c

server_session.c ────────► server_protocol.c
                 ────────► server_json.c
                 ────────► server_state.c

server_json.c ──────────► server_observe.c
              ──────────► server_debug.c
              ──────────► server_pty.c
              ──────────► cmd_serialize.c

server_debug.c ─────────► cmd_serialize.c
               ─────────► command_hooks.h

server_observe.c ────────► command_hooks.h

server_pty.c ───────────► (standalone, uses protocol/json frame I/O)
```

### Module Responsibilities

| Module | Purpose |
|--------|---------|
| `server_main.c` | Entry point, CLI parsing, config resolution, daemonize, signal handling, accept loop |
| `server_socket.c` | Unix socket lifecycle: create, bind, listen, accept, close, nonblocking toggle |
| `server_session.c` | Client session: init, auth, command dispatch, fork+exec, output capture, v1/v2 routing |
| `server_protocol.c` | v1 wire protocol: line I/O, command parsing, base64, secure compare |
| `server_json.c` | v2 wire protocol: frame I/O, JSON parsing, channel dispatch, NDJSON support |
| `server_state.c` | Shell state access: variables, functions, aliases, traps, inspect |
| `server_observe.c` | Observability hooks: pre/post command events, level management |
| `server_debug.c` | Debugger: breakpoints, stepping, AST inspection, break events |
| `server_pty.c` | PTY allocation, I/O relay, resize, signal delivery, ANSI stripping |
| `server_winpipe.c` | Windows Named Pipe transport (Cygwin-specific) |
| `cmd_serialize.c` | COMMAND tree to/from JSON serialization |

### Bash Integration Points

bash-server links against `cygbash-5.1.dll` and uses these entry points:

| Function | Source | Purpose |
|----------|--------|---------|
| `parse_and_execute()` | `evalstring.c` | Parse and execute a command string |
| `shell_initialize()` | `shell.c` | Full shell initialization |
| `initialize_shell_builtins()` | `builtins/common.c` | Register builtin commands |
| `initialize_traps()` | `trap.c` | Set up trap handling |
| `initialize_signals()` | `sig.c` | Signal disposition |
| `get_string_value()` | `variables.c` | Read shell variable |
| `bind_variable()` | `variables.c` | Set shell variable |
| `find_function()` | `variables.c` | Look up shell function |
| `find_alias()` | `alias.c` | Look up alias |
| `set_signal()` | `trap.c` | Set trap handler |

## See Also

- [architecture.md](architecture.md) --- Internal design, data structures, control flow
- [channels.md](channels.md) --- v2 protocol channel reference
- [transports.md](transports.md) --- Transport layer documentation
- [protocol.md](protocol.md) --- v1 wire protocol specification
- [security.md](security.md) --- Threat model and security controls
