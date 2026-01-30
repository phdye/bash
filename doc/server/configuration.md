# bash-server Configuration and Operations Guide

**Audience:** System administrators, operators, and integration developers.

## Command-Line Reference

### bash-server

```
bash-server [OPTIONS]
```

| Option | Short | Argument | Default | Description |
|--------|-------|----------|---------|-------------|
| `--socket PATH` | `-s` | Required | (auto) | Unix socket path |
| `--daemon` | `-d` | None | Off | Daemonize (background) |
| `--pidfile PATH` | `-p` | Required | None | Write PID to file |
| `--max-clients N` | `-m` | Required | 10 | Maximum simultaneous clients (reserved) |
| `--no-peercred` | `-P` | None | Off | Disable Cygwin credential handshake |
| `--verbose` | `-v` | None | Off | Verbose logging to stderr |
| `--help` | `-h` | None | — | Print usage and exit |
| `--version` | `-V` | None | — | Print version and exit |

### bashclient

```
bashclient [OPTIONS]
```

| Option | Short | Argument | Default | Description |
|--------|-------|----------|---------|-------------|
| `--socket PATH` | `-s` | Required | — | Unix socket path (required) |
| `--auth TOKEN` | `-a` | Required | — | Authentication token (required) |
| `--eval CMD` | `-e` | Required | — | Execute single command |
| `--file SCRIPT` | `-f` | Required | — | Execute commands from file |
| `--interactive` | `-i` | None | Auto | Interactive prompt mode |
| `--verbose` | `-v` | None | Off | Verbose output |
| `--help` | `-h` | None | — | Print usage and exit |
| `--version` | `-V` | None | — | Print version and exit |

**Mode selection:**
- If `--eval` is given, execute that command and exit.
- If `--file` is given, execute each line of the file and exit.
- If `--interactive` is given, enter interactive prompt mode.
- If none specified and stdin is a TTY, default to interactive mode.
- If none specified and stdin is not a TTY, print an error and exit.

## Socket Path Resolution

The server resolves its socket path using a priority hierarchy.  The first
match wins:

| Priority | Source | Example |
|----------|--------|---------|
| 1 | `--socket PATH` (CLI) | `--socket /run/bash-server/sock` |
| 2 | `$BASH_SERVER_SOCKET` (env) | `export BASH_SERVER_SOCKET=/tmp/my.sock` |
| 3 | `~/.bash-serverrc` (config file) | `socket /custom/path/sock` |
| 4 | `$XDG_RUNTIME_DIR/bash-server/sock` | `/run/user/1000/bash-server/sock` |
| 5 | `/tmp/bash-server-<uid>/sock` | `/tmp/bash-server-1000/sock` |

### Default Path Behavior

On a typical Cygwin system without `$XDG_RUNTIME_DIR`:

```
Socket: /tmp/bash-server-<uid>/sock
Token:  /tmp/bash-server-<uid>/sock.token
```

On a Linux system with systemd user session:

```
Socket: /run/user/<uid>/bash-server/sock
Token:  /run/user/<uid>/bash-server/sock.token
```

### Directory Creation

The server automatically creates the parent directory for the socket
with mode `0700` if it does not exist.  It will not create nested
parent directories — only the immediate parent.

## Configuration File

### Location

```
~/.bash-serverrc
```

The file is optional.  If absent, no error is raised.

### Format

Plain text, one directive per line:

```
# Comment lines start with #
# Blank lines are ignored

socket /path/to/socket
```

### Directives

| Directive | Arguments | Description |
|-----------|-----------|-------------|
| `socket` | PATH | Unix socket path |

**Parsing rules:**
- Lines beginning with `#` (after optional whitespace) are comments.
- Blank lines and whitespace-only lines are ignored.
- Leading and trailing whitespace on value is trimmed.
- Directives are case-sensitive.
- Only the first matching directive is used (no duplicate handling needed).
- CLI and environment settings take precedence — the config file only
  fills in values not already set.

### Example

```bash
# ~/.bash-serverrc
# Use XDG-style runtime directory
socket /run/user/1000/bash-server/sock
```

## Authentication

### Token Generation

On startup, the server generates a 256-bit (32-byte) random token:

1. Read 32 bytes from `/dev/urandom`.
2. Hex-encode to 64 lowercase hexadecimal characters.
3. Write to `<socket-path>.token` (mode `0600`, `O_CREAT | O_EXCL`).
4. Append a trailing newline for shell convenience (`cat`, `read`).
5. Zero the raw random bytes on the stack.

### Token File

| Property | Value |
|----------|-------|
| Path | `<socket-path>.token` |
| Permissions | `0600` (owner read/write only) |
| Contents | 64 hex characters + newline (65 bytes total) |
| Lifecycle | Created on startup, deleted on shutdown |
| Creation mode | `O_WRONLY | O_CREAT | O_EXCL` (fails if exists) |

Stale token files from previous runs are unlinked before creation.

### Client Authentication

```bash
# Read the token
TOKEN=$(cat /tmp/bash-server-$(id -u)/sock.token)

# Use with bashclient
bashclient -s /tmp/bash-server-$(id -u)/sock -a "$TOKEN" -e 'whoami'

# Use with raw protocol
echo -e "AUTH $TOKEN\nEVAL whoami\nQUIT" | socat - UNIX-CONNECT:/tmp/bash-server-$(id -u)/sock
```

## Daemon Mode

### Starting as a Daemon

```bash
bash-server --daemon --pidfile /tmp/bash-server.pid --verbose
```

**Daemonization process (double-fork):**

