# bash-server Architecture

## System Overview

bash-server is a persistent evaluation daemon that embeds the GNU Bash 5.1
interpreter as a shared library.  It accepts connections over multiple
transport types, authenticates clients, and dispatches commands to the
embedded interpreter.

```
                  +-----------------+
                  |   bashclient    |
                  | (standalone)    |
                  +-------+---------+
                          |
          Unix socket / Named Pipe / stdio / fd
                          |
                  +-------v---------+
                  |  server_main.c  |
                  |  (accept loop)  |
                  +-------+---------+
                          |
                     fork() per session
                          |
               +----------v-----------+
               |  server_session.c    |
               |  (protocol detect)   |
               +----+-----+----------+
                    |     |
            v1 text |     | v2 JSON
                    |     |
        +-----------+     +-----------+
        |                             |
+-------v--------+          +--------v--------+
| server_         |          | server_json.c   |
| protocol.c      |          | (channel        |
| (line I/O,      |          |  dispatch)      |
|  base64,        |          +---+--+--+--+---+
|  secure cmp)    |              |  |  |  |  |
+-------+---------+              |  |  |  |  |
        |                  +-----+  |  |  |  +------+
        v                  |     |  |  |  |         |
+-------+---------+   +---v-+ +-v--++ +v-++ +------v---+
| server_state.c  |   |CTRL | |CMD | |OBS| |DEBUG| PTY |
| (vars, funcs,   |   |ch=0 | |ch=1| |ch=3| |ch=4|ch=5 |
|  aliases, traps)|   +-----+ +----+ +---++ +----++----+
+--------+--------+                   |      |     |
         |                            |      |     |
         v                  +---------v+  +--v---+ +--v--------+
  +------+--------+         |server_   |  |server| |server_    |
  | cygbash-5.1   |         |observe.c |  |debug | |pty.c      |
  | .dll          |         |(hooks)   |  |.c    | |(forkpty,  |
  | (Bash engine) |         +----------+  |      | | relay,    |
  +---------------+                       |      | | ansi_strip|
                                          +------+ +-----------+
                                          |
                                   +------v--------+
                                   | cmd_serialize  |
                                   | .c (AST<->JSON)|
                                   +---------------+
```

Additional transport module (Cygwin only):

```
  +-------------------+
  | server_winpipe.c  |
  | (Windows Named    |
  |  Pipes transport)  |
  +-------------------+
```

## Source File Map

### Core Server

| File | Lines | Purpose |
|------|-------|---------|
| `server.h` | ~300 | Master header: constants, typedefs, all function prototypes |
| `server_main.c` | ~890 | Entry point, CLI parsing, signal setup, daemonization, accept loops for socket/stdio/fd/named-pipe transports |
| `server_session.c` | ~400 | Session lifecycle, protocol auto-detection, v1 command dispatch, bash initialization, startup file sourcing |
| `server_protocol.c` | ~350 | v1 wire protocol: line-oriented I/O, base64 encode/decode, constant-time string compare |
| `server_socket.c` | ~200 | Unix domain socket: create, bind, listen, accept, close, SO_PEERCRED |

### v2 Protocol and Channels

| File | Lines | Purpose |
|------|-------|---------|
| `server_json.c` | ~600 | v2 JSON frame read/write (binary + NDJSON), channel dispatch, JSON helpers (get_string, get_int, escape) |
| `server_state.c` | ~520 | Shell state operations: get/set/unset for variables, functions, aliases, traps; INSPECT bulk listing |
| `server_observe.c` | ~260 | Observability hooks: pre/post command events via command_hooks infrastructure, timestamps, sequencing |
| `server_debug.c` | ~730 | Interactive debugger: breakpoints (command/line/function), stepping (step/next/finish/skip), AST inspection, pause/resume via socket I/O |
| `server_pty.c` | ~625 | PTY terminal emulation: forkpty spawn, select-based relay loop, resize (TIOCSWINSZ), signal injection, ANSI escape stripping state machine |
| `cmd_serialize.c` | ~500 | COMMAND tree serialization: bash internal AST structures to/from JSON for debugger inspection and pre-parsed execution |

### Platform-Specific

| File | Lines | Purpose |
|------|-------|---------|
| `server_winpipe.c` | ~290 | Windows Named Pipes transport (Cygwin): CreateNamedPipeW with owner-only DACL, ConnectNamedPipe via helper thread, cygwin_attach_handle_to_fd conversion, token file management |

### Tests

