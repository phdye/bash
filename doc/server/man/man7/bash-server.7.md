# BASH-SERVER(7) -- Persistent Bash evaluation daemon architecture

# DESCRIPTION

**bash-server** is a persistent evaluation daemon that embeds GNU Bash 5.1
as a shared library (`cygbash-5.1.dll`).  It accepts connections over Unix
domain sockets, Windows Named Pipes, stdio, or inherited file descriptors,
authenticates clients with a random token, and evaluates Bash commands in
isolated forked processes.

The server links against the full Bash interpreter via `cygbash-5.1.dll`,
giving it access to all Bash builtins, parameter expansion, arithmetic,
globbing, and every other shell feature.  Both the standard `bash.exe`
shell and `bash-server.exe` share this DLL, so bug fixes in one
automatically apply to the other.

# SOURCE MODULES

The implementation consists of 11 C source files and 1 shared header:

| File | Purpose |
|------|---------|
| `server.h` | Common header: constants, typedefs (`server_config_t`, `client_session_t`), buffer sizes, channel IDs, function declarations |
| `server_main.c` | Entry point, CLI argument parsing, daemonization, signal setup, accept loop |
| `server_protocol.c` | v1 wire protocol I/O, base64 encode/decode, constant-time secure compare, protocol version detection |
| `server_session.c` | v1 session lifecycle, AUTH handling, command dispatch, EVAL fork+pipe execution, Bash initialization |
| `server_socket.c` | Unix socket create/bind/listen, close with cleanup, accept, nonblocking toggle |
| `server_json.c` | v2 JSON frame read/write, channel dispatch, NDJSON wire format support, JSON string helpers |
| `server_state.c` | Shell state operations: get/set/unset variables, functions, aliases, traps; inspect (list all of a type) |
| `server_observe.c` | Observability hooks: pre/post command events pushed on CHAN_OBSERVE |
| `server_debug.c` | Interactive debugger: breakpoints (command/line/function), stepping, AST inspection on CHAN_DEBUG |
| `server_pty.c` | PTY mode: forkpty() spawn, select()-based relay loop, resize, signal injection, ANSI escape stripping |
| `cmd_serialize.c` | COMMAND tree to JSON serialization and JSON to COMMAND deserialization |
| `server_winpipe.c` | Windows Named Pipes transport (Cygwin only): create pipe, accept via helper thread, DACL security |

# KEY DESIGN DECISIONS

# Fork-per-session model

Each accepted client connection is handled in a forked child process.
This provides complete isolation between clients: a crash, resource leak,
or runaway command in one session cannot affect the server or other
sessions.  The trade-off is that shell state (variables, functions,
directory changes) persists within a session but is isolated from other
sessions.

# Protocol auto-detection

The server detects the client's protocol version from the first byte
received on a new connection:

- ASCII letter (A-Z, a-z) indicates v1 text protocol
- Binary byte with value 0-5 indicates v2 binary framing
- `{` (0x7B) indicates v2 NDJSON wire format

This allows a single server to serve clients speaking any protocol
version without configuration.

# Channel multiplexing (v2)

The v2 protocol multiplexes six logical channels over a single
connection, identified by a single-byte channel ID (0-5):

- **CHAN_CONTROL (0)**: Session lifecycle (auth, ping, configure, disconnect)
- **CHAN_COMMAND (1)**: Command execution (eval, execute pre-parsed AST)
- **CHAN_STATE (2)**: Shell state operations (variables, functions, aliases, traps)
- **CHAN_OBSERVE (3)**: Server-push observability events
- **CHAN_DEBUG (4)**: Interactive debugger (breakpoints, stepping, AST inspection)
- **CHAN_PTY (5)**: Terminal emulation (PTY spawn, I/O relay, resize, signals)

See **bash-server-channels**(7) for details.

# Pre/post command hooks

The server registers hooks into Bash's command execution pipeline to
intercept commands before and after execution.  These hooks drive both
the observability system (CHAN_OBSERVE) and the debugger (CHAN_DEBUG).

# ANSI escape stripping

PTY mode includes a stateful finite automaton that strips ANSI escape
sequences from terminal output.  This allows clients to receive clean
text without terminal formatting codes, useful for programmatic
consumption of interactive shell output.

# BUFFER SIZES

| Constant | Value | Purpose |
|----------|-------|---------|
| `SERVER_MAX_LINE` | 8192 | Maximum v1 protocol line length |
| `SERVER_MAX_TOKEN` | 256 | Maximum token buffer size |
| `SERVER_MAX_CMD` | 65536 | Maximum command string length |
| `SERVER_MAX_OUTPUT` | 1 MB | Maximum captured stdout/stderr per command |
| `FRAME_MAX_PAYLOAD` | 1 MB | Maximum v2 frame payload |
| `FRAME_HEADER_SIZE` | 6 | v2 binary frame header: channel(1) + flags(1) + length(4) |

# TOKEN FORMAT

The authentication token is 32 bytes of entropy read from `/dev/urandom`,
hex-encoded to a 64-character string.  The raw bytes are zeroed after
encoding.  The token file is written with 0600 permissions.

# DLL ARCHITECTURE

```
bash.exe -----------+
                    +--> cygbash-5.1.dll --> cygwin1.dll
bash-server.exe ---+                    --> KERNEL32.dll
```

Both executables share the same Bash interpreter DLL.  `bashclient.exe`
is standalone and does not link against `cygbash-5.1.dll`.

# SEE ALSO

**bash-server-channels**(7),
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