1. First `fork()` — parent exits.
2. Child calls `setsid()` to become session leader.
3. Second `fork()` — first child exits (prevents acquiring controlling terminal).
4. Grandchild calls `chdir("/")`.
5. Close stdin/stdout/stderr.
6. Redirect all three to `/dev/null`.

**Note:** When `--daemon` is combined with `--verbose`, verbose messages are
written to stderr *before* daemonization.  After daemonization, stderr goes
to `/dev/null` and verbose output is lost.  For production daemon logging,
use an external process manager or redirect stderr before starting.

### PID File

When `--pidfile` is specified:

| Property | Value |
|----------|-------|
| Contents | PID as decimal integer + newline |
| Created | After daemonization (contains daemon PID, not parent) |
| Deleted | On clean shutdown (SIGINT/SIGTERM) |
| Permissions | Default umask |

### Stopping a Daemon

```bash
# Using PID file
kill $(cat /tmp/bash-server.pid)

# Using process name
kill $(pgrep bash-server)

# Verify stopped
ls /tmp/bash-server-$(id -u)/sock  # Should not exist
```

## Shutdown Behavior

On receipt of SIGINT or SIGTERM, the server:

1. Sets `server_running = 0` (signal handler).
2. The `accept()` call returns `EINTR` (SA_RESTART not set for these signals).
3. The main loop exits.
4. `server_shutdown()` is called:
   - Closes the listening socket file descriptor.
   - Unlinks the socket file.
   - Unlinks the token file.
   - Unlinks the PID file (if any).
   - Reaps zombie child processes.

**If the server crashes** (SIGSEGV, SIGABRT, power loss):
- The socket file remains on disk (stale).
- The token file remains on disk (stale).
- The PID file remains on disk (stale PID).
- On next startup, the server unlinks the stale socket file before binding.
- The token file is unlinked and re-created with `O_EXCL`.

## File Permissions Summary

| File | Created by | Mode | Purpose |
|------|-----------|------|---------|
| Socket directory | `ensure_directory()` | `0700` | Contains socket and token |
| Socket file | `bind()` + `chmod()` | `0600` | Unix domain socket |
| Token file | `open(O_CREAT\|O_EXCL)` | `0600` | Authentication token |
| PID file | `fopen("w")` | umask | Daemon PID |

All security-sensitive files are restricted to owner access only.

## Environment Variables

| Variable | Used by | Description |
|----------|---------|-------------|
| `BASH_SERVER_SOCKET` | Server | Socket path (priority 2) |
| `XDG_RUNTIME_DIR` | Server | Base for default socket path (priority 4) |
| `HOME` | Server | Used to locate `~/.bash-serverrc` |

## Operational Patterns

### Health Check

```bash
SOCK=/tmp/bash-server-$(id -u)/sock
echo PING | socat - UNIX-CONNECT:$SOCK
# Expected: PONG
```

Or via bashclient:

```bash
# No direct PING support in bashclient, but a trivial EVAL works:
bashclient -s $SOCK -a $(cat ${SOCK}.token) -e 'echo ok'
```

### Scripted Use

```bash
#!/bin/bash
SOCK=/tmp/bash-server-$(id -u)/sock
TOKEN=$(cat ${SOCK}.token)

# Execute commands from a script
bashclient -s "$SOCK" -a "$TOKEN" -f commands.sh

# Or pipe commands
echo 'echo hello' | bashclient -s "$SOCK" -a "$TOKEN" -f /dev/stdin
```

### Process Manager Integration (systemd example)

```ini
[Unit]
Description=Bash Server Daemon
After=network.target

[Service]
Type=forking
ExecStart=/usr/local/bin/bash-server --daemon --pidfile /run/bash-server.pid
ExecStop=/bin/kill -TERM $MAINPID
PIDFile=/run/bash-server.pid
Restart=on-failure
User=appuser
RuntimeDirectory=bash-server

[Install]
WantedBy=multi-user.target
```

### Cygwin Service (cygrunsrv)

```bash
cygrunsrv --install bash-server \
  --path /usr/local/bin/bash-server \
  --args "--verbose" \
  --user SYSTEM \
  --desc "Bash Server Daemon"

cygrunsrv --start bash-server
cygrunsrv --stop bash-server
```

Note: For Cygwin service use, do **not** pass `--daemon` since cygrunsrv
manages the process lifecycle.  Use `--verbose` for logging to the
service's stdout/stderr which cygrunsrv captures.

## Troubleshooting

### Cannot Connect

| Symptom | Cause | Fix |
|---------|-------|-----|
| `connect: No such file or directory` | Socket file missing | Start the server |
| `connect: Connection refused` | Stale socket file | Remove file, restart server |
| `connect: Permission denied` | Socket permissions | Check file ownership/mode |
| `ECONNABORTED` (errno 113) | Cygwin peercred race | Use `--no-peercred` flag |

### Authentication Failures

| Symptom | Cause | Fix |
|---------|-------|-----|
| `ERR invalid token` | Wrong token | Re-read token file |
| `ERR token required` | Empty AUTH argument | Check token variable |
| Token file missing | Server not running or crashed | Restart server |
| Token file has wrong permissions | Manual tampering | Restart server to regenerate |

### Server Won't Start

| Symptom | Cause | Fix |
|---------|-------|-----|
| `cannot create socket: Address already in use` | Another server running | Stop existing server |
| `cannot create <dir>: Permission denied` | Directory permissions | Fix parent directory permissions |
| `cannot open /dev/urandom` | Restricted environment | Ensure /dev/urandom is available |
| `short read from /dev/urandom` | System entropy issue | Rare; check kernel entropy pool |