| File | Purpose |
|------|---------|
| `tests/test_protocol.c` | v1 protocol: base64, secure_compare, parse_command, read/write line |
| `tests/test_socket.c` | Socket: create/close, permissions, accept, nonblocking |
| `tests/test_config.c` | Configuration: directory creation, config parsing, path resolution, token, CLI |
| `tests/test_serialize.c` | cmd_serialize: COMMAND tree to/from JSON round-trip |
| `tests/test_observe.c` | Observability: hook registration, event formatting |
| `tests/test_state.c` | State operations: get/set/unset vars, funcs, aliases, traps, inspect |
| `tests/test_session.c` | Session: init, protocol detection, v1 command handling |
| `tests/test_transport.c` | Transport: stdio/fd mode, named pipe integration |
| `tests/test_winpipe.c` | Windows Named Pipes: create, accept, token path resolution |

## Data Structures

### server_config_t

Holds all server configuration parsed from CLI, environment, and config file.
Defined in `server.h`.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `socket_path` | `char *` | (resolved) | Unix socket path |
| `pid_file` | `char *` | NULL | PID file path (--pidfile) |
| `auth_token` | `char *` | (generated) | 256-bit hex token |
| `auth_file` | `char *` | (derived) | Token file path (`<socket>.token`) |
| `init_file` | `char *` | NULL | Additional init script (--init) |
| `named_pipe` | `char *` | NULL | Named pipe name (--named-pipe, Cygwin) |
| `max_clients` | `int` | 10 | Max concurrent sessions (--max-clients) |
| `client_fd` | `int` | -1 | Inherited fd (--fd mode) |
| `auth_fd` | `int` | -1 | Token delivery fd (--auth-fd; -1 = stderr) |
| `daemon_mode` | `int` | 0 | Daemonize (--daemon) |
| `no_peercred` | `int` | 0 | Disable SO_PEERCRED (--no-peercred) |
| `login_mode` | `int` | 0 | Login shell init (--login) |
| `norc` | `int` | 0 | Skip ~/.bashrc (--norc) |
| `noprofile` | `int` | 0 | Skip profile files (--noprofile) |
| `stdio_mode` | `int` | 0 | Stdio transport (--stdio) |
| `fd_mode` | `int` | 0 | Fd transport (--fd) |
| `verbose` | `int` | 0 | Verbose logging (--verbose) |

### client_session_t

Per-session state.  Created in the forked child process (or in the main
process for stdio/fd/named-pipe transport).

| Field | Type | Description |
|-------|------|-------------|
| `fd` | `int` | Client read fd |
| `write_fd` | `int` | Client write fd (-1 = use `fd` for both) |
| `authenticated` | `int` | 1 if AUTH succeeded |
| `protocol_version` | `int` | PROTOCOL_V1, PROTOCOL_V2, or PROTOCOL_V2_NDJSON |
| `wire_format` | `int` | WIRE_BINARY or WIRE_NDJSON |
| `pid` | `pid_t` | Session process PID |
| `stdout_pipe[2]` | `int` | Pipe for capturing EVAL stdout |
| `stderr_pipe[2]` | `int` | Pipe for capturing EVAL stderr |

### Protocol Constants

```c
/* Protocol versions */
#define PROTOCOL_V1          1
#define PROTOCOL_V2          2
#define PROTOCOL_V2_NDJSON   3

/* Wire formats */
#define WIRE_BINARY          0
#define WIRE_NDJSON          1

/* Channel IDs */
#define CHAN_CONTROL   0
#define CHAN_COMMAND   1
#define CHAN_STATE     2
#define CHAN_OBSERVE   3
#define CHAN_DEBUG     4
#define CHAN_PTY       5

/* Limits */
#define SERVER_MAX_LINE       8192
#define SERVER_MAX_TOKEN      256
#define SERVER_MAX_CMD        65536
#define SERVER_MAX_OUTPUT     1048576
#define SERVER_TOKEN_BYTES    32
#define SERVER_TOKEN_HEXLEN   64
#define FRAME_HEADER_SIZE     6
#define FRAME_MAX_PAYLOAD     1048576

/* Observability levels */
#define OBSERVE_LEVEL_MAX     1
```

## Control Flow

### Server Startup Sequence

