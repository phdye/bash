# bash-server Architecture

**Audience:** Developers and contributors working on the bash-server codebase.

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                        bash-server                          │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────┐  │
│  │ server_main  │  │server_socket │  │ server_session   │  │
│  │              │  │              │  │                  │  │
│  │ CLI parsing  │  │ Socket       │  │ Auth handling    │  │
│  │ Config       │  │ create/bind  │  │ Cmd dispatch     │  │
│  │ Daemonize    │  │ accept       │  │ Fork+exec        │  │
│  │ Signal setup │  │ close/clean  │  │ Output capture   │  │
│  │ Accept loop  │  │ nonblocking  │  │ Bash init        │  │
│  └──────┬───────┘  └──────────────┘  └────────┬─────────┘  │
│         │                                      │            │
│  ┌──────┴──────────────────────────────────────┴─────────┐  │
│  │                server_protocol                         │  │
│  │                                                        │  │
│  │  Line I/O (read_line, write_line)                      │  │
│  │  Command parsing (parse_command)                       │  │
│  │  Base64 encode/decode                                  │  │
│  │  Constant-time compare                                 │  │
│  └────────────────────────────────────────────────────────┘  │
│                            │                                 │
│                     ┌──────┴──────┐                          │
│                     │ cygbash-5.1 │                          │
│                     │    .dll     │                          │
│                     │             │                          │
│                     │ parse_and_  │                          │
│                     │ execute()   │                          │
│                     │ shell vars  │                          │
│                     │ builtins    │                          │
│                     └─────────────┘                          │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────┐
│     bashclient      │
│                     │
│ Standalone binary   │
│ No bash DLL linkage │
│ Base64 decode       │
│ Three modes:        │
│  -e eval            │
│  -f file            │
│  -i interactive     │
└─────────────────────┘
```

## Source File Map

### bash-server/

| File | Lines | Responsibility |
|------|-------|---------------|
| `server.h` | 121 | Common header: includes, constants, types, function declarations |
| `server_main.c` | 600 | Entry point, CLI parsing, config resolution, daemonize, signal handling, accept loop |
| `server_socket.c` | 155 | Unix socket lifecycle: create, bind, listen, accept, close, nonblocking toggle |
| `server_session.c` | 397 | Client session: init, auth, command dispatch, fork+exec, output capture |
| `server_protocol.c` | 269 | Wire protocol: line I/O, command parsing, base64, secure compare |

### bashclient/

| File | Lines | Responsibility |
|------|-------|---------------|
| `bashclient.c` | 510 | Standalone client: connect, auth, eval/interactive/script modes, base64 decode |

### bash-server/tests/

| File | Tests | Covers |
|------|-------|--------|
| `test_protocol.c` | 30 | Base64 encode/decode, secure compare, command parsing, line I/O |
| `test_socket.c` | 8 | Socket creation, permissions, stale replacement, accept, nonblocking |
| `test_config.c` | 19 | Directory creation, config file parsing, socket path resolution, token generation, CLI parsing |
| `test_client.c` | — | Integration test utility: raw protocol client with single/multi/eval modes |

## Data Structures

### server_config_t

Defined in `server.h`.  Populated by `parse_arguments()` and `resolve_socket_path()`.

```c
typedef struct server_config {
    char *socket_path;    // Resolved Unix socket path
    char *auth_token;     // Heap-allocated 64-char hex token
    char *auth_file;      // Path to <socket>.token file
    int   max_clients;    // Max simultaneous clients (default: 10, currently unused)
    int   verbose;        // Verbose logging to stderr
    int   daemon_mode;    // Fork to background
    int   no_peercred;    // Cygwin: disable SO_PEERCRED handshake
    char *pid_file;       // Path for PID file (daemon mode)
} server_config_t;
```

### client_session_t

Per-connection state, stack-allocated in `handle_client()`.

```c
typedef struct client_session {
    int   fd;              // Client socket file descriptor
    int   authenticated;   // Boolean: AUTH completed
    pid_t pid;             // Server PID (for identification)
    int   stdout_pipe[2];  // Pipe for capturing stdout (unused in current flow)
    int   stderr_pipe[2];  // Pipe for capturing stderr (unused in current flow)
} client_session_t;
```

Note: The `stdout_pipe`/`stderr_pipe` fields in the session struct are initialized
but not used directly.  The `capture_output()` function creates its own local pipes
per-EVAL call.

## Control Flow

### Server Startup Sequence

```
main()
  ├── parse_arguments()           // CLI → server_config_t
  ├── server_daemonize()          // If --daemon: double-fork, setsid, redirect to /dev/null
  ├── resolve_socket_path()       // CLI > $BASH_SERVER_SOCKET > ~/.bash-serverrc > XDG > /tmp
  │   └── read_config_file()      // Parse ~/.bash-serverrc
  ├── setup_signals()             // SIGINT/SIGTERM → server_running=0
  │                               // SIGCHLD → got_sigchld=1 (SA_RESTART)
  │                               // SIGPIPE → SIG_IGN
  ├── server_socket_create()      // socket → setsockopt → unlink → bind → chmod 0600 → listen
  ├── generate_auth_token()       // /dev/urandom → 32 bytes → hex → write <socket>.token
  ├── write_pid_file()            // If --pidfile
  │
  └── while (server_running)      // ACCEPT LOOP
        ├── reap_children()       // If got_sigchld: waitpid(WNOHANG) loop
        ├── accept()              // Blocks until connection or EINTR
        └── handle_client()
              ├── session_init()
              ├── session_handle()     // Command read loop
              │     ├── protocol_read_line()
              │     ├── protocol_parse_command()
              │     └── dispatch:
              │           ├── AUTH → handle_auth()
              │           │           └── init_bash_for_session() (first time)
              │           ├── EVAL → handle_eval()
              │           │           └── capture_output()
              │           │                 ├── pipe() × 2
              │           │                 ├── fork()
              │           │                 │   └── child: dup2 → parse_and_execute → _exit
              │           │                 ├── read_all_fd() × 2
              │           │                 ├── waitpid()
              │           │                 ├── base64_encode()
              │           │                 └── write STDOUT/STDERR/EXIT
              │           ├── PING → handle_ping()
              │           └── QUIT → handle_quit() → done=1
              └── session_cleanup()    // close(fd), close pipes
