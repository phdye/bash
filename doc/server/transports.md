# bash-server Transport Layer

**Audience:** Developers integrating with bash-server and operators deploying it.

## Overview

bash-server supports 4 transport modes for different deployment scenarios.
Each transport provides a bidirectional byte stream between the client and
server.  The protocol layer (v1 text or v2 JSON frames) operates
identically regardless of transport.

### Transport Summary

| Transport | Flag | Multi-client | Platform | Use case |
|-----------|------|-------------|----------|----------|
| Unix socket | (default) | Yes | All | Daemon serving local clients |
| stdio | `--stdio` | No | All | Subprocess spawned by parent |
| fd | `--fd N` | No | All | Inherited file descriptor |
| Named Pipe | `--named-pipe NAME` | Yes | Cygwin only | Windows IPC bypassing AF_UNIX emulation |

### Comparison

| Feature | Unix Socket | stdio | fd | Named Pipe |
|---------|------------|-------|----|----|
| Multi-client | Yes | No | No | Yes |
| Accept loop | Yes | No | No | Yes |
| Platform | All | All | All | Cygwin only |
| Auth delivery | Token file | auth-fd | auth-fd | Token file |
| Socket path | Filesystem | N/A | N/A | `\\.\pipe\bash-server-NAME` |
| Use case | Daemon | Subprocess | Subprocess | Windows IPC |

## Unix Socket (Default)

The default transport uses a Unix domain socket (`AF_UNIX`, `SOCK_STREAM`).
This is the standard mode for running bash-server as a long-lived daemon
that serves multiple sequential clients.

### Socket Path Resolution

The server resolves its socket path using a priority hierarchy.  The first
match wins:

| Priority | Source | Example |
|----------|--------|---------|
| 1 | `--socket PATH` (CLI) | `--socket /run/bash-server/sock` |
| 2 | `$BASH_SERVER_SOCKET` (env) | `export BASH_SERVER_SOCKET=/tmp/my.sock` |
| 3 | `~/.bash-serverrc` (config file) | `socket /custom/path/sock` |
| 4 | `$XDG_RUNTIME_DIR/bash-server/sock` | `/run/user/1000/bash-server/sock` |
| 5 | `/tmp/bash-server-<uid>/sock` | `/tmp/bash-server-1000/sock` |

### Directory and Permission Setup

The server creates the parent directory for the socket file if it does not
exist, with mode `0700` (owner-only access).  It will not create nested
parent directories---only the immediate parent.

| Resource | Mode | Purpose |
|----------|------|---------|
| Socket directory | `0700` | Contains socket and token files |
| Socket file | `0600` | Unix domain socket endpoint |
| Token file | `0600` | Authentication credential |

### Token File

The authentication token is written to `<socket-path>.token` with mode
`0600`.  The file contains 64 hexadecimal characters followed by a
newline (65 bytes total).  Created with `O_CREAT | O_EXCL` after
unlinking any stale token from a previous run.

### Accept Loop

The server enters a blocking `accept()` loop after binding:

```
while (server_running):
    reap_children()       // waitpid(WNOHANG) for zombie children
    client_fd = accept()  // blocks until connection or EINTR
    handle_client(client_fd)
```

Clients are handled sequentially.  While one client is connected,
subsequent connections queue in the kernel's listen backlog (`SOMAXCONN`).

### Stale Socket Handling

On startup, the server calls `unlink()` on the socket path before
`bind()`.  This removes stale socket files from previous runs that did
not shut down cleanly.

### Cygwin AF_UNIX Internals

On Cygwin, `AF_UNIX` sockets are internally implemented over TCP loopback
(`127.0.0.1`).  The Cygwin runtime performs a credential handshake during
`connect()`/`accept()` using a shared secret and `ucred` structure
exchange.

**Problem:**  Python's `socket.connect()` uses a non-blocking connect
followed by `poll()` and `getsockopt(SO_ERROR)`.  This races with
Cygwin's credential handshake, causing `ECONNABORTED` (errno 113) on
`accept()`.

**Solution:**  The `--no-peercred` flag calls:

```c
setsockopt(fd, SOL_SOCKET, SO_PEERCRED, NULL, 0);
```

