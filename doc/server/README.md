# bash-server — Persistent Bash Evaluation Daemon

## Overview

`bash-server` is a Unix domain socket server that provides persistent,
authenticated remote evaluation of Bash commands.  It embeds a full GNU Bash
5.1 interpreter and exposes it through a lightweight, line-oriented text
protocol.

A companion CLI client, `bashclient`, connects to a running server, authenticates,
and submits commands for execution.  Output (stdout and stderr) is captured
per-command and returned to the client as base64-encoded payloads, along with
the command's exit code.

### Key Characteristics

| Property | Value |
|----------|-------|
| Transport | Unix domain socket (`AF_UNIX`, `SOCK_STREAM`) |
| Protocol | Line-oriented text (LF-terminated), base64 payloads |
| Authentication | 256-bit random token, file-based distribution |
| Concurrency | Single-threaded, sequential client handling |
| Execution model | Fork-per-command with pipe-captured output |
| Max command size | 64 KB |
| Max output size | 1 MB per stream (stdout/stderr) |
| License | GNU GPL v3+ |
| Version | 1.0 (GNU Bash 5.1) |

### Components

| Binary | Description |
|--------|-------------|
| `bash-server` | Daemon process (links against Bash shared library) |
| `bashclient` | CLI client (standalone, no Bash linkage) |

## Quick Start

### Start the Server

```bash
# Foreground (verbose)
bash-server --verbose

# Background daemon with PID file
bash-server --daemon --pidfile /tmp/bash-server.pid --verbose

# Custom socket path
bash-server --socket ~/.local/run/bash-server/sock
```

On startup, the server:
1. Resolves the socket path (see [configuration.md](configuration.md))
2. Creates and binds the Unix domain socket (mode `0600`)
3. Generates a 256-bit random authentication token
4. Writes the token to `<socket-path>.token` (mode `0600`)
5. Enters the accept loop

### Connect with bashclient

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

### Raw Protocol Session (netcat/socat)

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
EXIT 0
BYE
```

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
| [README.md](README.md) | All | This file — overview and quick start |
| [protocol.md](protocol.md) | Developers | Wire protocol specification |
| [architecture.md](architecture.md) | Contributors | Internal design and code structure |
| [configuration.md](configuration.md) | Operators | Deployment, configuration, and operations |
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