```
main()
  |
  +-- parse_arguments()          Parse CLI options into server_config_t
  |
  +-- [if --daemon] server_daemonize()   Double-fork, setsid, redirect stdio
  |
  +-- setup_signals()            SIGINT/SIGTERM -> server_running=0
  |                              SIGCHLD -> got_sigchld=1
  |                              SIGPIPE -> SIG_IGN
  |
  +-- Dispatch by transport mode:
      |
      +-- [--stdio or --fd]   run_single_session()
      |     +-- generate_auth_token()
      |     +-- deliver_token_fd()      Write "TOKEN <hex>\n" to auth_fd
      |     +-- session_init()
      |     +-- session_handle()
      |     +-- session_cleanup()
      |
      +-- [--named-pipe]     run_named_pipe_server()  (Cygwin only)
      |     +-- generate_auth_token()
      |     +-- server_winpipe_token_path()
      |     +-- while (server_running):
      |           +-- server_winpipe_create()
      |           +-- server_winpipe_accept()    Helper thread + polling
      |           +-- handle_client()            Direct (no fork)
      |
      +-- [default: socket]  run_socket_server()
            +-- resolve_socket_path()
            +-- server_socket_create()
            +-- generate_auth_token()
            +-- while (server_running):
                  +-- server_accept_client()
                  +-- fork()
                  +-- [child] handle_client()
                  +-- [parent] active_clients++
```

### Session Flow

```
session_handle()
  |
  +-- Auto-detect protocol (peek first byte)
  |
  +-- [v1] v1_session_loop():
  |     while (!done):
  |       +-- protocol_read_line()
  |       +-- Parse command keyword
  |       +-- Dispatch: AUTH / EVAL / PING / QUIT / state commands
  |       +-- [AUTH] init_bash_for_session() on first auth
  |       +-- [EVAL] capture_output() -> fork + pipe + parse_and_execute
  |       +-- [State] state_handle_get_var(), state_handle_set_var(), etc.
  |
  +-- [v2] json_session_handle():
        +-- Handle AUTH on CHAN_CONTROL
        +-- init_bash_for_session() on first auth
        +-- while (!done):
              +-- json_frame_read() or ndjson_read_line()
              +-- Extract channel from header or "ch" field
              +-- Dispatch by channel:
                    CHAN_CONTROL -> ping, disconnect, configure
                    CHAN_COMMAND -> eval (fork + pipe + capture)
                    CHAN_STATE  -> v2 state get/set/unset/inspect
                    CHAN_OBSERVE -> subscribe/unsubscribe
                    CHAN_DEBUG  -> debug_handle_message()
                    CHAN_PTY    -> pty_handle_spawn() (takes over loop)
```

### Protocol Auto-Detection

```
session_handle():
  byte = recv(fd, &byte, 1, MSG_PEEK)

  if byte == '{' or byte == '\n':
      session->protocol_version = PROTOCOL_V2_NDJSON
      session->wire_format = WIRE_NDJSON
      -> json_session_handle()

  else if byte >= 0x00 and byte <= 0x05:
      session->protocol_version = PROTOCOL_V2
      session->wire_format = WIRE_BINARY
      -> json_session_handle()

  else:  (printable ASCII: 'A', 'E', 'P', 'Q', ...)
      session->protocol_version = PROTOCOL_V1
      -> v1_session_loop()
```

### EVAL Execution (fork + pipe capture)

Both v1 and v2 use the same execution model for EVAL commands:

```
capture_output(session, command):
  pipe(stdout_pipe)
  pipe(stderr_pipe)
  pid = fork()

  [child]:
    close read ends
    dup2(stdout_pipe[1], STDOUT)
    dup2(stderr_pipe[1], STDERR)
    parse_and_execute(command, "bash-server", SEVAL_NONINT | SEVAL_NOHIST)
    _exit(last_command_exit_value)

  [parent]:
    close write ends
    read stdout_pipe[0] -> stdout_buf (up to SERVER_MAX_OUTPUT)
    read stderr_pipe[0] -> stderr_buf (up to SERVER_MAX_OUTPUT)
    waitpid(pid, &status, 0) -> exit_code
    base64_encode(stdout_buf) -> STDOUT response
    base64_encode(stderr_buf) -> STDERR response
    send EXIT exit_code
```

## Module Architecture

### Protocol Layer

**v1 (server_protocol.c):**
- `protocol_read_line()` -- Read LF-terminated line from fd, strip CR
- `protocol_write_line()` -- Write formatted line with LF terminator
- `protocol_base64_encode()` / `protocol_base64_decode()` -- RFC 4648 base64
- `protocol_secure_compare()` -- Constant-time string comparison (anti-timing)

