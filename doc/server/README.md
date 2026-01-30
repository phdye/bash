# bash-server — Persistent Bash Evaluation Daemon

## Overview

`bash-server` is a persistent evaluation daemon that embeds GNU Bash 5.1 as a
shared library (`cygbash-5.1.dll`).  It exposes the interpreter through two
protocol versions: a line-oriented text protocol (v1) and a channel-multiplexed
JSON protocol (v2), with optional NDJSON framing.

A companion CLI client, `bashclient`, connects and submits commands.  The v2
protocol adds channels for state operations, observability events, interactive
debugging, and PTY terminal emulation.

### Key Characteristics

| Property | Value |
|----------|-------|
| Transport | Unix socket, stdio, fd, Windows Named Pipes |
| Protocol | v1 (text), v2 (JSON frames), v2/NDJSON |
| Authentication | 256-bit random token, constant-time compare |
| Concurrency | Single-threaded accept, fork-per-session |
| Channels (v2) | Control, Command, State, Observe, Debug, PTY |
| Max command | 64 KB |
| Max output | 1 MB per stream |
| License | GNU GPL v3+ |
| Version | 1.0 (GNU Bash 5.1) |

### Components

| Binary | Description |
|--------|-------------|
| `bash-server` | Daemon process |
| `bashclient` | CLI client (v1 protocol) |
| `cygbash-5.1.dll` | Shared Bash library |

### Source Modules

| Module | Purpose |
|--------|---------|
| `server_main.c` | Entry point, CLI, daemonization, accept loop |
| `server_protocol.c` | v1 wire protocol, base64, secure compare |
| `server_session.c` | v1 session lifecycle, AUTH, EVAL, protocol detection |
| `server_socket.c` | Unix socket management |
| `server_json.c` | v2 JSON frames, channel dispatch, NDJSON wire format |
| `server_state.c` | Shell state (vars, funcs, aliases, traps, inspect) |
| `server_observe.c` | Observability hooks (pre/post command events) |
| `server_debug.c` | Breakpoints, stepping, AST inspection |
| `server_pty.c` | PTY spawn, relay, resize, ANSI stripping |
| `cmd_serialize.c` | COMMAND tree to/from JSON serialization |
| `server_winpipe.c` | Windows Named Pipes transport (Cygwin) |

## Quick Start

### Start the Server

```bash
# Foreground (verbose)
bash-server --verbose

# Background daemon with PID file
bash-server --daemon --pidfile /tmp/bash-server.pid --verbose

# Custom socket path
bash-server --socket ~/.local/run/bash-server/sock

# stdio mode (for subprocess integration)
bash-server --stdio --auth-fd 3

# fd mode (for socketpair integration)
bash-server --fd 4 --auth-fd 5

# Named pipe (Cygwin only)
bash-server --named-pipe myserver
```

On startup, the server:
1. Resolves the socket path (see [configuration.md](configuration.md))
2. Creates and binds the transport endpoint (mode `0600` for sockets)
3. Generates a 256-bit random authentication token
4. Writes the token to `<socket-path>.token` (mode `0600`) or delivers via `--auth-fd`
5. Enters the accept loop (socket/named-pipe) or handles a single session (stdio/fd)

### Connect with bashclient (v1)

```bash
# Read token from the default location
SOCK=/tmp/bash-server-$(id -u)/sock
TOKEN=$(cat ${SOCK}.token)

# Execute a single command
bashclient --socket "$SOCK" --auth "$TOKEN" --eval 'echo hello world'

# Interactive session
bashclient --socket "$SOCK" --auth "$TOKEN" --interactive

# Execute a script file
bashclient --socket "$SOCK" --auth "$TOKEN" --file script.sh
```

### Raw v1 Protocol Session (socat)

```bash
SOCK=/tmp/bash-server-$(id -u)/sock
TOKEN=$(cat ${SOCK}.token)

# Using socat
socat - UNIX-CONNECT:$SOCK <<EOF
AUTH $TOKEN
EVAL echo hello
QUIT
EOF
```

Expected responses:
```
OK
STDOUT aGVsbG8K
STDERR
EXIT 0
BYE
```

### v2 NDJSON Session Example

```bash
# Using socat with NDJSON framing
SOCK=/tmp/bash-server-$(id -u)/sock
TOKEN=$(cat ${SOCK}.token)

socat - UNIX-CONNECT:$SOCK <<'EOF'
{"ch":0,"type":"auth","token":"TOKEN_HERE"}
{"ch":1,"type":"eval","command":"echo hello"}
{"ch":0,"type":"disconnect"}
EOF
```

Expected responses (one JSON object per line):
```json
{"ch":0,"type":"auth_ok","capabilities":["state","command","observe","debug"]}
{"ch":1,"type":"stdout","data":"aGVsbG8K","encoding":"base64"}
{"ch":1,"type":"stderr","data":"","encoding":"base64"}
{"ch":1,"type":"complete","exit_code":0}
{"ch":0,"type":"disconnect_ok"}
```

The protocol version is auto-detected from the first byte of the connection:
- `{` or `\n` selects NDJSON (v2)
- Bytes 0x00-0x05 select binary v2 framing
- Printable ASCII selects v1 text protocol

## Stopping the Server

```bash
# If running in foreground: Ctrl-C (SIGINT)

# If running as daemon:
kill $(cat /tmp/bash-server.pid)

# Or send SIGTERM to the process
kill $(pgrep bash-server)
```

The server performs clean shutdown on SIGINT/SIGTERM:
- Closes the listening socket
- Removes the socket file
- Removes the token file
- Removes the PID file (if any)
- Reaps child processes

## Documentation Index

| Document | Audience | Description |
|----------|----------|-------------|
| [README.md](README.md) | All | This file -- overview and quick start |
| [protocol.md](protocol.md) | Developers | Wire protocol specification (v1 + v2) |
| [architecture.md](architecture.md) | Contributors | Internal design and code structure |
| [configuration.md](configuration.md) | Operators | CLI, configuration, and transport modes |
| [security.md](security.md) | Security engineers | Threat model and security controls |
| [api/](api/) | Developers | Per-operation reference (man-page style) |

## Build

```bash
# From the bash source root:
./configure --enable-bash-server
make -j12
make bash-server
make bashclient

# Run tests:
cd bash-server/tests && make check
```

## Platform Support

`bash-server` is developed and tested on Cygwin (x86_64).

The Cygwin platform has a specific quirk with `AF_UNIX` sockets: they are
internally implemented over TCP loopback with a credential handshake.  The
`--no-peercred` flag disables this handshake to allow non-C clients (e.g.,
Python's `socket` module) to connect without `ECONNABORTED` errors.

The Windows Named Pipes transport (`--named-pipe`) provides an alternative
that bypasses the `AF_UNIX`-over-TCP-loopback emulation entirely, eliminating
the `SO_PEERCRED` handshake race condition.  Named Pipes are secured with an
owner-only DACL (equivalent to `chmod 0600`).

See [security.md](security.md) for the security implications of each transport.
