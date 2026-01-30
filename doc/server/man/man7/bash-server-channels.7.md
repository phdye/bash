# BASH-SERVER-CHANNELS(7) -- Channel multiplexing system

# DESCRIPTION

The v2 protocol multiplexes six logical channels over a single client
connection.  Each channel carries a distinct class of messages,
allowing simultaneous command execution, state queries, observability
events, debugger interaction, and terminal I/O without interference.

Channels are identified by a single byte (0-5) in binary frame headers,
or by a `"channel"` integer field in NDJSON frames.

# CONCEPTS

# Channel identification

In binary wire format, the channel ID is the first byte of the 6-byte
frame header:

```
+----------+-------+------------------+---------+
| channel  | flags | payload_length   | payload |
| (1 byte) | (1 B) | (4 bytes, BE)    | (var)   |
+----------+-------+------------------+---------+
```

In NDJSON wire format, each JSON line includes a `"channel"` field:

```json
{"channel":1,"type":"eval","command":"echo hello"}
```

# Channel definitions

**CHAN_CONTROL (0)** -- Session lifecycle

Always available on every connection.  Handles authentication, session
configuration, keepalive, and graceful disconnect.

Message types:
- `auth` -- Authenticate with token
- `auth_ok` / `auth_fail` -- Authentication response
- `ping` / `pong` -- Keepalive
- `configure` -- Set session parameters (e.g., `observe_level`)
- `disconnect` -- Graceful session teardown

**CHAN_COMMAND (1)** -- Command execution

Submits commands for evaluation and receives results.

Message types:
- `eval` -- Evaluate a command string; server responds with `stdout`,
  `stderr`, and `complete` messages carrying base64-encoded output and
  the exit code
- `execute` -- Evaluate a pre-parsed AST (JSON-serialized COMMAND tree);
  same response format as `eval`

Response messages:
- `stdout` -- Base64-encoded standard output
- `stderr` -- Base64-encoded standard error
- `complete` -- Exit code and signal information

**CHAN_STATE (2)** -- Shell state operations

Direct manipulation of shell internal state without executing commands.

Message types:
- `get_var` / `set_var` / `unset_var` -- Shell variables
- `get_func` / `unset_func` -- Shell functions
- `get_alias` / `set_alias` / `unset_alias` -- Aliases
- `set_trap` / `unset_trap` -- Signal traps
- `inspect` -- List all items of a given type (variables, functions,
  aliases, traps)

Each operation returns an `ok` or `error` response with the requested
data or error details.

**CHAN_OBSERVE (3)** -- Observability events (server push)

Server-initiated events that notify the client about command execution.
Must be enabled via `configure` on CHAN_CONTROL by setting
`observe_level` to a non-zero value.

Event types at level 1:
- `pre_command` -- Emitted before command execution; includes command
  string, current working directory, line number, subshell/async flags,
  timestamp
- `post_command` -- Emitted after command execution; includes command
  string, exit status, signal number (if killed), duration in
  milliseconds, timestamp

Events include sequence numbers for ordering.  See
**bash-server-observe**(7) for details.

**CHAN_DEBUG (4)** -- Interactive debugger

Breakpoint management, execution control, and AST inspection.  The
debugger pauses execution when a breakpoint is hit and waits for the
client to send a resume command.

Message types:
- `set_breakpoint` -- Add a breakpoint (command, line, or function pattern)
- `delete_breakpoint` -- Remove a breakpoint by ID
- `enable_breakpoint` / `disable_breakpoint` -- Toggle breakpoint state
- `list_breakpoints` -- Enumerate all breakpoints with hit counts
- `step` / `next` / `finish` / `continue` -- Execution control
- `inspect_ast` -- Serialize the pending COMMAND tree to JSON
- `break_hit` -- Server-push event when a breakpoint or step fires

See **bash-server-debug**(7) for details.

**CHAN_PTY (5)** -- Terminal emulation

Spawns an interactive Bash session in a pseudo-terminal and relays I/O
between the client and the PTY.  Once a PTY session is spawned, this
channel takes over the session loop.

Message types:
- `spawn` -- Create a PTY with specified rows, columns, shell path,
  and optional ANSI stripping
- `input` -- Client-to-PTY data (base64-encoded)
- `output` -- PTY-to-client data (base64-encoded)
- `resize` -- Change terminal dimensions (TIOCSWINSZ + SIGWINCH)
- `signal` -- Inject a signal (SIGINT, SIGTERM, etc.)
- `close` -- Terminate the PTY session
- `exit` -- Server-push notification with child exit code

Control messages (`ping`, `disconnect`) remain functional during PTY
mode.  See **bash-server-pty**(7) for details.

# Channel availability

All channels are available after authentication.  CHAN_CONTROL is the
only channel that accepts messages before authentication (specifically,
the `auth` message).

# Ordering guarantees

Messages within a single channel are processed in order.  Messages on
different channels may be interleaved.  Server-push events (CHAN_OBSERVE,
CHAN_DEBUG) include sequence numbers to allow clients to reconstruct
ordering relative to command responses.

# SEE ALSO

**bash-server**(7),
**bash-server-auth**(7),
**bash-server-transports**(7),
**bash-server-debug**(7),
**bash-server-observe**(7),
**bash-server-pty**(7)

# AUTHORS

GNU Bash is Copyright (C) Free Software Foundation, Inc.
The bash-server extension was developed as part of the Cygwin Bash project.

# COPYRIGHT

This is free software; see the GNU General Public License v3 or later
for copying conditions.  There is NO warranty.