This invokes Cygwin's `af_local_set_no_getpeereid()`, disabling the
credential handshake entirely.  The `NULL, 0` form is required---any
non-NULL optval or non-zero optlen returns `EINVAL`.

**Trade-off:**  `getpeereid()` and `getsockopt(SO_PEERCRED)` no longer
return peer credentials.  This is acceptable because bash-server uses
token-based authentication rather than peer credential checks.

### Example

```bash
# Start with defaults
bash-server --verbose

# Start with custom socket path
bash-server --socket /run/user/$(id -u)/bash-server/sock --verbose

# Connect
bashclient --socket /tmp/bash-server-$(id -u)/sock \
           --auth "$(cat /tmp/bash-server-$(id -u)/sock.token)" \
           --eval 'echo hello'
```

## stdio Mode (`--stdio`)

In stdio mode, the server uses standard input (fd 0) for reading and
standard output (fd 1) for writing.  This is designed for scenarios where
a parent process spawns bash-server as a subprocess and communicates via
pipes.

### Behavior

- **Single session:**  No accept loop.  The server handles exactly one
  session on stdin/stdout, then exits.
- **Token delivery:**  The authentication token is written to the
  auth-fd (default: stderr, fd 2) instead of a token file.  The parent
  process reads the token from this fd.
- **Exit:**  After the session ends (client sends disconnect/quit, or
  stdin reaches EOF), the server exits.

### Token Delivery

On startup, the server writes the 64-character hex token followed by a
newline to the auth-fd.  The parent process should read this token before
sending the AUTH message.  The default auth-fd is stderr (fd 2).  Use
`--auth-fd N` to redirect to a different file descriptor.

### Example

```bash
# Parent spawns bash-server and communicates via pipes
bash-server --stdio 2>token.tmp &
SERVER_PID=$!
TOKEN=$(cat token.tmp)

# Send commands via stdin/stdout
echo "AUTH $TOKEN"    >&0
echo "EVAL echo hi"   >&0
echo "QUIT"            >&0
```

A more typical usage is programmatic: the parent opens pipes, forks,
execs `bash-server --stdio`, reads the token from stderr (or a specified
auth-fd), and then communicates via the pipe endpoints.

## fd Mode (`--fd N`)

In fd mode, the server uses an inherited file descriptor for both read
and write operations.  This is designed for parent processes that create
a socketpair or pipe pair and pass one end to bash-server.

### Behavior

- **Single session:**  Like stdio mode, no accept loop.
- **Descriptor usage:**  The server reads from and writes to the
  specified fd.  If the parent creates a socketpair, one fd suffices
  for bidirectional communication.
- **Token delivery:**  Via auth-fd (same as stdio mode).
- **Exit:**  After the session ends.

### Separate Read/Write Descriptors

For unidirectional pipes (where a single fd cannot serve both read and
write), the parent should set up the file descriptors appropriately
before exec.  The `client_fd` field in the config stores the read fd,
and the `write_fd` field in the session allows a separate write
descriptor when needed.

### Example

```bash
# Parent creates a socketpair and passes one end
python3 -c "
import socket, subprocess, os
s1, s2 = socket.socketpair()
proc = subprocess.Popen(
    ['bash-server', '--fd', str(s2.fileno()), '--auth-fd', '3'],
    pass_fds=(s2.fileno(), 3),
    # fd 3 captures the token
)
s2.close()
# Read token from fd 3, communicate via s1
"
```

## Windows Named Pipes (`--named-pipe NAME`, Cygwin only)

The Named Pipe transport uses Windows Named Pipes instead of Unix domain
sockets.  This bypasses Cygwin's `AF_UNIX`-over-TCP-loopback emulation
entirely, avoiding the `SO_PEERCRED` race condition and providing native
Windows IPC.

### Pipe Path

The server creates a pipe at:

```
\\.\pipe\bash-server-NAME
```

Where `NAME` is the argument to `--named-pipe`.

### Security (DACL)

The pipe is created with an owner-only DACL using SDDL:

```
D:(A;;GA;;;OW)
```

This grants `GENERIC_ALL` access to the Owner only, preventing other
users from connecting to the pipe.

### Accept Mechanism

Named Pipe accept uses a helper thread to work around Cygwin's inability
to perform overlapped I/O on Named Pipes:

