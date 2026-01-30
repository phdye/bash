# Examples

## bashclient Example Scripts

This directory contains example scripts demonstrating how to use the
bashclient Python package. Each example focuses on a specific set of
protocol channels and can be run independently.

---

## Table of Contents

- [Overview](#overview)
- [Prerequisites](#prerequisites)
- [Examples Summary](#examples-summary)
- [eval.py -- Basic Command Evaluation](#evalpy----basic-command-evaluation)
- [observe.py -- Command Observation Events](#observepy----command-observation-events)
- [debugger.py -- Breakpoints and Stepping](#debuggerpy----breakpoints-and-stepping)
- [pty.py -- Interactive PTY Session](#ptypy----interactive-pty-session)
- [Common Patterns](#common-patterns)

---

## Overview

These examples demonstrate the four main use cases of bashclient:

1. **Evaluating commands** and reading results (COMMAND channel)
2. **Observing execution** with pre/post command events (OBSERVE channel)
3. **Debugging scripts** with breakpoints and stepping (DEBUG channel)
4. **Interactive terminals** via PTY sessions (PTY channel)

All examples use async/await with `asyncio.run()` as the entry point.
They are designed to be readable and self-contained.

---

## Prerequisites

### bash-server

All examples require a running bash-server instance. Start one with:

```bash
# Option 1: Daemonize (runs in background)
bash-server --daemonize

# Option 2: Foreground (useful for debugging)
bash-server

# Option 3: stdio mode (used by debugger.py)
# No need to start manually -- the example launches it as a subprocess
```

### Authentication Token

After starting bash-server, note the authentication token. It is
printed to stderr on startup and also written to a token file:

```bash
# Read the token
cat /tmp/bash-server-$(id -u)/token
```

Some examples read the token from a file path or environment variable.
Set the token before running:

```bash
export BASH_SERVER_TOKEN=$(cat /tmp/bash-server-$(id -u)/token)
```

### bashclient Package

Install bashclient if you have not already:

```bash
pip install -e clients/python
```

### Python Version

All examples require Python 3.8 or later.

---

## Examples Summary

| File          | Description                          | Channels Used              | Transport     |
|---------------|--------------------------------------|----------------------------|---------------|
| `eval.py`     | Basic command evaluation             | CONTROL, COMMAND           | Unix socket   |
| `observe.py`  | Command observation events           | CONTROL, COMMAND, OBSERVE  | Unix socket   |
| `debugger.py` | Breakpoints and stepping             | CONTROL, COMMAND, DEBUG    | stdio         |
| `pty.py`      | Interactive PTY session              | CONTROL, PTY               | Unix socket   |

---

## eval.py -- Basic Command Evaluation

### Description

Demonstrates the most common use case: connecting to bash-server,
authenticating, evaluating shell commands, and reading the results.

This example shows:

- Connecting via Unix socket
- Reading the auth token from a file
- Evaluating simple commands with `client.eval()`
- Accessing stdout, stderr, and exit code from `EvalResult`
- Evaluating commands that produce stderr output
- Handling non-zero exit codes
- Using `eval_parsed()` with a pre-built COMMAND AST
- Proper cleanup with `async with` context manager

### Run

```bash
# Ensure bash-server is running
bash-server --daemonize

# Run the example
python examples/eval.py
```

Or with an explicit socket path:

```bash
BASH_SERVER_SOCKET=/tmp/bash-server-1000/sock python examples/eval.py
```

### Expected Output

```
Connected to bash-server
Authenticated successfully

--- Simple command ---
$ echo "Hello, world!"
stdout: Hello, world!
exit_code: 0

--- Command with stderr ---
$ ls /nonexistent 2>&1
stderr: ls: cannot access '/nonexistent': No such file or directory
exit_code: 2

--- Multi-line output ---
$ seq 1 5
stdout:
1
2
3
4
5

--- Environment variable ---
$ echo $HOME
stdout: /home/user

--- Command with duration ---
$ sleep 0.1 && echo done
stdout: done
duration: 0.105s

Done.
```

---

## observe.py -- Command Observation Events

### Description

Demonstrates subscribing to command observation events. When observation
is active, the server sends `pre_command` and `post_command` events for
every command that executes in the session.

This example shows:

- Subscribing to observe events with `observe_start()`
- Registering a callback with `on_observe()`
- Executing commands that trigger observation events
- Reading event fields: type, command, cwd, timestamp, exit_code, duration
- Using the "detailed" observation level for extra fields
- Unsubscribing with `observe_stop()`

### Run

```bash
# Ensure bash-server is running
bash-server --daemonize

# Run the example
python examples/observe.py
```

### Expected Output

```
Connected and authenticated.
Observation started (level: detailed)

Executing: echo hello
  [observe] pre_command: echo hello
    cwd: /home/user
  [observe] post_command: echo hello
    exit_code: 0, duration: 0.001s

Executing: ls /tmp
  [observe] pre_command: ls /tmp
    cwd: /home/user
  [observe] post_command: ls /tmp
    exit_code: 0, duration: 0.003s

Executing: false
  [observe] pre_command: false
    cwd: /home/user
  [observe] post_command: false
    exit_code: 1, duration: 0.001s

Observation stopped.
Total events received: 6
Done.
```

---

## debugger.py -- Breakpoints and Stepping

### Description

Demonstrates the debug channel for setting breakpoints, stepping through
commands, and inspecting the AST. This example uses the stdio transport
to launch bash-server as a subprocess, which simplifies setup.

This example shows:

- Connecting via stdio transport (subprocess)
- Setting a function breakpoint with `breakpoint_set()`
- Registering a debug event callback with `on_debug()`
- Executing a script that triggers the breakpoint
- Stepping through commands with `debug_step()` and `debug_next()`
- Inspecting the current AST with `inspect_ast()`
- Continuing execution with `debug_continue()`
- Clearing breakpoints with `breakpoint_clear()`

### Run

```bash
# No need to start bash-server separately -- stdio transport launches it
python examples/debugger.py
```

**Note**: This example requires `bash-server` to be on PATH.

### Expected Output

```
Connected via stdio transport.
Authenticated.

Setting breakpoint on function 'greet'...
Breakpoint set: id=1, type=function, target=greet

Defining function and calling it...
  [debug] breakpoint_hit at greet
  AST type: function_def

Stepping into function body...
  [debug] step_complete
  AST: {"type": "simple", "words": ["echo", "Hello,", "$1"]}

Continuing execution...
  [debug] step_complete (function returned)

Command result:
  stdout: Hello, World

Clearing breakpoint...
Breakpoint 1 cleared.

Done.
```

---

## pty.py -- Interactive PTY Session

### Description

Demonstrates spawning a pseudo-terminal (PTY) session, sending input,
and receiving output. This enables interactive terminal emulation
through the bash-server protocol.

This example shows:

- Spawning a PTY with `pty_spawn()` and custom dimensions
- Registering a PTY event callback with `on_pty()`
- Sending commands via `pty_write()`
- Receiving output events asynchronously
- Resizing the terminal with `pty_resize()`
- Sending signals with `pty_signal()`
- Closing the PTY session with `pty_close()`
- Using `strip_ansi=True` for clean output

### Run

```bash
# Ensure bash-server is running
bash-server --daemonize

# Run the example
python examples/pty.py
```

### Expected Output

```
Connected and authenticated.

Spawning PTY (40x120, strip_ansi=True)...
PTY spawned.

Sending: echo "PTY is working"
  [pty] output: $ echo "PTY is working"
  [pty] output: PTY is working
  [pty] output: $

Sending: pwd
  [pty] output: $ pwd
  [pty] output: /home/user
  [pty] output: $

Resizing terminal to 50x200...
Terminal resized.

Sending: tput cols && tput lines
  [pty] output: $ tput cols && tput lines
  [pty] output: 200
  [pty] output: 50
  [pty] output: $

Sending Ctrl+C (SIGINT)...
Signal sent.

Closing PTY...
  [pty] exit: code=0
PTY closed.

Done.
```

---

## Common Patterns

### Connection and Authentication

All examples follow the same connection pattern:

```python
import asyncio
import os
from bashclient import BashClient

async def main():
    async with BashClient() as client:
        # Connect (choose one transport)
        socket_path = os.environ.get(
            "BASH_SERVER_SOCKET",
            f"/tmp/bash-server-{os.getuid()}/sock"
        )
        await client.connect(socket_path)

        # Authenticate
        token_path = os.environ.get(
            "BASH_SERVER_TOKEN_FILE",
            f"/tmp/bash-server-{os.getuid()}/token"
        )
        with open(token_path) as f:
            token = f.read().strip()
        await client.auth(token=token)

        # ... use the client ...

asyncio.run(main())
```

### Error Handling

All examples should handle common errors:

```python
from bashclient.errors import (
    ConnectionError,
    AuthenticationError,
    TimeoutError,
)

try:
    await client.connect(path)
except ConnectionError as e:
    print(f"Connection failed: {e}")
    return

try:
    await client.auth(token=token)
except AuthenticationError as e:
    print(f"Auth failed: {e}")
    return

try:
    result = await client.eval("some-command", timeout=10.0)
except TimeoutError:
    print("Command timed out")
```

### Callback Registration

For channels with server-push events (OBSERVE, DEBUG, PTY), register
callbacks before starting the relevant operation:

```python
# Register callback FIRST
events = []
def collect(event):
    events.append(event)

client.on_observe(collect)

# THEN start observation
await client.observe_start(level="detailed")

# Execute commands (events will be collected)
await client.eval("echo test")

# Stop and process
await client.observe_stop()
print(f"Collected {len(events)} events")
```

### Async Callbacks

Callbacks can be async functions:

```python
async def async_handler(event):
    # Can use await inside callbacks
    print(f"Event: {event.type}")
    if event.type == "post_command" and event.exit_code != 0:
        # React to failures
        await client.eval("echo 'Command failed!'")

client.on_observe(async_handler)
```

### Timeout Management

Set appropriate timeouts for different operations:

```python
# Quick commands
result = await client.eval("echo fast", timeout=5.0)

# Slow commands (builds, downloads)
result = await client.eval("make -j12", timeout=300.0)

# Global default
client = BashClient(timeout=60.0)
```
