# Troubleshooting

## bashclient - Diagnosis and Solutions

This document covers common issues encountered when using the bashclient
Python package, with symptoms, causes, and solutions.

---

## Table of Contents

- [Installation Issues](#installation-issues)
  - [Python Version Errors](#python-version-errors)
  - [pip Errors](#pip-errors)
  - [Import Failures](#import-failures)
- [Connection Failures](#connection-failures)
  - [Socket Not Found](#socket-not-found)
  - [Permission Denied](#permission-denied)
  - [Server Not Running](#server-not-running)
  - [Connection Refused](#connection-refused)
  - [Connection Reset](#connection-reset)
- [Authentication Errors](#authentication-errors)
  - [Wrong Token](#wrong-token)
  - [Token File Permissions](#token-file-permissions)
  - [Not Authenticated](#not-authenticated)
- [Timeout Issues](#timeout-issues)
  - [Slow Commands](#slow-commands)
  - [Increasing Timeouts](#increasing-timeouts)
  - [Deadlocked Server](#deadlocked-server)
- [Protocol Errors](#protocol-errors)
  - [Malformed Frames](#malformed-frames)
  - [Version Mismatch](#version-mismatch)
  - [Unexpected Response](#unexpected-response)
  - [Incomplete Reads](#incomplete-reads)
- [Transport Issues](#transport-issues)
  - [Unix Socket Transport](#unix-socket-transport)
  - [stdio Transport](#stdio-transport)
  - [fd Transport](#fd-transport)
  - [Named Pipe Transport](#named-pipe-transport)
- [Platform-Specific Issues](#platform-specific-issues)
  - [Linux](#linux)
  - [macOS](#macos)
  - [Cygwin](#cygwin)
  - [Windows Native](#windows-native)
- [Channel-Specific Issues](#channel-specific-issues)
  - [COMMAND Channel](#command-channel)
  - [STATE Channel](#state-channel)
  - [OBSERVE Channel](#observe-channel)
  - [DEBUG Channel](#debug-channel)
  - [PTY Channel](#pty-channel)
- [Debugging Tips](#debugging-tips)
  - [Enable Logging](#enable-logging)
  - [Protocol Tracing](#protocol-tracing)
  - [Inspecting Server State](#inspecting-server-state)
- [FAQ](#faq)

---

## Installation Issues

### Python Version Errors

**Symptom**: `SyntaxError` or `ImportError` when importing bashclient.

**Cause**: bashclient requires Python 3.8+. Features used include
`dataclasses` (3.7+), `asyncio.run()` (3.7+), and typing features
specific to 3.8+.

**Solution**: Check your Python version:

```bash
python --version
```

If below 3.8, upgrade Python. On Cygwin:

```bash
# Check available versions
cygcheck -l python3

# Install or update
# Use Cygwin setup to install python38 or later
```

### pip Errors

**Symptom**: `pip install` fails with build errors or dependency issues.

**Cause**: Old pip/setuptools, or wrong Python environment.

**Solution**:

```bash
# Upgrade pip and setuptools
python -m pip install --upgrade pip setuptools

# Retry installation
pip install -e clients/python
```

If pip itself is missing:

```bash
python -m ensurepip --upgrade
```

### Import Failures

**Symptom**: `ModuleNotFoundError: No module named 'bashclient'`

**Cause**: Package not installed in the active Python environment.

**Solution**:

```bash
# Verify which python is active
which python
python -m pip list | grep bashclient

# If using a virtualenv, ensure it is activated
source .venv/bin/activate

# Reinstall if needed
pip install -e clients/python
```

---

## Connection Failures

### Socket Not Found

**Symptom**:

```
bashclient.errors.ConnectionError: Socket not found: /tmp/bash-server-1000/sock
```

**Cause**: The socket file does not exist. Either bash-server is not
running, or it is using a different socket path.

**Solution**:

1. Check if bash-server is running:

   ```bash
   pgrep -a bash-server
   ```

2. Find the actual socket path:

   ```bash
   # Check common locations
   ls -la /tmp/bash-server-$(id -u)/sock
   ls -la ${XDG_RUNTIME_DIR}/bash-server/sock
   echo $BASH_SERVER_SOCKET
   ```

3. Start bash-server if not running:

   ```bash
   bash-server --daemonize
   ```

4. Connect to the correct path:

   ```python
   await client.connect("/actual/path/to/sock")
   ```

### Permission Denied

**Symptom**:

```
bashclient.errors.ConnectionError: Permission denied: /tmp/bash-server-1000/sock
```

**Cause**: The socket file exists but the current user does not have
permission to connect. bash-server creates sockets with mode `0600`
(owner only).

**Solution**:

1. Check socket ownership:

   ```bash
   ls -la /tmp/bash-server-1000/sock
   ```

2. Ensure you are connecting as the same user that started bash-server.

3. If running as a different user, start a separate bash-server instance
   for that user.

### Server Not Running

**Symptom**: Connection attempt hangs or returns `ConnectionRefusedError`.

**Cause**: bash-server process has exited but the socket file remains
(stale socket).

**Solution**:

1. Check if bash-server is running:

   ```bash
   pgrep bash-server
   ```

2. Remove the stale socket and restart:

   ```bash
   rm -f /tmp/bash-server-$(id -u)/sock
   bash-server --daemonize
   ```

### Connection Refused

**Symptom**:

```
bashclient.errors.ConnectionError: Connection refused
```

**Cause**: The socket exists but nothing is listening on it.

**Solution**: Same as [Server Not Running](#server-not-running). Remove
the stale socket and restart bash-server.

### Connection Reset

**Symptom**:

```
bashclient.errors.ConnectionError: Connection reset by peer
```

**Cause**: bash-server closed the connection unexpectedly. Possible
reasons:

- Server crashed
- Maximum client limit reached (`--max-clients`)
- Server shutdown while client was connected

**Solution**:

1. Check server logs for crash information
2. Reconnect with a new client instance
3. Increase `--max-clients` if the limit is being hit

---

## Authentication Errors

### Wrong Token

**Symptom**:

```
bashclient.errors.AuthenticationError: Authentication failed: invalid token
```

**Cause**: The token passed to `auth()` does not match the server's
token.

**Solution**:

1. Read the token from the server's token file:

   ```bash
   cat /tmp/bash-server-$(id -u)/token
   ```

2. Or read from the configured location:

   ```bash
   cat ~/.bash-server/token
   ```

3. Pass the correct token:

   ```python
   with open("/tmp/bash-server-1000/token") as f:
       token = f.read().strip()
   await client.auth(token=token)
   ```

### Token File Permissions

**Symptom**: Cannot read the token file.

**Cause**: Token file permissions are restrictive (mode `0600`).

**Solution**:

1. Check permissions:

   ```bash
   ls -la /tmp/bash-server-$(id -u)/token
   ```

2. Ensure you are reading as the correct user. The token file is
   owned by the user who started bash-server.

### Not Authenticated

**Symptom**:

```
bashclient.errors.AuthenticationError: Not authenticated
```

**Cause**: Attempting to use channel methods (eval, get_var, etc.)
before calling `auth()`.

**Solution**: Always authenticate after connecting:

```python
client = BashClient()
await client.connect(socket_path)
await client.auth(token=token)       # Must call this first
result = await client.eval("echo hi")  # Now this works
```

---

## Timeout Issues

### Slow Commands

**Symptom**:

```
bashclient.errors.TimeoutError: eval timed out after 30.0 seconds
```

**Cause**: The command takes longer than the timeout to complete.

**Solution**: Increase the timeout for long-running commands:

```python
# Per-call timeout
result = await client.eval("make -j12", timeout=300.0)

# Or set a higher default
client = BashClient(timeout=120.0)
```

### Increasing Timeouts

**Default timeout**: 30 seconds.

You can override at multiple levels:

```python
# 1. Constructor default (applies to all operations)
client = BashClient(timeout=60.0)

# 2. Per-call override (applies to one operation)
result = await client.eval("sleep 10", timeout=15.0)

# 3. Disable timeout (not recommended)
result = await client.eval("long-command", timeout=None)
```

### Deadlocked Server

**Symptom**: All operations time out. Server process exists but is
unresponsive.

**Cause**: bash-server internal deadlock or resource exhaustion.

**Solution**:

1. Check if the server process is stuck:

   ```bash
   ps aux | grep bash-server
   kill -0 <pid>  # check if alive
   ```

2. Kill and restart:

   ```bash
   kill <pid>
   rm -f /tmp/bash-server-$(id -u)/sock
   bash-server --daemonize
   ```

3. Create a new client and reconnect.

---

## Protocol Errors

### Malformed Frames

**Symptom**:

```
bashclient.errors.ProtocolError: Failed to decode frame: ...
```

**Cause**: Server sent data that is not valid NDJSON. Possible reasons:

- Binary data mixed into the text stream
- Server bug producing invalid JSON
- Corrupted data on the transport

**Solution**:

1. Enable protocol tracing (see [Debugging Tips](#debugging-tips))
2. Check if the server version supports NDJSON
3. Report as a bug with the raw bytes if reproducible

### Version Mismatch

**Symptom**: Connection succeeds but responses are garbled or
`ProtocolError` is raised on every operation.

**Cause**: bashclient uses NDJSON (v2) but the server expects v1
line-oriented protocol, or binary v2.

**Solution**:

1. Ensure bash-server supports NDJSON. Check version:

   ```bash
   bash-server --version
   ```

2. bashclient auto-negotiates NDJSON by sending an NDJSON frame first.
   If the server does not support auto-detection, the connection will
   fail.

3. Upgrade bash-server to a version supporting NDJSON.

### Unexpected Response

**Symptom**:

```
bashclient.errors.ProtocolError: Unexpected op 'xyz' on channel N
```

**Cause**: The server sent a response with an operation type that the
client does not recognize.

**Solution**:

1. This may indicate a server version newer than the client. Update
   bashclient to match the server version.
2. If using a custom server extension, the client needs corresponding
   channel method updates.

### Incomplete Reads

**Symptom**: Operations hang or return partial data.

**Cause**: The transport read buffer received a partial JSON line.
This should not happen with properly framed NDJSON but can occur
with misconfigured transports.

**Solution**:

1. The `read_line()` implementation buffers until a complete line is
   received. If you are using a custom transport, ensure `read_line()`
   does not return partial lines.
2. Check for network issues between client and server.

---

## Transport Issues

### Unix Socket Transport

**Problem**: `OSError: [Errno 111] Connection refused`

**Cause**: Stale socket file. The server process that created the socket
has exited.

**Fix**: Remove the socket and restart:

```bash
rm /tmp/bash-server-$(id -u)/sock
bash-server --daemonize
```

**Problem**: `OSError: AF_UNIX path too long`

**Cause**: Socket path exceeds the OS limit (108 bytes on Linux, 104 on
macOS).

**Fix**: Use a shorter socket path:

```python
await client.connect("/tmp/bs/sock")
```

Or configure bash-server with `--socket /tmp/bs/sock`.

### stdio Transport

**Problem**: `FileNotFoundError: [Errno 2] No such file or directory: 'bash-server'`

**Cause**: bash-server is not on PATH.

**Fix**: Use the full path:

```python
await client.connect_stdio(["/usr/local/bin/bash-server", "--stdio"])
```

**Problem**: Server exits immediately after connect.

**Cause**: bash-server may fail to initialize (missing libraries, config
errors).

**Fix**: Run bash-server manually first to see error output:

```bash
bash-server --stdio
```

Check stderr for initialization errors.

**Problem**: Reads return empty, client disconnects.

**Cause**: Server writing to stderr instead of stdout. The stdio
transport reads from stdout.

**Fix**: Ensure bash-server `--stdio` mode writes protocol frames to
stdout and diagnostic messages to stderr.

### fd Transport

**Problem**: `OSError: [Errno 9] Bad file descriptor`

**Cause**: The file descriptors passed to `connect_fd()` are not valid
or have already been closed.

**Fix**: Verify FDs are open before connecting:

```python
import os
os.fstat(read_fd)   # Raises if FD is invalid
os.fstat(write_fd)
await client.connect_fd(read_fd, write_fd)
```

**Problem**: fd transport not available on Windows.

**Cause**: Windows does not use POSIX file descriptors.

**Fix**: Use stdio or Named Pipe transport on Windows.

### Named Pipe Transport

**Problem**: `FileNotFoundError: Named pipe not found`

**Cause**: The Named Pipe does not exist. bash-server may not be
running with `--named-pipe`.

**Fix**: Start bash-server with Named Pipe transport:

```bash
bash-server --named-pipe
```

**Problem**: `PermissionError` on Named Pipe.

**Cause**: DACL security on the pipe restricts access.

**Fix**: bash-server creates pipes with owner-only access. Ensure you
are connecting as the same user.

---

## Platform-Specific Issues

### Linux

**Issue**: `asyncio.open_unix_connection` raises `OSError`.

**Possible causes**:
- SELinux policy blocking socket connections
- AppArmor profile restricting access
- File system mounted with `nosock` option (rare)

**Diagnostics**:

```bash
# Check SELinux
getenforce
ausearch -m avc | tail

# Check AppArmor
aa-status
```

### macOS

**Issue**: Socket path length limit.

macOS has a 104-byte limit for Unix socket paths (vs 108 on Linux).
Deep directory nesting can exceed this.

**Fix**: Use short socket paths. Configure bash-server:

```bash
bash-server --socket /tmp/bs.sock
```

**Issue**: System Integrity Protection (SIP).

SIP does not typically affect socket operations, but if bash-server is
installed in a protected location, it may have restricted capabilities.

### Cygwin

**Issue**: `SO_PEERCRED` not supported.

Cygwin's Unix socket implementation does not support `SO_PEERCRED` for
peer credential checking. bash-server will reject connections that
attempt this handshake.

**Fix**: Start bash-server with `--no-peercred`:

```bash
bash-server --no-peercred --daemonize
```

**Issue**: Path translation between Cygwin and Windows.

When using Named Pipe transport, pipe names use Windows conventions
(`\\.\pipe\name`) even from Cygwin Python.

**Fix**: Use Windows-style pipe names:

```python
await client.connect_named_pipe(r"\\.\pipe\bash-server")
```

**Issue**: Cygwin Python vs Windows Python confusion.

Cygwin Python and Windows Python are separate installations. bashclient
works with both, but transport availability differs.

**Fix**: Check which Python you are using:

```bash
python -c "import sys; print(sys.platform)"
# Cygwin: 'cygwin'
# Windows: 'win32'
```

Use Cygwin Python for Unix socket transport. Use either for Named Pipe
transport.

### Windows Native

**Issue**: `AF_UNIX` not available.

Windows native Python before 3.9, or Windows before build 17063, does
not support `AF_UNIX` sockets.

**Fix**: Use stdio or Named Pipe transport:

```python
# stdio transport
await client.connect_stdio(["bash-server.exe", "--stdio"])

# Named Pipe transport
await client.connect_named_pipe(r"\\.\pipe\bash-server")
```

**Issue**: `asyncio` event loop policy.

On Windows, the default event loop policy may use `ProactorEventLoop`
which has different capabilities from `SelectorEventLoop`.

**Fix**: For best compatibility, use the selector event loop:

```python
import asyncio
if sys.platform == "win32":
    asyncio.set_event_loop_policy(asyncio.WindowsSelectorEventLoopPolicy())
```

---

## Channel-Specific Issues

### COMMAND Channel

**Issue**: eval returns empty stdout for commands that produce output.

**Cause**: The command's output goes to stderr instead of stdout, or the
command has not completed within the timeout.

**Fix**: Check `result.stderr` as well as `result.stdout`. Increase
timeout if the command is slow.

**Issue**: Exit code is always 0.

**Cause**: Some commands exit 0 even on failure (e.g., commands piped
through `tee`). This is shell behavior, not a bashclient issue.

### STATE Channel

**Issue**: `get_var` returns None for a variable that exists.

**Cause**: The variable may be in a subshell or child process scope
that bash-server does not inherit.

**Fix**: Set variables in the server's global scope via `set_var`
or `eval("export VAR=value")`.

### OBSERVE Channel

**Issue**: No events received after `observe_start`.

**Cause**: No commands have been executed since subscribing, or the
observe level is too low.

**Fix**:

```python
await client.observe_start(level="detailed")
# Now execute commands in another session or the same one
await client.eval("echo trigger")
```

### DEBUG Channel

**Issue**: Breakpoint not hitting.

**Cause**: The breakpoint target does not match any executed command.

**Fix**: Verify the breakpoint target matches exactly. Use
`inspect_ast()` to see the command structure.

### PTY Channel

**Issue**: PTY output contains ANSI escape sequences.

**Cause**: The spawned program outputs terminal escape codes.

**Fix**: Enable ANSI stripping:

```python
await client.pty_spawn("bash", rows=24, cols=80, strip_ansi=True)
```

**Issue**: PTY input not echoed.

**Cause**: PTY echo may be disabled by the spawned program.

**Fix**: This is expected for programs like `passwd` or editors.
The input is received by the process but not reflected in output.

---

## Debugging Tips

### Enable Logging

bashclient uses Python's `logging` module. Enable debug logging to see
internal operations:

```python
import logging
logging.basicConfig(level=logging.DEBUG)

# Or target just bashclient
logger = logging.getLogger("bashclient")
logger.setLevel(logging.DEBUG)
handler = logging.StreamHandler()
handler.setFormatter(logging.Formatter(
    "%(asctime)s %(name)s %(levelname)s: %(message)s"
))
logger.addHandler(handler)
```

This shows:

- Transport connect/disconnect events
- Frames sent and received (with content)
- Reader loop dispatch decisions
- Timeout and error details

### Protocol Tracing

For detailed protocol analysis, capture raw bytes:

```python
import logging
logging.getLogger("bashclient.protocol").setLevel(logging.DEBUG)
```

This logs every frame encoded and decoded, including the raw bytes.

For external tracing, use `socat` to proxy the socket:

```bash
# Proxy with hex dump
socat -x UNIX-LISTEN:/tmp/proxy.sock,fork UNIX-CONNECT:/tmp/bash-server-1000/sock
```

Then connect the client to `/tmp/proxy.sock`.

### Inspecting Server State

Use the STATE channel to inspect the server's internal state:

```python
# List all variables
result = await client.inspect("variables")
for item in result.items:
    print(f"{item.name}={item.value}")

# List all functions
result = await client.inspect("functions")
for item in result.items:
    print(f"{item.name}: {item.body[:50]}...")
```

---

## FAQ

### Q: Does bashclient work with bash-server v1 protocol?

No. bashclient implements the v2 NDJSON protocol only. For v1
(line-oriented text protocol), use the C bashclient or a simple
script with `socat`.

### Q: Can I use bashclient without asyncio?

Not directly. bashclient is built on asyncio. However, you can use
`asyncio.run()` to call async methods from synchronous code:

```python
import asyncio
from bashclient import BashClient

def sync_eval(cmd):
    async def _run():
        client = BashClient()
        await client.connect("/tmp/bash-server-1000/sock")
        await client.auth(token="...")
        result = await client.eval(cmd)
        await client.close()
        return result
    return asyncio.run(_run())

result = sync_eval("echo hello")
print(result.stdout)
```

### Q: Can I use multiple BashClient instances simultaneously?

Yes. Each BashClient has its own transport, reader loop, and channel
queues. You can connect multiple clients to the same or different
bash-server instances.

### Q: Is bashclient thread-safe?

No. bashclient is designed for asyncio single-threaded concurrency.
Do not share a BashClient instance across threads. If you need
multi-threaded access, create a separate client per thread and use
`asyncio.run()` in each.

### Q: How do I handle server disconnections?

Check `client.is_connected` before operations, or catch
`ConnectionError`:

```python
try:
    result = await client.eval("echo test")
except bashclient.errors.ConnectionError:
    # Reconnect
    await client.connect(socket_path)
    await client.auth(token=token)
    result = await client.eval("echo test")
```

### Q: What is the maximum message size?

NDJSON frames have no hard size limit in bashclient. However, extremely
large frames (e.g., multi-megabyte stdout) are streamed as multiple
`stdout` chunks by the server. The assembled result is limited by
available memory.

### Q: Can I use bashclient with Python 3.7?

No. Python 3.8 is the minimum supported version. Python 3.7 is
end-of-life and lacks some typing features used by bashclient.