1. The main thread creates the pipe via `CreateNamedPipeW()`.
2. A helper thread calls `ConnectNamedPipe()`, which blocks until a
   client connects.
3. The main thread polls a shared flag indicating connection status.
4. On connection, `cygwin_attach_handle_to_fd()` converts the Windows
   `HANDLE` to a POSIX file descriptor usable by the session code.

This design allows the main thread to remain responsive to signals
(SIGINT, SIGTERM) while waiting for clients.

### Token File

The token is written to:

```
$XDG_RUNTIME_DIR/bash-server/NAME.token
```

Or if `$XDG_RUNTIME_DIR` is not set:

```
/tmp/bash-server-<uid>/NAME.token
```

The token file path is determined by `server_winpipe_token_path()`.

### Multi-client Support

Like the Unix socket transport, the Named Pipe transport supports
multiple sequential clients.  After each client disconnects, the server
creates a new pipe instance and waits for the next connection.

### Why Named Pipes?

On Cygwin, `AF_UNIX` sockets are emulated over TCP loopback.  This
emulation has known issues:

| Issue | AF_UNIX on Cygwin | Named Pipe |
|-------|-------------------|------------|
| `SO_PEERCRED` race | Yes (ECONNABORTED with non-blocking connect) | No (no credential handshake) |
| TCP loopback exposure | `127.0.0.1` port visible to local processes | Named pipe namespace, DACL-protected |
| Performance | TCP stack overhead | Direct kernel IPC |
| Firewall interaction | May trigger firewall prompts | No network stack involvement |

### Example

```bash
# Start with Named Pipe transport
bash-server --named-pipe myserver --verbose

# Token location
cat /tmp/bash-server-$(id -u)/myserver.token

# Client connects to \\.\pipe\bash-server-myserver
bashclient --named-pipe myserver \
           --auth "$(cat /tmp/bash-server-$(id -u)/myserver.token)" \
           --eval 'echo hello'
```

## Auth-fd Token Delivery (`--auth-fd N`)

The `--auth-fd` option controls where the server writes the
authentication token for stdio and fd transport modes.

### Behavior

| Mode | Default auth-fd | Token delivery |
|------|----------------|----------------|
| Unix socket | N/A | Token file (`<socket>.token`) |
| stdio | stderr (fd 2) | Written to auth-fd |
| fd | stderr (fd 2) | Written to auth-fd |
| Named Pipe | N/A | Token file (`NAME.token`) |

### Usage

```bash
# Default: token on stderr
bash-server --stdio

# Token on fd 3 (parent opens fd 3 before exec)
bash-server --stdio --auth-fd 3

# Token on fd 4 with fd mode
bash-server --fd 5 --auth-fd 4
```

### Parent Process Pattern

A typical parent process:

1. Creates a pipe pair for auth-fd (e.g., fds 3 and 4).
2. Creates a socketpair for communication (e.g., fds 5 and 6).
3. Forks and execs: `bash-server --fd 6 --auth-fd 4`.
4. Closes the child's ends (fds 4 and 6).
5. Reads the 65-byte token (64 hex + newline) from fd 3.
6. Communicates via fd 5.

### Token Format

The token written to auth-fd is identical to the token file format:
64 lowercase hexadecimal characters followed by a single newline
character (65 bytes total).

## Protocol Version Detection

Regardless of transport, the server auto-detects the protocol version
from the first byte of client data:

| First byte | Version | Wire format |
|------------|---------|-------------|
| ASCII letter (`A`--`Z`, `a`--`z`) | v1 (line-oriented text) | Text lines, LF-terminated |
| `0x00`--`0x05` (channel ID) | v2 (binary frames) | 6-byte header + JSON payload |
| `{` (0x7B) | v2 (NDJSON) | Newline-delimited JSON |

This detection happens once at the start of each session.  The protocol
version is then fixed for the remainder of the session.

## See Also

- [protocol.md](protocol.md) --- v1 line-oriented text protocol specification
- [channels.md](channels.md) --- v2 channel architecture and message formats
- [configuration.md](configuration.md) --- Socket path resolution and config file
- [security.md](security.md) --- Permission model and threat analysis
- [developer.md](developer.md) --- Building, testing, and contributing