```

### Command Execution (EVAL) Detail

The fork-per-command model provides isolation:

```
            Server Process                    Child Process
            ──────────────                    ─────────────
capture_output():
  pipe(stdout_pipe)
  pipe(stderr_pipe)
  fork() ─────────────────────────────────►  close(read ends)
  close(write ends)                           dup2(stdout_pipe[1] → fd 1)
  read_all_fd(stdout_pipe[0])                 dup2(stderr_pipe[1] → fd 2)
  read_all_fd(stderr_pipe[0])                 close(pipe write ends)
  waitpid(child) ◄───────────────────────     parse_and_execute(cmd)
  base64_encode(stdout_data)                  fflush(stdout, stderr)
  base64_encode(stderr_data)                  _exit(last_command_exit_value)
  write STDOUT/STDERR/EXIT lines
```

**Why fork-per-command:**
- Process isolation: a segfault or resource leak in the command does not
  affect the server.
- Clean output capture: stdout/stderr are captured via pipes with no
  interference from the server's own I/O.
- Exit code accuracy: `_exit()` in the child provides the true exit status.
- Trade-off: No shared state between EVAL calls.  Variable assignments,
  function definitions, and directory changes in one EVAL do not persist.

### Bash Initialization

`init_bash_for_session()` is called on first successful AUTH.  It initializes
the Bash interpreter components in this order:

1. `shell_name = "bash-server"` — identifies the shell in error messages
2. `interactive_shell = 0` — non-interactive mode
3. `login_shell = 0` — not a login shell
4. `initialize_shell_builtins()` — registers builtin commands
5. `initialize_traps()` — sets up trap handling
6. `initialize_signals(0)` — signal disposition (non-privileged)
7. `tilde_initialize()` — tilde expansion support
8. `initialize_shell_variables(shell_environment, 0)` — imports environment
9. `initialize_job_control(0)` — job control stubs (no terminal)
10. `initialize_bash_input()` — input subsystem
11. `initialize_flags()` — shell flags
12. `initialize_shell_options(0)` — `shopt` options
13. `initialize_bashopts(0)` — `BASHOPTS` variable

This is done once per server lifetime (guarded by `bash_initialized` flag).

## Signal Handling

| Signal | Handler | Flags | Purpose |
|--------|---------|-------|---------|
| `SIGINT` | `server_signal_handler` | 0 (no SA_RESTART) | Sets `server_running=0`; interrupts `accept()` with EINTR |
| `SIGTERM` | `server_signal_handler` | 0 (no SA_RESTART) | Same as SIGINT |
| `SIGCHLD` | `server_signal_handler` | SA_RESTART | Sets `got_sigchld=1`; does not interrupt I/O |
| `SIGPIPE` | `SIG_IGN` | — | Prevents crash on write to disconnected client |

**Design rationale:**
- SIGINT/SIGTERM use `sa_flags=0` so that the blocking `accept()` call
  returns `EINTR`, allowing the main loop to check `server_running` and exit.
- SIGCHLD uses `SA_RESTART` so that ongoing I/O operations (pipe reads,
  socket writes) are not interrupted when a child exits.

## Socket Implementation

### Cygwin AF_UNIX Internals

Cygwin implements `AF_UNIX` sockets over TCP loopback (`127.0.0.1`).  The
Cygwin runtime performs a credential handshake during `connect()`/`accept()`
using a shared secret and `ucred` structure exchange.

**Problem:**  Python's `socket.connect()` uses a non-blocking connect +
`poll()` + `getsockopt(SO_ERROR)` pattern.  This races with Cygwin's
credential handshake, causing `ECONNABORTED` (errno 113) on `accept()`.

**Solution:**  The `--no-peercred` flag calls:
```c
setsockopt(fd, SOL_SOCKET, SO_PEERCRED, NULL, 0);
```
This invokes Cygwin's `af_local_set_no_getpeereid()`, disabling the
credential handshake entirely.  The `NULL, 0` form is required — any
non-NULL optval or non-zero optlen returns `EINVAL`.

**Trade-off:**  `getpeereid()` and `getsockopt(SO_PEERCRED)` no longer
return peer credentials.  This is acceptable because bash-server uses
token-based authentication rather than peer credential checks.

## Build System Integration

### Makefile Targets (top-level)

```makefile
bash-server: .made         # Build bash-server (requires --enable-bash-server)
bashclient: .made          # Build bashclient
install-bash-server:       # Install both to $(bindir)
```

### Link Dependencies

**bash-server** links against:
- `cygbash-5.1.dll` (via `-L$(BUILD_DIR) -lcygbash`) — Bash interpreter
- `-lreadline` — Readline library
- `-lhistory` — History library
- `-lncursesw` — Terminal capabilities
- `-lintl` — Internationalization
- `-ldl` — Dynamic loading

**bashclient** links against:
- `-ldl` only — Standalone, no Bash dependency

### DLL Architecture

The bash build produces a shared library `cygbash-5.1.dll` containing
the full Bash interpreter.  Both `bash.exe` (the shell) and `bash-server.exe`
link against this DLL via an import library (`libcygbash.dll.a`).

```
bash.exe ──────────┐
                    ├──► cygbash-5.1.dll ──► cygwin1.dll