**v2 (server_json.c):**
- `json_frame_read()` -- Read binary v2 frame (6-byte header + payload)
- `json_frame_write()` / `json_frame_write_fmt()` -- Write binary v2 frame
- `ndjson_read_line()` -- Read NDJSON line, extract `"ch"` field
- `ndjson_write()` -- Write NDJSON line with `"ch"` field
- `json_get_string()` / `json_get_int()` -- Lightweight JSON field extraction (no full parser)
- `json_escape()` / `json_escape_for_observe()` -- JSON string escaping

### State Operations (server_state.c)

Operates on the bash interpreter's live state without forking.  Each function
takes a write fd and an argument string:

- **Variables:** `state_handle_get_var()`, `state_handle_set_var()`, `state_handle_unset_var()` -- Uses `find_variable()`, `bind_variable()`, `unbind_variable()` from the bash API
- **Functions:** `state_handle_get_func()`, `state_handle_unset_func()` -- Uses `find_function()`, `named_function_string()`, `parse_and_execute("unset -f ...")`
- **Aliases:** `state_handle_get_alias()`, `state_handle_set_alias()`, `state_handle_unset_alias()` -- Uses `get_alias_value()`, `add_alias()`, `remove_alias()`
- **Traps:** `state_handle_set_trap()`, `state_handle_unset_trap()` -- Uses `set_signal()`, `restore_default_signal()`, `decode_signal()`
- **Inspect:** `state_handle_inspect()` -- Bulk listing with optional prefix filter. Uses `all_visible_variables()`, `all_shell_functions()`, `all_aliases()`, `trap_list[]`

Variable attributes are reported as comma-separated strings: `exported`, `readonly`, `integer`, `local`, `array`, `assoc`, `nameref`, `uppercase`, `lowercase`, `capcase`, `trace`.

### Observability Hooks (server_observe.c)

Registers callbacks with the bash `command_hooks` infrastructure to intercept
command execution:

- `observe_init(fd, level)` -- Initialize for a session, set observe fd
- `observe_set_level(level)` -- Register/unregister hooks based on level (0=off, 1=events)
- `observe_cleanup()` -- Unregister hooks

**Hook callbacks:**
- `observe_pre_hook()` -- Sends `pre_command` JSON event on CHAN_OBSERVE with command string, cwd, line number, subshell/async flags, timestamp. Starts timing.
- `observe_post_hook()` -- Sends `post_command` JSON event with exit status, optional signal number, and duration in milliseconds.

Events are sequenced with a monotonic `event_seq` counter.  Since each session
runs in a forked process, the global state (fd, level, seq) is safe.

### Debug System (server_debug.c)

Provides interactive debugging using pre-command hooks:

**Breakpoints:**
- Three types: `DBG_BREAK_COMMAND` (substring match), `DBG_BREAK_LINE` (exact line), `DBG_BREAK_FUNC` (substring match on function name)
- Stored as a linked list of `breakpoint_t` structs
- Each has: id, type, enabled flag, hit count, pattern/line, optional condition string
- Management: `debug_add_breakpoint()`, `debug_remove_breakpoint()`, `debug_enable_breakpoint()`, `debug_list_breakpoints()` (serializes to JSON array)

**Step modes:**
- `DBG_RUN` (0): Normal execution
- `DBG_STEP` (1): Break at every command
- `DBG_NEXT` (2): Break at same nesting depth
- `DBG_FINISH` (3): Break when depth decreases (step out)
- `DBG_SKIP` (4): Acknowledged but command still executes (hook cannot prevent execution)

**Execution flow when a break fires:**
1. `debug_pre_hook()` checks step mode and breakpoints
2. Stores `info->command` as `dbg.pending_cmd` for AST inspection
3. Sends `break_hit` event on CHAN_DEBUG
4. Calls `debug_wait_for_resume()` which blocks reading from the client fd
5. Client can send: `continue`, `step`, `next`, `finish`, `skip`, `inspect_ast`, `list`, `break`, `delete`
6. On resume command, returns the new step mode
7. `pending_cmd` is cleared

**AST inspection:**
When paused at a breakpoint, `inspect_ast` triggers `cmd_serialize(dbg.pending_cmd)`
which serializes the bash internal COMMAND tree to JSON.

### PTY Relay (server_pty.c)

Provides full terminal emulation via pseudo-terminal:

