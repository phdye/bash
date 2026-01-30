# bash-server Configuration

## Command-Line Options

### Synopsis

```
bash-server [OPTIONS]
```

### Option Reference

#### Transport Options

| Short | Long | Argument | Description |
|-------|------|----------|-------------|
| `-s` | `--socket` | PATH | Unix domain socket path |
| `-S` | `--stdio` | -- | Use stdin/stdout (single session) |
| `-f` | `--fd` | N | Use inherited file descriptor N (single session) |
| `-W` | `--named-pipe` | NAME | Use Windows Named Pipe (Cygwin only) |

**Transport mode selection:**

The server operates in exactly one transport mode.  If multiple transport
options are specified, the last one wins.

| Mode | Flag | Accepts | Fork model |
|------|------|---------|------------|
| Socket (default) | `--socket` or none | Multiple clients | Fork-per-session |
| Stdio | `--stdio` | Single session | No fork |
| Fd | `--fd N` | Single session | No fork |
| Named Pipe | `--named-pipe NAME` | Multiple clients | Sequential (no fork) |

#### Daemon Options

| Short | Long | Argument | Description |
|-------|------|----------|-------------|
| `-d` | `--daemon` | -- | Run as background daemon (double-fork, setsid) |
| `-p` | `--pidfile` | PATH | Write PID to file (useful with --daemon) |

`--daemon` is incompatible with `--stdio` and `--fd` (the server will exit
with an error if both are specified).

When daemonizing, the server:
1. Forks twice (to prevent acquiring a controlling terminal)
2. Calls `setsid()` to become a session leader
3. Changes working directory to `/`
4. Redirects stdin/stdout/stderr to `/dev/null`

#### Connection Options

| Short | Long | Argument | Description |
|-------|------|----------|-------------|
| `-m` | `--max-clients` | N | Maximum simultaneous client sessions (default: 10) |
| `-P` | `--no-peercred` | -- | Disable Cygwin SO_PEERCRED credential handshake |

The `--max-clients` limit applies only to socket transport mode.  When the
limit is reached, new connections receive `ERR server busy (max clients reached)`
and are closed.