bash-server.exe ───┘                         ──► KERNEL32.dll
```

This architecture means:
- `cygbash-5.1.dll` must be in the same directory or in PATH
- Both `bash.exe` and `bash-server.exe` share the same Bash version
- Bug fixes in Bash automatically apply to both binaries

## Memory Management

### Heap Allocations

| Field | Allocated by | Freed by |
|-------|-------------|----------|
| `config.socket_path` | `resolve_socket_path()` (strdup/malloc) | Process exit |
| `config.auth_token` | `generate_auth_token()` (strdup) | Process exit |
| `config.auth_file` | `generate_auth_token()` (malloc) | Process exit |
| `stdout_data` / `stderr_data` | `read_all_fd()` (malloc/realloc) | `capture_output()` cleanup |
| `stdout_b64` / `stderr_b64` | `protocol_base64_encode()` (malloc) | `capture_output()` cleanup |
| `cmd_copy` | `strdup()` in child | `parse_and_execute()` frees it |

### Security-Sensitive Memory

- Raw random bytes (32 bytes from `/dev/urandom`) are zeroed with `memset()`
  after hex encoding in `generate_auth_token()`.
- The auth token string remains in memory for the server's lifetime (needed
  for comparison on each AUTH).

## Error Handling Patterns

### Resource Cleanup Pattern

```c
static int
capture_output(client_session_t *session, const char *command)
{
    int stdout_pipe[2] = {-1, -1};
    // ... allocations ...

    if (error_condition) {
        protocol_write_line(session->fd, "%s reason", RSP_ERR);
        goto cleanup;
    }

cleanup:
    if (stdout_pipe[0] >= 0) close(stdout_pipe[0]);
    // ... free all allocations ...
    return 0;
}
```

### errno Preservation Pattern

```c
if (bind(fd, ...) < 0) {
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return -1;
}
```

This ensures callers see the meaningful errno (bind failure) rather than
a spurious errno from close().

## Future Design Considerations

### max_clients Field

The `max_clients` configuration field is parsed and validated but not
currently enforced.  The server handles clients sequentially.  A future
multi-threaded or multi-process model could use this to cap concurrency.

### Persistent State

The fork-per-command model means no state persists between EVAL calls.
If persistent state is needed (e.g., shell variables across commands),
the execution model would need to change to in-process evaluation with
stdio redirection, at the cost of isolation.

### Loadable Builtins

The Cygwin DLL architecture (`cygbash-5.1.dll`) supports loadable
builtins via `enable -f`.  This could allow extending the server with
custom builtins without recompilation.