- `pty_spawn()` -- `forkpty()` + exec interactive bash with `--login -i`
- `pty_relay_loop()` -- `select()`-based bidirectional relay between client socket and PTY master fd
- `pty_resize()` -- `ioctl(TIOCSWINSZ)` + `kill(SIGWINCH)`
- `pty_signal()` -- Signal injection to PTY child
- `pty_close()` -- Close master fd, wait for child (polling with timeout, then SIGKILL)

**ANSI escape stripping (`ansi_strip()`):**

A stateful finite-state machine that removes ANSI escape sequences from a
byte stream.  State persists across calls so sequences split across `read()`
boundaries are handled correctly.

States: `STRIP_NORMAL`, `STRIP_ESC`, `STRIP_CSI`, `STRIP_OSC`, `STRIP_OSC_ESC`, `STRIP_CHARSET`

Handles:
- CSI sequences: `ESC [ ... <final>` and 8-bit CSI (0x9B)
- OSC sequences: `ESC ] ... BEL` or `ESC ] ... ESC \`
- Two-character escapes: `ESC + final byte (0x40-0x7E)`
- Charset designators: `ESC (` or `ESC )` + one byte

The filter operates in-place (output <= input) and is enabled per-session
via the `strip_ansi` field in the PTY spawn message.

### Command Serialization (cmd_serialize.c)

Converts bash's internal COMMAND tree structures to/from JSON:

- `cmd_serialize(COMMAND *cmd)` -- Serialize COMMAND tree to JSON string (malloc'd)
- Uses a growable string buffer (`serbuf_t`) for efficient JSON construction
- Handles all COMMAND types: simple commands, pipelines, connections (&&, ||, ;), for/while/until/if/case/select loops, function definitions, subshells, coprocesses, group commands
- Serializes WORD_DESC, WORD_LIST, REDIRECT structures
- Used by the debugger for `inspect_ast` responses

### Windows Named Pipes (server_winpipe.c)

Cygwin-only transport that bypasses AF_UNIX-over-TCP-loopback:

- `server_winpipe_create()` -- `CreateNamedPipeW()` with owner-only DACL (`D:(A;;GA;;;OW)`)
- `server_winpipe_accept()` -- Helper thread runs blocking `ConnectNamedPipe()`; main thread polls event every 500ms checking `server_running` flag; on connect, `cygwin_attach_handle_to_fd()` converts Win32 HANDLE to POSIX fd
- `server_winpipe_token_path()` -- Resolves token file path for named pipe mode

Named pipe fds do not survive `fork()` + parent `close()` on Cygwin (the
parent's `CloseHandle()` disconnects the underlying Win32 HANDLE), so named
pipe sessions are handled directly in the main process (no fork-per-session).

## Key Design Decisions

### Fork-per-Session Model

Each accepted client connection results in a `fork()`.  The child process
inherits the bash interpreter state and handles the session.  This provides:

- **Isolation:** EVAL commands cannot crash the server; a child exit does not
  affect other sessions
- **State persistence:** Variables, functions, aliases persist across EVALs
  within a session (the child's address space is the session's state)
- **Simplicity:** No locking, no thread safety concerns, no shared memory

The parent process only manages accept/reap and enforces `max_clients`.

**Exception:** Named pipe transport handles clients directly in the main
process because Cygwin's `cygwin_attach_handle_to_fd()` fds do not survive
fork.

### Protocol Auto-Detection

The server auto-detects the protocol version from the first byte using
`MSG_PEEK`, allowing a single server to serve both v1 and v2 clients on
the same endpoint.  This avoids requiring separate ports or negotiation
handshakes.

### Channel Multiplexing

v2 protocol uses fixed channel IDs (0-5) rather than dynamic channel
negotiation.  This simplifies the implementation: each channel maps to a
specific module (state, observe, debug, pty), and the dispatch is a simple
switch statement.  All channels share the same fd pair.

### ANSI Stripping State Machine

PTY output contains ANSI escape sequences for cursor positioning, colors,
etc.  When clients want only the text content (e.g., for automated testing
or AI consumption), the `strip_ansi` option activates a server-side state
machine.  The state machine handles sequences split across `read()` boundaries
by maintaining state between calls, which is necessary because PTY reads
are arbitrarily chunked.

### Single-Threaded Per Session

Within a session process, all operations are single-threaded.  The debug
system exploits this by blocking in `debug_wait_for_resume()` when a
breakpoint fires -- since there is no other work to do in the session
process, blocking on the socket is the simplest synchronization mechanism.

The PTY relay loop uses `select()` for I/O multiplexing rather than threads,
keeping the single-threaded model consistent.