`--no-peercred` exists for compatibility with non-C clients (specifically
Python's `socket` module) on Cygwin, where the `SO_PEERCRED` credential
handshake bytes leak into the application data stream.  **Do NOT use** with
C clients or `bashclient` -- it is not needed and may corrupt the protocol.
Consider using `--named-pipe` instead, which avoids the issue entirely.

#### Authentication Options

| Short | Long | Argument | Description |
|-------|------|----------|-------------|
| `-A` | `--auth-fd` | N | Write authentication token to fd N |

Token delivery depends on the transport mode:

| Transport | Token delivery | Default |
|-----------|---------------|---------|
| Socket | Written to `<socket-path>.token` (mode 0600) | Always |
| Stdio | Written to `--auth-fd` or stderr | stderr |
| Fd | Written to `--auth-fd` or stderr | stderr |
| Named Pipe | Written to `$XDG_RUNTIME_DIR/bash-server/<name>.token` or `/tmp/bash-server-<uid>/<name>.token` | Always |

In stdio/fd modes, the token is delivered as a line:
```
TOKEN <64-hex-chars>\n
```

The `--auth-fd` option redirects this line to a specific file descriptor.
This is useful when stdout is used for protocol data (stdio mode) and you
need the token on a separate channel (e.g., fd 3):

```bash
bash-server --stdio --auth-fd 3  3>token.txt
```

#### Shell Initialization Options

| Short | Long | Argument | Description |
|-------|------|----------|-------------|
| `-l` | `--login` | -- | Login shell initialization |
| | `--norc` | -- | Skip sourcing `~/.bashrc` |
| | `--noprofile` | -- | Skip sourcing `/etc/profile` and `~/.bash_profile` |
| `-I` | `--init` | SCRIPT | Source additional init script per session |

These options control how the Bash interpreter is initialized when a session
is first authenticated.  They mirror standard Bash startup behavior:

**Default (no flags):**
- Source `~/.bashrc` (if it exists)

**Login mode (`--login`):**
- Source `/etc/profile` (if it exists)
- Source the first found of: `~/.bash_profile`, `~/.bash_login`, `~/.profile`
- Source `~/.bashrc` (unless `--norc`)

**`--norc`:**
- Skip `~/.bashrc`

**`--noprofile`:**
- Skip `/etc/profile` and `~/.bash_profile` / `~/.bash_login` / `~/.profile`

**`--init SCRIPT`:**
- After all standard startup files, source the specified script
- Useful for loading project-specific functions, aliases, and variables

All sourcing uses bash's `source_file()` function, which properly handles
`return` statements in sourced scripts.

#### Information Options

| Short | Long | Description |
|-------|------|-------------|
| `-v` | `--verbose` | Verbose output to stderr |
| `-h` | `--help` | Print usage and exit |
| `-V` | `--version` | Print version and exit |

`--verbose` prints diagnostic messages to stderr:
- Socket/pipe path and token file location
- Client connect/disconnect events
- Session fork PID and client count
- Child exit status
- Shutdown confirmation

## Socket Path Resolution

The socket path is resolved using a five-level hierarchy.  The first match
wins:

| Priority | Source | Example |
|----------|--------|---------|
| 1 | `--socket PATH` (CLI) | `--socket /run/bash-server/sock` |
| 2 | `$BASH_SERVER_SOCKET` (environment) | `export BASH_SERVER_SOCKET=/tmp/my-sock` |
| 3 | `socket` directive in `~/.bash-serverrc` | `socket /home/user/.local/bash-server/sock` |
| 4 | `$XDG_RUNTIME_DIR/bash-server/sock` | `/run/user/1000/bash-server/sock` |
| 5 | `/tmp/bash-server-<uid>/sock` (fallback) | `/tmp/bash-server-1000/sock` |

At levels 4 and 5, the server creates the parent directory (mode `0700`) if
it does not exist.  The socket file itself is created with mode `0600`.

## Config File

**Path:** `~/.bash-serverrc`

**Format:** One directive per line.  `#` introduces a comment.  Leading and
trailing whitespace is ignored.

**Supported directives:**

| Directive | Value | Description |
|-----------|-------|-------------|
| `socket` | PATH | Unix socket path (same as `--socket`) |

CLI options take precedence over config file directives.

**Example `~/.bash-serverrc`:**
```
# Use XDG runtime directory
socket /run/user/1000/bash-server/sock
```

## Environment Variables

| Variable | Used by | Description |
|----------|---------|-------------|
| `BASH_SERVER_SOCKET` | Server + client | Unix socket path (priority 2) |
| `BASH_SERVER_TOKEN` | Client | Authentication token |
| `XDG_RUNTIME_DIR` | Server | Base directory for socket/token files (priority 4) |
| `HOME` | Server | Home directory for `~/.bash-serverrc` and `~/.bashrc` |
| `TERM` | PTY sessions | Terminal type (set to `xterm-256color` if unset in PTY child) |

## Transport Modes

### Socket Mode (default)

The default and most capable transport.  Supports multiple concurrent
sessions via fork-per-session.

```bash
# Start with defaults
bash-server

# Start with explicit socket path
bash-server --socket /tmp/my-server.sock

# Start as daemon
bash-server --daemon --pidfile /tmp/bash-server.pid
```

The server creates a `SOCK_STREAM` Unix domain socket, binds it, and
listens with a backlog.  Each accepted connection is forked into a child
process that handles the session independently.

Token is written to `<socket-path>.token` (mode `0600`).

### Stdio Mode

Single-session transport for subprocess integration.  The parent process
launches `bash-server --stdio` and communicates over the child's stdin
(server reads) and stdout (server writes).

```bash
# Basic stdio mode
bash-server --stdio

# With token on fd 3
bash-server --stdio --auth-fd 3

# Typical parent process usage (pseudocode):
#   pipe(stdin_pipe)
#   pipe(stdout_pipe)
#   pipe(token_pipe)   # fd 3
#   fork + exec("bash-server", "--stdio", "--auth-fd", "3")
#   read token from token_pipe
#   write protocol commands to stdin_pipe
#   read responses from stdout_pipe
```

No socket file or token file is created.  The token is delivered via
`--auth-fd` (default: stderr).

### Fd Mode

Single-session transport for socketpair integration.  The parent creates
a `socketpair()`, passes one end as an inherited fd, and communicates
bidirectionally over a single fd.

```bash
# Use inherited fd 4
bash-server --fd 4

# With separate token delivery fd
bash-server --fd 4 --auth-fd 5
```

This is useful when the parent process creates a Unix socketpair:
```c
int sv[2];
socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
// sv[0] = parent end, sv[1] = child end (becomes fd 4)
```

### Named Pipe Mode (Cygwin only)

Windows Named Pipes bypass Cygwin's `AF_UNIX`-over-TCP-loopback emulation.

```bash
# Start with named pipe
bash-server --named-pipe myserver
```

This creates a Windows Named Pipe at `\\.\pipe\bash-server-myserver` with
an owner-only DACL (equivalent to `chmod 0600`).

Clients connect using the Win32 `CreateFile()` API or any Named Pipe client.
The pipe name is always prefixed with `bash-server-` to avoid collisions.

**Differences from socket mode:**
- No `SO_PEERCRED` issues (Named Pipes have native security)
- Sessions are handled sequentially (no fork -- see architecture.md for why)
- Token file is at `$XDG_RUNTIME_DIR/bash-server/<name>.token` or `/tmp/bash-server-<uid>/<name>.token`
- The `--no-peercred` flag is not needed (and has no effect)

## Usage Examples

### Development Setup

```bash
# Start verbose server with login shell init
bash-server --verbose --login

# In another terminal, connect
SOCK=/tmp/bash-server-$(id -u)/sock
TOKEN=$(cat ${SOCK}.token)
bashclient --socket "$SOCK" --auth "$TOKEN" --interactive
```

### Daemon Deployment

```bash
# Start as daemon with custom socket and init script
bash-server \
    --daemon \
    --socket /run/bash-server/sock \
    --pidfile /run/bash-server/pid \
    --login \
    --init /etc/bash-server/init.sh \
    --max-clients 20

# Stop
kill $(cat /run/bash-server/pid)
```

### Subprocess Integration (stdio)

```bash
# Python parent process example:
#   import subprocess
#   proc = subprocess.Popen(
#       ['bash-server', '--stdio', '--auth-fd', '3'],
#       stdin=subprocess.PIPE,
#       stdout=subprocess.PIPE,
#       pass_fds=(3,)
#   )
#   # Read token from fd 3
#   # Write AUTH command to proc.stdin
#   # Read responses from proc.stdout
```

### Named Pipe (Cygwin)

```bash
# Start server
bash-server --named-pipe dev --verbose

# Connect from Python (Windows):
#   import win32pipe, win32file
#   handle = win32file.CreateFile(
#       r'\\.\pipe\bash-server-dev',
#       win32file.GENERIC_READ | win32file.GENERIC_WRITE,
#       0, None, win32file.OPEN_EXISTING, 0, None
#   )
```
