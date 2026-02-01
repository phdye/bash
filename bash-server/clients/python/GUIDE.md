# Usage Guide -- bashclient (Python)

Comprehensive guide to the async Python client for the bash-server v2 NDJSON
protocol. This library gives you full programmatic control over a persistent
Bash interpreter from Python -- executing commands, managing shell state,
observing execution, debugging scripts, and driving a pseudo-terminal -- all
over a multiplexed JSON protocol with async/await.

**Intended audience:** Python developers integrating with `bash-server`,
building automation harnesses, remote shells, or scripted workflows.

**Assumes:** You have a running `bash-server` instance (see
`doc/server/README.md`), Python 3.8+, and basic familiarity with `asyncio`.

**Companion docs:**
- `API.md` -- complete method signatures, parameter types, return types
- `doc/server/protocol.md` -- wire protocol specification
- `doc/server/channels.md` -- v2 channel architecture with JSON schemas
- `doc/server/configuration.md` -- server CLI options and transport modes

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Core Concepts](#core-concepts)
3. [Getting Started](#getting-started)
4. [Transports](#transports)
5. [Command Execution (COMMAND Channel)](#command-execution-command-channel)
6. [Shell State Management (STATE Channel)](#shell-state-management-state-channel)
7. [Observability (OBSERVE Channel)](#observability-observe-channel)
8. [Debugging (DEBUG Channel)](#debugging-debug-channel)
9. [Terminal Emulation (PTY Channel)](#terminal-emulation-pty-channel)
10. [Error Handling](#error-handling)
11. [Best Practices](#best-practices)
12. [Recipes](#recipes)

---

## Prerequisites

### Software Requirements

- **Python 3.8+** (3.10+ recommended for performance)
- **bash-server** compiled and accessible (see `doc/server/developer.md`)
- No external dependencies -- the library uses only the Python standard library

### Starting bash-server

Before any client can connect, the server must be running. The server supports
several transport modes; the most common is Unix domain socket:

```bash
# Start with default socket path ($XDG_RUNTIME_DIR or /tmp/bash-server-$UID/sock)
bash-server

# Start with an explicit socket path
bash-server --socket /tmp/my-server/sock

# Start in stdio mode (no socket; client talks over stdin/stdout)
bash-server --stdio

# Start with a specific auth token (64 hex chars)
bash-server --token "$(head -c 32 /dev/urandom | xxd -p)"

# Daemonize
bash-server --daemonize --socket /tmp/my-server/sock
```

### Obtaining the Auth Token

The server writes a 64-character hexadecimal token to its configuration
directory on startup. The path depends on your setup:

```bash
# Default location
cat "$XDG_RUNTIME_DIR/bash-server/token"

# Or read from the server's config directory
cat /tmp/bash-server-$(id -u)/token
```

You can also supply a fixed token with `--token` when starting the server.

### Installing the Client

```bash
# From the repository root
pip install -e bash-server/clients/python

# Or for development (includes pytest)
pip install -e "bash-server/clients/python[dev]"
```

Verify the installation:

```python
import bashclient
print(bashclient.__version__)  # "0.1.0"
```

---

## Core Concepts

### The v2 NDJSON Protocol

The bash-server v2 protocol uses **NDJSON** (Newline-Delimited JSON) as its
wire format. Every message is a single JSON object terminated by a newline
character (`\n`). This makes the protocol human-readable, easy to debug, and
trivial to parse.

Each message contains at minimum:
- `"ch"` -- the channel ID (integer 0-5)
- `"type"` -- the message type (string)

Additional fields depend on the channel and message type.

Example wire exchange:

```
Client -> {"ch":0,"type":"auth","token":"aabb...ff"}\n
Server -> {"ch":0,"type":"auth_ok","capabilities":["state","command","observe","debug"]}\n
Client -> {"ch":1,"type":"eval","command":"echo hello"}\n
Server -> {"ch":1,"type":"stdout","data":"aGVsbG8K","encoding":"base64"}\n
Server -> {"ch":1,"type":"stderr","data":"","encoding":"base64"}\n
Server -> {"ch":1,"type":"complete","exit_code":0}\n
```

The Python client handles all serialization and deserialization for you. You
interact with typed Python objects, not raw JSON.

### Channel Architecture

The v2 protocol multiplexes six channels over a single transport connection:

| Channel | ID | Purpose | Client Property |
|---------|----|---------|-----------------|
| CONTROL | 0 | Auth, ping, configure, disconnect | `client.control` |
| COMMAND | 1 | Evaluate commands, capture output | `client.command` |
| STATE   | 2 | Get/set variables, functions, aliases, traps | `client.state` |
| OBSERVE | 3 | Subscribe to pre/post command events | `client.observe` |
| DEBUG   | 4 | Breakpoints, stepping, AST inspection | `client.debug` |
| PTY     | 5 | Pseudo-terminal spawn, I/O, resize | `client.pty` |

Each channel has its own Python class with methods specific to its purpose.
Messages from the server are automatically routed to the correct channel.

**Request/response channels** (CONTROL, COMMAND, STATE): You send a request,
then `await` the response. The client manages per-channel queues internally.

**Event-driven channels** (OBSERVE, DEBUG, PTY): The server pushes events to
you. Register callbacks with `.on()` and unregister with `.off()`.

### Async/Await Model

The entire client is built on Python's `asyncio`. Every operation that
communicates with the server is a coroutine that must be awaited:

```python
result = await client.eval("echo hello")  # correct
result = client.eval("echo hello")        # WRONG: returns a coroutine, not a result
```

The client runs a background reader task that continuously reads messages from
the transport and dispatches them to the appropriate channel queue or callback.
This task starts automatically when you create a client and is cancelled when
you close it.

**Callbacks** registered with `.on()` can be either synchronous functions or
async coroutines. The client detects which kind you provided and handles both:

```python
# Sync callback -- works fine
client.observe.on("pre_command", lambda e: print(e.command))

# Async callback -- also works
async def on_event(event):
    await some_async_operation(event)
client.observe.on("pre_command", on_event)
```

---

## Getting Started

### Installation

See [Prerequisites](#installing-the-client) above. Once installed, you can
import everything from the top-level package:

```python
from bashclient import (
    BashClient,
    EvalResult,
    VarInfo,
    FuncInfo,
    AliasInfo,
    TrapInfo,
    PreCommandEvent,
    PostCommandEvent,
    Breakpoint,
    BreakHitEvent,
    DebugStatus,
    PtyInfo,
    AuthError,
    BashClientError,
    ProtocolError,
    ServerError,
    TimeoutError,
    TransportError,
    CHAN_CONTROL,
    CHAN_COMMAND,
    CHAN_STATE,
    CHAN_OBSERVE,
    CHAN_DEBUG,
    CHAN_PTY,
)
```

### First Connection

The simplest possible program -- connect, authenticate, run a command, print
the output, disconnect:

```python
import asyncio
from bashclient import BashClient

async def main():
    # Connect to the server's Unix socket
    client = await BashClient.connect("/tmp/bash-server-1000/sock")

    try:
        # Authenticate with the 64-char hex token
        await client.auth("a1b2c3d4e5f6...64 hex chars total...")

        # Run a command
        result = await client.eval("echo 'Hello from bash-server!'")
        print(result.stdout)   # "Hello from bash-server!\n"
        print(result.stderr)   # ""
        print(result.exit_code)  # 0
    finally:
        await client.close()

asyncio.run(main())
```

### Authentication

Authentication is mandatory. The server rejects all channel operations until a
valid token is provided. The token is a 64-character hexadecimal string (256
bits) generated from `/dev/urandom` by the server at startup.

```python
# Read the token from the server's token file
import pathlib

token_path = pathlib.Path("/tmp/bash-server-1000/token")
token = token_path.read_text().strip()

async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
    await client.auth(token)
    # Now you can use all channels
```

If the token is wrong, `AuthError` is raised:

```python
from bashclient import AuthError

try:
    await client.auth("0000000000000000000000000000000000000000000000000000000000000000")
except AuthError as e:
    print(f"Authentication failed: {e}")
```

After successful authentication, `client.is_authenticated` returns `True`.

### Using the Context Manager

The recommended pattern is to use `async with`, which ensures the connection is
properly closed even if an exception occurs:

```python
async with await BashClient.connect(socket_path) as client:
    await client.auth(token)
    result = await client.eval("whoami")
    print(result.stdout)
# Connection is automatically closed here
```

The context manager calls `client.close()` on exit, which:
1. Sends a `disconnect` message to the server
2. Cancels the background reader task
3. Closes the underlying transport

---

## Transports

The client supports four built-in transports, plus the ability to plug in a
custom one.

### Unix Socket (default)

The most common transport. Connect to a Unix domain socket that the server is
listening on:

```python
client = await BashClient.connect("/tmp/bash-server-1000/sock")
```

The socket path depends on how the server was started. The default resolution
order is:
1. `--socket` CLI argument
2. `$BASH_SERVER_SOCKET` environment variable
3. `~/.bash-serverrc` config file
4. `$XDG_RUNTIME_DIR/bash-server/sock`
5. `/tmp/bash-server-<uid>/sock`

**Typical usage with context manager:**

```python
import asyncio
from bashclient import BashClient

async def main():
    socket_path = "/tmp/bash-server-1000/sock"
    token = "a" * 64  # replace with actual token

    async with await BashClient.connect(socket_path) as client:
        await client.auth(token)
        result = await client.eval("hostname")
        print(result.stdout.strip())

asyncio.run(main())
```

### Subprocess (stdio)

Launch the server as a child process and communicate over its stdin/stdout.
This is useful for testing, short-lived sessions, or when you cannot use Unix
sockets:

```python
client = await BashClient.connect_stdio("bash-server", "--stdio")
```

All arguments are passed directly to `asyncio.create_subprocess_exec`. You can
specify the full path and any CLI options:

```python
client = await BashClient.connect_stdio(
    "/usr/local/bin/bash-server",
    "--stdio",
    "--login",
    "--init", "/home/user/.my_bashrc",
)
```

In stdio mode, the server typically generates and prints the token to stderr.
The client library captures stderr, but you need to read the token from the
server's configuration or pass a known `--token`.

**Typical pattern with a known token:**

```python
async def connect_stdio():
    token = "abcd" * 16  # 64 hex chars
    async with await BashClient.connect_stdio(
        "bash-server", "--stdio", "--token", token,
    ) as client:
        await client.auth(token)
        result = await client.eval("echo stdio works")
        print(result.stdout)
```

### File Descriptor

Connect over an inherited file descriptor. This is used when a parent process
sets up the connection (e.g., via `socketpair`) and passes the fd to the
Python process:

```python
# Assuming fd 3 is a connected socket inherited from the parent
client = await BashClient.connect_fd(3)
```

This transport is mainly useful for:
- Integration with process managers that set up fd-based IPC
- The `--fd N` server option
- The `--auth-fd N` option for passing the token out-of-band

**Example with socketpair (advanced):**

```python
import asyncio
import os
import socket
from bashclient import BashClient

async def main():
    # Create a Unix socketpair
    parent_fd, child_fd = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)

    # Fork bash-server with --fd pointing to child_fd
    proc = await asyncio.create_subprocess_exec(
        "bash-server", "--fd", str(child_fd.fileno()),
        pass_fds=(child_fd.fileno(),),
    )
    child_fd.close()  # Parent closes its copy of the child end

    # Connect the client to the parent end
    client = await BashClient.connect_fd(parent_fd.fileno())
    await client.auth(token)
    result = await client.eval("echo fd transport works")
    print(result.stdout)

    await client.close()
    proc.terminate()
    await proc.wait()
```

### Windows Named Pipe (Cygwin)

On Cygwin, the server can listen on a Windows Named Pipe instead of (or in
addition to) a Unix socket. This allows native Windows applications to connect:

```python
client = await BashClient.connect_named_pipe("/tmp/bash-server-pipe")
```

**Note:** Native Windows Python (`python.exe` from python.org) is not yet
supported for Named Pipe transport. Use Cygwin Python or connect from a
Cygwin environment.

The server creates Named Pipes with DACL security that restricts access to the
current user. Start the server with:

```bash
bash-server --named-pipe "bash-server-pipe"
```

### Custom Transport

You can implement the `Transport` abstract base class to create a transport
over any medium (TCP, WebSocket, serial port, etc.):

```python
import asyncio
from bashclient import BashClient
from bashclient.transport import Transport

class TcpTransport(Transport):
    """Example: TCP socket transport."""

    def __init__(self):
        self._reader = None
        self._writer = None
        self._open = False

    async def connect(self, host, port):
        self._reader, self._writer = await asyncio.open_connection(host, port)
        self._open = True

    async def read_line(self):
        if not self._reader:
            raise Exception("not connected")
        line = await self._reader.readline()
        if not line:
            self._open = False
            raise Exception("connection closed")
        return line

    async def write(self, data):
        if not self._writer:
            raise Exception("not connected")
        self._writer.write(data)
        await self._writer.drain()

    async def close(self):
        if self._writer:
            self._writer.close()
            await self._writer.wait_closed()
        self._open = False

    @property
    def is_open(self):
        return self._open


async def main():
    transport = TcpTransport()
    await transport.connect("127.0.0.1", 9999)
    client = await BashClient.from_transport(transport)
    await client.auth(token)
    result = await client.eval("echo custom transport")
    print(result.stdout)
    await client.close()
```

The `Transport` ABC requires four members:
- `async read_line() -> bytes` -- read one NDJSON line (including the trailing `\n`)
- `async write(data: bytes) -> None` -- write bytes to the transport
- `async close() -> None` -- close the transport
- `is_open: bool` (property) -- whether the transport is currently open

Use `BashClient.from_transport(transport)` to create a client from any
`Transport` instance.

---

## Command Execution (COMMAND Channel)

The COMMAND channel (channel 1) is the primary interface for running Bash
commands and capturing their output.

### Basic eval

The simplest way to run a command:

```python
result = await client.eval("echo hello world")
print(result.stdout)     # "hello world\n"
print(result.stderr)     # ""
print(result.exit_code)  # 0
```

The `eval` method is a convenience wrapper on `client.command.eval()`. Both are
equivalent:

```python
# These two lines do the same thing
result = await client.eval("ls -la /tmp")
result = await client.command.eval("ls -la /tmp")
```

The returned `EvalResult` is a dataclass with three fields:
- `stdout: str` -- standard output
- `stderr: str` -- standard error
- `exit_code: int` -- the command's exit status (0 = success)

**Multi-line commands:**

```python
result = await client.eval("""
for i in 1 2 3; do
    echo "item $i"
done
""")
print(result.stdout)
# item 1
# item 2
# item 3
```

**Pipelines:**

```python
result = await client.eval("cat /etc/passwd | grep root | head -1")
print(result.stdout)  # "root:x:0:0:root:/root:/bin/bash\n"
```

**Command substitution and variable expansion:**

```python
await client.eval("export MY_DIR=/tmp/test")
result = await client.eval("echo $MY_DIR")
print(result.stdout)  # "/tmp/test\n"
```

### Timeout handling

By default, `eval` waits up to 30 seconds for the command to complete. You can
override this:

```python
# Short timeout for quick commands
result = await client.eval("echo fast", timeout=5.0)

# Long timeout for slow operations
result = await client.eval("make -j12", timeout=300.0)
```

If the timeout expires before the server sends a `complete` message, a
`TimeoutError` is raised:

```python
from bashclient import TimeoutError

try:
    result = await client.eval("sleep 60", timeout=5.0)
except TimeoutError:
    print("Command did not finish in time")
```

**Important:** The timeout applies to waiting for the server's response. It
does not kill the running command on the server. If you need to cancel a
long-running command, use the CONTROL channel or close the connection.

### Capturing stdout/stderr

The server sends stdout and stderr as separate messages before the `complete`
message. The client collects all three automatically:

```python
result = await client.eval("echo out; echo err >&2")
print(f"stdout: {result.stdout!r}")  # "out\n"
print(f"stderr: {result.stderr!r}")  # "err\n"
```

**Checking for errors:**

```python
result = await client.eval("ls /nonexistent")
if result.exit_code != 0:
    print(f"Command failed (exit {result.exit_code}): {result.stderr}")
```

**Pattern: raise on non-zero exit:**

```python
async def run(client, command, timeout=30.0):
    """Run a command, raising RuntimeError on failure."""
    result = await client.eval(command, timeout=timeout)
    if result.exit_code != 0:
        raise RuntimeError(
            f"Command failed (exit {result.exit_code}): "
            f"{result.stderr.strip()}"
        )
    return result

result = await run(client, "gcc -o hello hello.c")
print("Compiled successfully")
```

### Binary output (base64)

The server encodes command output as base64 when the `encoding` field is
`"base64"`. The client automatically decodes this for you, so `result.stdout`
and `result.stderr` are always plain strings.

If you need raw bytes (e.g., for binary data), you can work with the COMMAND
channel directly. The lower-level `client.command.eval()` returns the same
`EvalResult`, but you could also read raw frames:

```python
# The client already handles base64 decoding.
# stdout and stderr are always strings.
result = await client.eval("cat /bin/true | head -c 16 | xxd")
print(result.stdout)
```

For truly binary pipelines, consider using the PTY channel which can relay
raw byte streams.

### Request IDs

You can tag requests with an `id` field for correlation in multiplexed
scenarios:

```python
result = await client.command.eval("echo tagged", id="req-001")
```

The server echoes the `id` back in all response messages (stdout, stderr,
complete). This is useful when you have multiple concurrent requests in flight
and need to match responses to requests.

---

## Shell State Management (STATE Channel)

The STATE channel (channel 2) provides direct access to the Bash interpreter's
state -- variables, functions, aliases, and traps -- without executing
commands. This is faster and safer than using `eval("echo $VAR")` because it
avoids shell parsing and expansion side effects.

### Variables

**Get a variable:**

```python
var = await client.state.get_var("PATH")
print(var.name)        # "PATH"
print(var.value)       # "/usr/bin:/bin:..."
print(var.attributes)  # ["exported"]
```

The returned `VarInfo` has:
- `name: str` -- the variable name
- `value: str` -- the current value
- `attributes: List[str]` -- Bash attributes (`"exported"`, `"readonly"`,
  `"integer"`, `"array"`, `"associative"`, etc.)

**Set a variable:**

```python
await client.state.set_var("MY_VAR", "hello")
```

**Set with attributes:**

```python
# Export the variable (equivalent to export MY_VAR=hello)
await client.state.set_var("MY_VAR", "hello", attributes=["exported"])

# Make it readonly (equivalent to readonly MY_VAR=hello)
await client.state.set_var("MY_VAR", "hello", attributes=["readonly"])

# Integer attribute (equivalent to declare -i MY_VAR=42)
await client.state.set_var("MY_VAR", "42", attributes=["integer"])
```

**Unset a variable:**

```python
await client.state.unset_var("MY_VAR")
```

**Pattern: read-modify-write:**

```python
async def append_to_path(client, directory):
    """Append a directory to PATH."""
    var = await client.state.get_var("PATH")
    new_path = var.value + ":" + directory
    await client.state.set_var("PATH", new_path, attributes=var.attributes)

await append_to_path(client, "/usr/local/bin")
```

**Pattern: check if a variable exists:**

```python
from bashclient import ServerError

async def var_exists(client, name):
    """Check whether a variable is defined."""
    try:
        await client.state.get_var(name)
        return True
    except ServerError:
        return False

if await var_exists(client, "DISPLAY"):
    print("Running under X11")
```

### Functions

**Get a function definition:**

```python
func = await client.state.get_func("my_function")
print(func.name)        # "my_function"
print(func.definition)  # "my_function () { echo hello; }"
```

The returned `FuncInfo` has:
- `name: str` -- the function name
- `definition: str` -- the full definition including body

**Define a function (via eval):**

The STATE channel does not have a `set_func` method because function
definitions require shell parsing. Use `eval` instead:

```python
await client.eval("""
greet() {
    local name="$1"
    echo "Hello, $name!"
}
""")

# Verify it was defined
func = await client.state.get_func("greet")
print(func.definition)
```

**Unset a function:**

```python
await client.state.unset_func("greet")
```

### Aliases

**Get an alias:**

```python
alias = await client.state.get_alias("ll")
print(alias.name)   # "ll"
print(alias.value)  # "ls -lah"
```

The returned `AliasInfo` has:
- `name: str` -- the alias name
- `value: str` -- the alias expansion text

**Set an alias:**

```python
await client.state.set_alias("ll", "ls -lah --color=auto")
await client.state.set_alias("gs", "git status")
await client.state.set_alias("..", "cd ..")
```

**Unset an alias:**

```python
await client.state.unset_alias("ll")
```

**Pattern: batch alias setup:**

```python
aliases = {
    "ll": "ls -lah",
    "la": "ls -A",
    "gs": "git status",
    "gd": "git diff",
    "gc": "git commit",
}

for name, value in aliases.items():
    await client.state.set_alias(name, value)
```

### Traps

**Set a trap:**

```python
# Trap SIGINT
await client.state.set_trap("SIGINT", "echo 'Caught SIGINT!'")

# Trap EXIT
await client.state.set_trap("EXIT", "cleanup_temp_files")

# Trap DEBUG (runs before every command)
await client.state.set_trap("DEBUG", "echo 'About to run: $BASH_COMMAND'")
```

**Unset a trap:**

```python
await client.state.unset_trap("SIGINT")
await client.state.unset_trap("EXIT")
```

**Note:** Traps apply to the server-side Bash interpreter. They fire when
commands are executed via `eval` or in the PTY session, not on the client side.

### Inspect (listing)

The `inspect` method queries the server for lists of defined entities:

```python
# List all variables
vars_list = await client.state.inspect("vars")
for item in vars_list:
    print(f"{item['name']}={item.get('value', '')}")

# List all functions
funcs = await client.state.inspect("functions")
for item in funcs:
    print(f"function {item['name']}")

# List all aliases
aliases = await client.state.inspect("aliases")
for item in aliases:
    print(f"alias {item['name']}={item.get('value', '')!r}")

# List all traps
traps = await client.state.inspect("traps")
for item in traps:
    print(f"trap {item.get('command', '')} {item.get('signal', '')}")
```

The `inspect` method returns a `List[Dict[str, Any]]`. Each dict contains
at minimum a `"name"` key, plus additional keys depending on the entity type.

**Pattern: find exported variables:**

```python
all_vars = await client.state.inspect("vars")
exported = [
    v for v in all_vars
    if "exported" in v.get("attributes", [])
]
for v in exported:
    print(f"export {v['name']}={v.get('value', '')!r}")
```

---

## Observability (OBSERVE Channel)

The OBSERVE channel (channel 3) lets you subscribe to events that fire before
and after every command the Bash interpreter executes. This is useful for
logging, auditing, timing, and building reactive automation.

### Subscribing to events

You must explicitly subscribe before events are delivered. The `level`
parameter controls verbosity:

```python
# Subscribe at level 1 (command-level events)
await client.observe.subscribe(level=1)
```

Currently defined levels:
- `0` -- observation off
- `1` -- pre/post command events

### Pre-command events

Fired just before a command begins execution. Register a callback with
`client.observe.on("pre_command", callback)`:

```python
from bashclient import PreCommandEvent

def on_pre_command(event):
    """Called before each command executes."""
    print(f"[{event.seq}] About to run: {event.command!r}")
    print(f"  cwd: {event.cwd}")
    print(f"  line: {event.line_number}")
    print(f"  subshell: {event.is_subshell}")
    print(f"  async: {event.is_async}")

client.observe.on("pre_command", on_pre_command)
await client.observe.subscribe(level=1)
```

`PreCommandEvent` fields:
- `seq: int` -- monotonically increasing sequence number
- `timestamp: int` -- Unix timestamp (server clock)
- `command: str` -- the command text about to execute
- `cwd: str` -- current working directory at time of execution
- `line_number: int` -- source line number (0 if not applicable)
- `is_subshell: bool` -- whether this runs in a subshell
- `is_async: bool` -- whether this is an asynchronous (background) command

### Post-command events

Fired after a command completes. Register with `"post_command"`:

```python
from bashclient import PostCommandEvent

def on_post_command(event):
    """Called after each command completes."""
    print(f"[{event.seq}] Finished: {event.command!r}")
    print(f"  exit_status: {event.exit_status}")
    print(f"  signal: {event.signal_number}")
    print(f"  duration: {event.duration_ms}ms")

client.observe.on("post_command", on_post_command)
```

`PostCommandEvent` fields:
- `seq: int` -- sequence number (matches the corresponding pre event)
- `timestamp: int` -- Unix timestamp when command completed
- `command: str` -- the command text that was executed
- `exit_status: int` -- exit code (0 = success)
- `signal_number: int` -- signal that terminated the command, if any (0 = none)
- `duration_ms: int` -- wall-clock execution time in milliseconds

### Unsubscribing

Stop receiving events:

```python
await client.observe.unsubscribe()
```

This does not remove your registered callbacks. If you subscribe again later,
the same callbacks will fire. To remove callbacks, use `.off()`.

### Async callbacks

Callbacks can be async coroutines. The client detects async functions
automatically:

```python
import aiofiles  # example async library

async def log_command(event):
    """Log each command to a file asynchronously."""
    async with aiofiles.open("/tmp/command.log", "a") as f:
        await f.write(f"{event.timestamp} {event.command}\n")

client.observe.on("pre_command", log_command)
```

### Removing callbacks

Remove a specific callback:

```python
def my_handler(event):
    print(event.command)

client.observe.on("pre_command", my_handler)

# Later: remove just this handler
client.observe.off("pre_command", my_handler)
```

Remove all callbacks for an event type:

```python
# Remove all pre_command callbacks
client.observe.off("pre_command")
```

### Full observation example

```python
import asyncio
from bashclient import BashClient, PreCommandEvent, PostCommandEvent

async def main():
    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth(token)

        commands_run = []

        def on_pre(event):
            print(f"[PRE]  seq={event.seq} cmd={event.command!r} cwd={event.cwd}")

        def on_post(event):
            print(f"[POST] seq={event.seq} cmd={event.command!r} "
                  f"exit={event.exit_status} dur={event.duration_ms}ms")
            commands_run.append(event.command)

        client.observe.on("pre_command", on_pre)
        client.observe.on("post_command", on_post)
        await client.observe.subscribe(level=1)

        # These commands generate observation events
        await client.eval("echo one")
        await client.eval("echo two")
        await client.eval("false")  # exit code 1

        await client.observe.unsubscribe()

        print(f"\nTotal commands observed: {len(commands_run)}")
```

---

## Debugging (DEBUG Channel)

The DEBUG channel (channel 4) provides a full-featured debugger for Bash
scripts. You can set breakpoints, step through execution, inspect the AST
(Abstract Syntax Tree), and control execution flow.

### Enabling the debugger

The debugger must be enabled before you can set breakpoints or step:

```python
await client.debug.enable()

# Check debugger status
status = await client.debug.status()
print(f"active: {status.active}")        # True
print(f"mode: {status.mode}")            # "run"
print(f"breakpoints: {status.breakpoints}")  # 0
print(f"depth: {status.depth}")          # 0
```

`DebugStatus` fields:
- `active: bool` -- whether the debugger is enabled
- `mode: str` -- current mode: `"run"`, `"step"`, `"next"`, or `"finish"`
- `breakpoints: int` -- number of active breakpoints
- `depth: int` -- current call depth

### Breakpoint types (command, line, function)

Three kinds of breakpoints are supported:

**Command breakpoints** -- break when a specific command is about to execute:

```python
# Break on any 'echo' command
bp_id = await client.debug.add_breakpoint(kind="command", pattern="echo")

# Break on 'rm' commands (safety catch!)
bp_id = await client.debug.add_breakpoint(kind="command", pattern="rm")

# Break with a glob pattern
bp_id = await client.debug.add_breakpoint(kind="command", pattern="git *")
```

**Line breakpoints** -- break at a specific source line number:

```python
# Break at line 10
bp_id = await client.debug.add_breakpoint(kind="line", line=10)

# Break at line 25 in a specific file
bp_id = await client.debug.add_breakpoint(kind="line", line=25, pattern="script.sh")
```

**Function breakpoints** -- break when a specific function is called:

```python
# Break when 'deploy' function is called
bp_id = await client.debug.add_breakpoint(kind="function", pattern="deploy")
```

**Conditional breakpoints** -- any breakpoint can have a condition:

```python
# Break on 'echo' only when $DEBUG is set
bp_id = await client.debug.add_breakpoint(
    kind="command",
    pattern="echo",
    condition='[ -n "$DEBUG" ]',
)

# Break at line 10 only when counter > 5
bp_id = await client.debug.add_breakpoint(
    kind="line",
    line=10,
    condition="(( counter > 5 ))",
)
```

The `add_breakpoint` method returns an integer breakpoint ID that you use for
subsequent operations on that breakpoint.

### Adding/removing breakpoints

```python
# Add breakpoints
bp1 = await client.debug.add_breakpoint(kind="command", pattern="echo")
bp2 = await client.debug.add_breakpoint(kind="function", pattern="deploy")
bp3 = await client.debug.add_breakpoint(kind="line", line=42)

# List all breakpoints
breakpoints = await client.debug.list_breakpoints()
for bp in breakpoints:
    print(f"  BP#{bp.id}: kind={bp.kind} enabled={bp.enabled} "
          f"hits={bp.hit_count} pattern={bp.pattern} line={bp.line}")

# Disable a breakpoint (keeps it but stops triggering)
await client.debug.disable_breakpoint(bp1)

# Re-enable it
await client.debug.enable_breakpoint(bp1)

# Remove a breakpoint entirely
removed = await client.debug.remove_breakpoint(bp2)
print(f"Removed: {removed}")  # True if it existed
```

`Breakpoint` dataclass fields:
- `id: int` -- unique breakpoint ID
- `kind: str` -- `"command"`, `"line"`, or `"function"`
- `enabled: bool` -- whether the breakpoint is active
- `hit_count: int` -- how many times it has been triggered
- `pattern: Optional[str]` -- command/function pattern (for command/function BPs)
- `line: Optional[int]` -- line number (for line BPs)
- `condition: Optional[str]` -- conditional expression, if any

### Execution control (step, next, finish, continue, skip)

When execution hits a breakpoint, you control what happens next:

```python
# Continue running until next breakpoint
await client.debug.continue_()

# Step into the next command (including into functions)
await client.debug.step()

# Step over (execute the next command, skip into function calls)
await client.debug.next()

# Finish the current function and break at the caller
await client.debug.finish()

# Skip the current command (do not execute it) and continue
await client.debug.skip()
```

**Note:** `continue_()` has a trailing underscore because `continue` is a
Python keyword.

### AST inspection

When stopped at a breakpoint, you can inspect the current command's AST:

```python
ast = await client.debug.inspect_ast()
print(ast)
# Example output:
# {
#     "type": "cm_simple",
#     "words": ["echo", "hello", "world"],
#     "redirects": [],
#     "flags": 0,
# }
```

The AST structure mirrors the internal Bash COMMAND tree. The `type` field
indicates the command type. See `doc/server/channels.md` for the complete
AST schema covering all 10 command types:

- `cm_simple` -- simple command
- `cm_connection` -- pipeline or `&&`/`||`
- `cm_for` -- for loop
- `cm_case` -- case statement
- `cm_while` / `cm_until` -- while/until loops
- `cm_if` -- if/elif/else
- `cm_group` -- `{ ... }`
- `cm_subshell` -- `( ... )`
- `cm_function_def` -- function definition
- `cm_coproc` -- coprocess

### Break hit callbacks

Register a callback to handle breakpoint hits:

```python
from bashclient import BreakHitEvent

async def on_break_hit(event):
    """Called when execution stops at a breakpoint."""
    print(f"Break at line {event.line}: {event.command!r} (depth={event.depth})")

    # Inspect the AST
    ast = await client.debug.inspect_ast()
    print(f"AST type: {ast.get('type')}")

    # Decide what to do
    if "rm" in event.command:
        print("Skipping dangerous command!")
        await client.debug.skip()
    else:
        await client.debug.continue_()

client.debug.on("break_hit", on_break_hit)
```

`BreakHitEvent` fields:
- `line: int` -- source line number where execution stopped
- `command: str` -- the command text at the breakpoint
- `depth: int` -- call stack depth

**Sync callbacks also work:**

```python
def on_break_sync(event):
    print(f"Hit: {event.command}")

client.debug.on("break_hit", on_break_sync)
```

### Removing debug callbacks

```python
# Remove a specific callback
client.debug.off("break_hit", on_break_hit)

# Remove all break_hit callbacks
client.debug.off("break_hit")
```

### Disabling the debugger

When done debugging, disable the debugger to restore normal execution speed:

```python
await client.debug.disable()
```

### Full debugging example

```python
import asyncio
from bashclient import BashClient, BreakHitEvent

async def main():
    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth(token)

        # Enable debugger
        await client.debug.enable()

        # Set breakpoints
        bp_echo = await client.debug.add_breakpoint(kind="command", pattern="echo")
        bp_line = await client.debug.add_breakpoint(kind="line", line=3)

        hit_count = 0

        async def on_break(event):
            nonlocal hit_count
            hit_count += 1
            print(f"[HIT #{hit_count}] line={event.line} cmd={event.command!r}")

            # Inspect AST at the breakpoint
            ast = await client.debug.inspect_ast()
            if ast.get("type") == "cm_simple":
                words = ast.get("words", [])
                print(f"  Simple command: {' '.join(words)}")

            # Step through the first two hits, then continue
            if hit_count <= 2:
                await client.debug.step()
            else:
                await client.debug.continue_()

        client.debug.on("break_hit", on_break)

        # Run a script that will hit breakpoints
        result = await client.eval("""
echo "line 1"
echo "line 2"
echo "line 3"
echo "done"
""")

        print(f"\nFinal output: {result.stdout}")
        print(f"Total break hits: {hit_count}")

        # Cleanup
        await client.debug.remove_breakpoint(bp_echo)
        await client.debug.remove_breakpoint(bp_line)
        await client.debug.disable()

asyncio.run(main())
```

---

## Terminal Emulation (PTY Channel)

The PTY channel (channel 5) spawns a pseudo-terminal on the server, giving you
an interactive shell session with full terminal emulation. This is suitable for
running interactive programs (`vim`, `top`, `ssh`), TUI applications, or any
command that expects a terminal.

### Spawning a PTY

```python
info = await client.pty.spawn(rows=24, cols=80)
print(f"PTY pid: {info.pid}")
print(f"Size: {info.rows}x{info.cols}")
print(f"ANSI stripping: {info.strip_ansi}")
```

`PtyInfo` fields:
- `rows: int` -- terminal row count
- `cols: int` -- terminal column count
- `pid: int` -- server-side PID of the shell process
- `strip_ansi: bool` -- whether ANSI escape sequences are being stripped

**With custom options:**

```python
# Custom size and shell
info = await client.pty.spawn(
    rows=48,
    cols=120,
    shell="/bin/zsh",
    strip_ansi=True,
)
```

Parameters:
- `rows` (default 24) -- terminal height
- `cols` (default 80) -- terminal width
- `shell` (optional) -- path to the shell binary; defaults to server's built-in bash
- `strip_ansi` (optional) -- if `True`, the server strips ANSI escape sequences from
  output, giving you clean plain text

### Input/Output

**Sending input:**

```python
# Send a command (note the \n for Enter)
await client.pty.write_input("ls -la\n")

# Send a tab for completion
await client.pty.write_input("\t")

# Send Ctrl+C
await client.pty.write_input("\x03")

# Send Ctrl+D (EOF)
await client.pty.write_input("\x04")
```

Input is base64-encoded before sending over the wire. The client handles this
encoding automatically.

**Receiving output:**

Output arrives asynchronously via callbacks. Register an `"output"` callback
to receive terminal data:

```python
def on_output(data):
    """Called when PTY produces output."""
    # data is a string (decoded from base64)
    print(data, end="", flush=True)

client.pty.on("output", on_output)
```

**Receiving exit events:**

When the shell process exits, an `"exit"` event is fired:

```python
exit_event = asyncio.Event()

def on_exit(code):
    """Called when the PTY shell exits."""
    print(f"Shell exited with code {code}")
    exit_event.set()

client.pty.on("exit", on_exit)

# Wait for exit
await asyncio.wait_for(exit_event.wait(), timeout=30.0)
```

**Async callbacks for output:**

```python
collected_output = []

async def on_output_async(data):
    collected_output.append(data)
    # Could write to a file, send to a WebSocket, etc.
    await some_async_sink.write(data)

client.pty.on("output", on_output_async)
```

### Resizing

If the terminal window size changes, notify the PTY:

```python
await client.pty.resize(rows=48, cols=120)
```

This sends a `SIGWINCH` to the server-side shell process, which updates the
terminal dimensions for programs that check `LINES` and `COLUMNS`.

### Signals

Send signals to the PTY's shell process:

```python
# Send SIGINT (Ctrl+C)
await client.pty.signal("SIGINT")

# Send SIGTSTP (Ctrl+Z)
await client.pty.signal("SIGTSTP")

# Send SIGTERM
await client.pty.signal("SIGTERM")

# Send SIGHUP
await client.pty.signal("SIGHUP")
```

Signal names follow the standard POSIX convention. You can use the full name
(`"SIGINT"`) or the short name without the `SIG` prefix, depending on server
configuration.

### ANSI stripping

When `strip_ansi=True` is passed to `spawn()`, the server runs a state machine
that removes ANSI escape sequences (colors, cursor movement, etc.) from the
output before sending it to the client. This is useful when you want clean
text output without terminal formatting:

```python
# Without stripping: output contains \x1b[32mHello\x1b[0m
# With stripping: output contains just "Hello"
info = await client.pty.spawn(strip_ansi=True)
```

Use cases for ANSI stripping:
- Logging PTY output to a file
- Parsing structured output from interactive programs
- Displaying in a non-terminal context (web UI, chat bot)

### Closing

Close the PTY session when done:

```python
await client.pty.close()
```

This sends a close message to the server, which terminates the PTY process. If
you have an `"exit"` callback registered, it will fire with the exit code.

**Pattern: graceful shutdown:**

```python
# Ask the shell to exit normally first
await client.pty.write_input("exit\n")

# Wait for exit with timeout
try:
    await asyncio.wait_for(exit_event.wait(), timeout=5.0)
except asyncio.TimeoutError:
    # Force close if it didn't exit cleanly
    await client.pty.close()
```

### Removing PTY callbacks

```python
# Remove specific callback
client.pty.off("output", on_output)

# Remove all output callbacks
client.pty.off("output")

# Remove all exit callbacks
client.pty.off("exit")
```

### Full PTY example

```python
import asyncio
import sys
from bashclient import BashClient

async def main():
    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth(token)

        output_lines = []
        exit_code = None
        exit_event = asyncio.Event()

        def on_output(data):
            sys.stdout.write(data)
            sys.stdout.flush()
            output_lines.append(data)

        def on_exit(code):
            nonlocal exit_code
            exit_code = code
            print(f"\n[PTY exited: {code}]")
            exit_event.set()

        client.pty.on("output", on_output)
        client.pty.on("exit", on_exit)

        # Spawn with ANSI stripping for clean output
        info = await client.pty.spawn(rows=24, cols=80, strip_ansi=True)
        print(f"Spawned PTY: pid={info.pid}")

        # Run some commands
        await client.pty.write_input("echo 'Hello from PTY!'\n")
        await asyncio.sleep(0.5)

        await client.pty.write_input("pwd\n")
        await asyncio.sleep(0.5)

        # Resize the terminal
        await client.pty.resize(48, 120)

        await client.pty.write_input("echo 'Resized to 48x120'\n")
        await asyncio.sleep(0.5)

        # Exit gracefully
        await client.pty.write_input("exit\n")

        try:
            await asyncio.wait_for(exit_event.wait(), timeout=5.0)
        except asyncio.TimeoutError:
            print("[Timeout -- forcing close]")
            await client.pty.close()

        print(f"Total output chunks: {len(output_lines)}")

asyncio.run(main())
```

---

## Error Handling

### Error hierarchy

All exceptions inherit from `BashClientError`:

```
BashClientError
  |-- AuthError           # Authentication failed (wrong token)
  |-- ProtocolError       # Malformed frame, invalid JSON, oversized payload
  |-- TimeoutError        # Operation timed out waiting for server response
  |-- TransportError      # Connection lost, socket error, pipe broken
  |-- ServerError         # Server returned an error response
```

Import them all from the top-level package:

```python
from bashclient import (
    BashClientError,
    AuthError,
    ProtocolError,
    TimeoutError,
    TransportError,
    ServerError,
)
```

**`BashClientError`** -- base class. Catch this to handle any client error:

```python
try:
    result = await client.eval("some command")
except BashClientError as e:
    print(f"Client error: {e}")
```

**`AuthError`** -- the token was rejected:

```python
try:
    await client.auth(token)
except AuthError:
    print("Wrong token. Check the server's token file.")
```

**`ProtocolError`** -- the wire data is malformed:

```python
# This is typically a bug in the client or server, not user error.
# Can also happen if the frame exceeds FRAME_MAX_PAYLOAD (1MB).
try:
    result = await client.eval(command)
except ProtocolError as e:
    print(f"Protocol error: {e}")
```

**`TimeoutError`** -- waiting for a server response timed out:

```python
try:
    result = await client.eval("sleep 999", timeout=5.0)
except TimeoutError:
    print("Server did not respond in time")
```

**`TransportError`** -- the underlying connection failed:

```python
try:
    result = await client.eval("echo test")
except TransportError as e:
    print(f"Connection error: {e}")
    # Likely need to reconnect
```

**`ServerError`** -- the server understood the request but returned an error:

```python
try:
    var = await client.state.get_var("NONEXISTENT_VAR")
except ServerError as e:
    print(f"Server error on channel {e.channel}: {e}")
```

`ServerError` has a `.channel` attribute (int) indicating which channel
generated the error.

### Retry patterns

**Simple retry with exponential backoff:**

```python
import asyncio
from bashclient import BashClient, TransportError, TimeoutError

async def connect_with_retry(socket_path, token, max_retries=5):
    """Connect with exponential backoff retry."""
    delay = 0.5
    for attempt in range(max_retries):
        try:
            client = await BashClient.connect(socket_path)
            await client.auth(token)
            return client
        except TransportError as e:
            if attempt == max_retries - 1:
                raise
            print(f"Connection failed (attempt {attempt + 1}): {e}")
            await asyncio.sleep(delay)
            delay *= 2
    raise TransportError("max retries exceeded")
```

**Retry on timeout:**

```python
async def eval_with_retry(client, command, retries=3, timeout=30.0):
    """Eval with retry on timeout."""
    for attempt in range(retries):
        try:
            return await client.eval(command, timeout=timeout)
        except TimeoutError:
            if attempt == retries - 1:
                raise
            print(f"Timeout (attempt {attempt + 1}), retrying...")
    raise TimeoutError("all retries exhausted")
```

**Reconnect on transport failure:**

```python
class ResilientClient:
    """Wrapper that reconnects on transport failures."""

    def __init__(self, socket_path, token):
        self._socket_path = socket_path
        self._token = token
        self._client = None

    async def _ensure_connected(self):
        if self._client is None or not self._client.is_connected:
            if self._client:
                try:
                    await self._client.close()
                except Exception:
                    pass
            self._client = await BashClient.connect(self._socket_path)
            await self._client.auth(self._token)

    async def eval(self, command, timeout=30.0):
        try:
            await self._ensure_connected()
            return await self._client.eval(command, timeout=timeout)
        except TransportError:
            # Connection lost -- reconnect and retry once
            self._client = None
            await self._ensure_connected()
            return await self._client.eval(command, timeout=timeout)

    async def close(self):
        if self._client:
            await self._client.close()
            self._client = None
```

### Graceful shutdown

Always close the client when done. The context manager handles this
automatically, but if you manage the lifecycle manually:

```python
client = await BashClient.connect(socket_path)
try:
    await client.auth(token)
    # ... do work ...
finally:
    await client.close()
```

The `close()` method:
1. Sends a `disconnect` message to the server (with a 2-second timeout)
2. Cancels the background reader task
3. Closes the underlying transport

If the server is unresponsive, `close()` will not hang -- the disconnect
attempt times out after 2 seconds, and cleanup proceeds regardless.

---

## Best Practices

### Connection lifecycle

**Do:**
- Use `async with` for automatic cleanup
- Keep one connection per logical session
- Close connections when no longer needed

**Do not:**
- Create a new connection for every command (expensive)
- Leave connections open indefinitely without keepalive
- Share a single client across multiple asyncio tasks without synchronization

```python
# GOOD: single connection, multiple commands
async with await BashClient.connect(socket_path) as client:
    await client.auth(token)
    for cmd in commands:
        result = await client.eval(cmd)
        process(result)

# BAD: new connection per command
for cmd in commands:
    async with await BashClient.connect(socket_path) as client:
        await client.auth(token)
        result = await client.eval(cmd)
```

### Context managers

Always prefer the context manager pattern. If you need to pass the client
around, create it in a top-level `async with` and pass the client object:

```python
async def do_setup(client):
    await client.eval("export ENV=production")
    await client.state.set_var("APP_ROOT", "/opt/myapp")

async def do_deploy(client):
    result = await client.eval("cd $APP_ROOT && ./deploy.sh")
    return result.exit_code == 0

async def main():
    async with await BashClient.connect(socket_path) as client:
        await client.auth(token)
        await do_setup(client)
        success = await do_deploy(client)
        print("Deploy:", "OK" if success else "FAILED")
```

### Timeout strategies

Choose timeouts based on what you are doing:

```python
# Quick lookups (variable get, ping)
var = await client.state.get_var("HOME")  # default 30s is fine

# Fast commands
result = await client.eval("echo hello", timeout=5.0)

# Medium commands (file operations, small builds)
result = await client.eval("find / -name '*.conf' 2>/dev/null", timeout=60.0)

# Long-running operations (builds, deployments)
result = await client.eval("make -j12", timeout=600.0)
```

**Pattern: operation-specific timeout wrapper:**

```python
async def quick_eval(client, command):
    """For commands that should be near-instant."""
    return await client.eval(command, timeout=5.0)

async def build_eval(client, command):
    """For build commands that may take minutes."""
    return await client.eval(command, timeout=600.0)
```

### Memory and resource management

**Observation callbacks:** Unsubscribe and remove callbacks when done observing
to prevent memory leaks from accumulating event data:

```python
# Register
client.observe.on("pre_command", handler)
await client.observe.subscribe(level=1)

# ... do work ...

# Cleanup: unsubscribe AND remove callbacks
await client.observe.unsubscribe()
client.observe.off("pre_command", handler)
```

**PTY sessions:** Always close PTY sessions when done. An unclosed PTY
continues consuming server resources:

```python
info = await client.pty.spawn()
try:
    # ... interact with PTY ...
    await client.pty.write_input("exit\n")
    await asyncio.sleep(1)
finally:
    await client.pty.close()
```

**Large output:** If a command produces very large output, be aware that the
entire stdout/stderr is buffered in memory as strings. For multi-megabyte
output, consider streaming via the PTY channel instead.

### Concurrent operations

The client's channels share a single transport connection. While you can
technically issue requests on multiple channels without waiting for responses,
the simplest and safest approach is to await each operation:

```python
# Safe: sequential operations
var1 = await client.state.get_var("HOME")
var2 = await client.state.get_var("PATH")
result = await client.eval("whoami")
```

If you need to run multiple commands concurrently, use separate connections:

```python
async def run_parallel(socket_path, token, commands):
    """Run commands in parallel using separate connections."""
    async def run_one(cmd):
        async with await BashClient.connect(socket_path) as client:
            await client.auth(token)
            return await client.eval(cmd)

    results = await asyncio.gather(*[run_one(cmd) for cmd in commands])
    return results
```

---

## Recipes

### Run a script file

Read a script file and execute it:

```python
import asyncio
from pathlib import Path
from bashclient import BashClient

async def run_script(client, script_path, timeout=60.0):
    """Execute a script file on the server."""
    script = Path(script_path).read_text()
    result = await client.eval(script, timeout=timeout)
    return result

async def main():
    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth(token)

        result = await run_script(client, "/path/to/deploy.sh", timeout=300.0)
        if result.exit_code != 0:
            print(f"Script failed:\n{result.stderr}")
        else:
            print(f"Script output:\n{result.stdout}")

asyncio.run(main())
```

**Alternative: source the script (preserves side effects):**

```python
async def source_script(client, remote_path):
    """Source a script that exists on the server filesystem."""
    result = await client.eval(f"source {remote_path}", timeout=60.0)
    return result
```

### Interactive REPL

Build a Python-based REPL that talks to bash-server:

```python
import asyncio
import sys
from bashclient import BashClient, BashClientError

async def repl(client):
    """Simple interactive REPL."""
    print("bash-server REPL (type 'quit' to exit)")
    print("---")

    while True:
        try:
            # Read input
            sys.stdout.write("$ ")
            sys.stdout.flush()

            # Use asyncio-compatible input
            loop = asyncio.get_event_loop()
            line = await loop.run_in_executor(None, sys.stdin.readline)
            line = line.strip()

            if not line:
                continue
            if line in ("quit", "exit"):
                break

            # Execute
            result = await client.eval(line, timeout=30.0)

            # Print output
            if result.stdout:
                sys.stdout.write(result.stdout)
            if result.stderr:
                sys.stderr.write(result.stderr)
            if result.exit_code != 0:
                print(f"[exit: {result.exit_code}]")

        except BashClientError as e:
            print(f"Error: {e}", file=sys.stderr)
        except (KeyboardInterrupt, EOFError):
            break

    print("\nGoodbye.")

async def main():
    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth(token)
        await repl(client)

asyncio.run(main())
```

### Automated testing harness

Use bash-server as a test runner for shell scripts:

```python
import asyncio
from dataclasses import dataclass
from typing import List
from bashclient import BashClient

@dataclass
class TestCase:
    name: str
    command: str
    expected_stdout: str = ""
    expected_exit_code: int = 0

@dataclass
class TestResult:
    name: str
    passed: bool
    message: str = ""

async def run_tests(client, tests):
    """Run a suite of test cases."""
    results = []

    for test in tests:
        result = await client.eval(test.command, timeout=30.0)

        passed = True
        message = ""

        if result.exit_code != test.expected_exit_code:
            passed = False
            message = (
                f"exit code: got {result.exit_code}, "
                f"expected {test.expected_exit_code}"
            )
        elif test.expected_stdout and result.stdout.strip() != test.expected_stdout.strip():
            passed = False
            message = (
                f"stdout mismatch:\n"
                f"  got:      {result.stdout.strip()!r}\n"
                f"  expected: {test.expected_stdout.strip()!r}"
            )

        results.append(TestResult(
            name=test.name,
            passed=passed,
            message=message,
        ))

    return results

async def main():
    tests = [
        TestCase("echo works", "echo hello", "hello"),
        TestCase("math works", "echo $((2 + 3))", "5"),
        TestCase("false fails", "false", expected_exit_code=1),
        TestCase("pipe works", "echo abc | rev", "cba"),
        TestCase("variable expansion", 'x=42; echo $x', "42"),
    ]

    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth(token)
        results = await run_tests(client, tests)

        passed = sum(1 for r in results if r.passed)
        failed = sum(1 for r in results if not r.passed)

        for r in results:
            status = "PASS" if r.passed else "FAIL"
            print(f"  [{status}] {r.name}")
            if r.message:
                print(f"         {r.message}")

        print(f"\n{passed} passed, {failed} failed, {len(results)} total")

asyncio.run(main())
```

### Log watcher

Watch command execution in real time and log to a file:

```python
import asyncio
import json
import time
from bashclient import BashClient, PreCommandEvent, PostCommandEvent

async def log_watcher(client, log_file="/tmp/bash-commands.jsonl"):
    """Watch and log all commands executed on the server."""

    f = open(log_file, "a")

    def on_pre(event):
        entry = {
            "type": "pre_command",
            "seq": event.seq,
            "timestamp": event.timestamp,
            "command": event.command,
            "cwd": event.cwd,
            "line_number": event.line_number,
            "is_subshell": event.is_subshell,
            "client_time": time.time(),
        }
        f.write(json.dumps(entry) + "\n")
        f.flush()

    def on_post(event):
        entry = {
            "type": "post_command",
            "seq": event.seq,
            "timestamp": event.timestamp,
            "command": event.command,
            "exit_status": event.exit_status,
            "duration_ms": event.duration_ms,
            "signal_number": event.signal_number,
            "client_time": time.time(),
        }
        f.write(json.dumps(entry) + "\n")
        f.flush()

    client.observe.on("pre_command", on_pre)
    client.observe.on("post_command", on_post)
    await client.observe.subscribe(level=1)

    print(f"Logging commands to {log_file}")
    print("Press Ctrl+C to stop")

    try:
        # Run commands while logging
        await client.eval("echo 'watcher started'")
        await client.eval("ls /tmp")
        await client.eval("date")

        # Keep the subscription active for external commands too
        # (e.g., from other clients connected to the same server)
        await asyncio.sleep(60)
    finally:
        await client.observe.unsubscribe()
        client.observe.off("pre_command", on_pre)
        client.observe.off("post_command", on_post)
        f.close()

async def main():
    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth(token)
        await log_watcher(client)

asyncio.run(main())
```

### Remote debugger

Build a command-line debugger that connects to bash-server and lets you
interactively debug Bash scripts:

```python
import asyncio
import sys
from bashclient import BashClient, BreakHitEvent

class RemoteDebugger:
    """Interactive debugger for bash-server."""

    def __init__(self, client):
        self._client = client
        self._paused = asyncio.Event()
        self._current_event = None
        self._quit = False

    async def start(self):
        """Enable the debugger and register handlers."""
        await self._client.debug.enable()

        async def on_break(event):
            self._current_event = event
            self._paused.set()

        self._client.debug.on("break_hit", on_break)
        print("Debugger enabled. Set breakpoints with 'break' command.")

    async def prompt(self):
        """Interactive debugger prompt loop."""
        loop = asyncio.get_event_loop()

        while not self._quit:
            sys.stdout.write("(bashdb) ")
            sys.stdout.flush()
            line = await loop.run_in_executor(None, sys.stdin.readline)
            line = line.strip()

            if not line:
                continue

            parts = line.split(None, 2)
            cmd = parts[0]

            if cmd in ("q", "quit"):
                self._quit = True
            elif cmd in ("b", "break"):
                await self._cmd_break(parts[1:])
            elif cmd in ("bl", "breakpoints"):
                await self._cmd_list_breakpoints()
            elif cmd in ("del", "delete"):
                await self._cmd_delete(parts[1:])
            elif cmd in ("r", "run"):
                await self._cmd_run(parts[1:])
            elif cmd in ("c", "continue"):
                await self._client.debug.continue_()
            elif cmd in ("s", "step"):
                await self._client.debug.step()
            elif cmd in ("n", "next"):
                await self._client.debug.next()
            elif cmd in ("f", "finish"):
                await self._client.debug.finish()
            elif cmd == "skip":
                await self._client.debug.skip()
            elif cmd == "ast":
                await self._cmd_ast()
            elif cmd == "status":
                await self._cmd_status()
            elif cmd in ("h", "help"):
                self._cmd_help()
            else:
                print(f"Unknown command: {cmd!r}. Type 'help' for help.")

        await self._client.debug.disable()
        print("Debugger disabled.")

    async def _cmd_break(self, args):
        if not args:
            print("Usage: break <kind> [pattern|line] [condition]")
            print("  break command echo")
            print("  break line 10")
            print("  break function deploy")
            return

        kind = args[0]
        kwargs = {"kind": kind}

        if kind == "line" and len(args) > 1:
            kwargs["line"] = int(args[1])
        elif len(args) > 1:
            kwargs["pattern"] = args[1]

        bp_id = await self._client.debug.add_breakpoint(**kwargs)
        print(f"Breakpoint {bp_id} set ({kind})")

    async def _cmd_list_breakpoints(self):
        bps = await self._client.debug.list_breakpoints()
        if not bps:
            print("No breakpoints set.")
            return
        for bp in bps:
            state = "enabled" if bp.enabled else "disabled"
            target = bp.pattern or f"line {bp.line}"
            cond = f" if {bp.condition}" if bp.condition else ""
            print(f"  #{bp.id} [{state}] {bp.kind} {target}{cond} "
                  f"(hits: {bp.hit_count})")

    async def _cmd_delete(self, args):
        if not args:
            print("Usage: delete <breakpoint-id>")
            return
        bp_id = int(args[0])
        found = await self._client.debug.remove_breakpoint(bp_id)
        if found:
            print(f"Breakpoint {bp_id} deleted")
        else:
            print(f"Breakpoint {bp_id} not found")

    async def _cmd_run(self, args):
        if not args:
            print("Usage: run <command>")
            return
        command = " ".join(args)
        print(f"Running: {command}")

        # Start eval in a background task so we can handle break events
        async def do_eval():
            result = await self._client.eval(command, timeout=300.0)
            print(f"\n[Completed: exit={result.exit_code}]")
            if result.stdout:
                print(result.stdout, end="")

        eval_task = asyncio.ensure_future(do_eval())

        # Wait for break events
        while not eval_task.done():
            self._paused.clear()
            try:
                await asyncio.wait_for(self._paused.wait(), timeout=0.5)
            except asyncio.TimeoutError:
                continue

            if self._current_event:
                e = self._current_event
                print(f"\n[Break] line {e.line}: {e.command!r} (depth={e.depth})")
                # Return to prompt for debugging commands
                return

        await eval_task

    async def _cmd_ast(self):
        ast = await self._client.debug.inspect_ast()
        import json
        print(json.dumps(ast, indent=2))

    async def _cmd_status(self):
        status = await self._client.debug.status()
        print(f"  active: {status.active}")
        print(f"  mode: {status.mode}")
        print(f"  breakpoints: {status.breakpoints}")
        print(f"  depth: {status.depth}")

    def _cmd_help(self):
        print("Commands:")
        print("  break <kind> [pattern|line]  Set a breakpoint")
        print("  breakpoints                  List breakpoints")
        print("  delete <id>                  Delete a breakpoint")
        print("  run <command>                Execute a command")
        print("  continue (c)                 Continue execution")
        print("  step (s)                     Step into")
        print("  next (n)                     Step over")
        print("  finish (f)                   Finish function")
        print("  skip                         Skip current command")
        print("  ast                          Inspect current AST")
        print("  status                       Show debugger status")
        print("  quit (q)                     Quit debugger")


async def main():
    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth(token)

        debugger = RemoteDebugger(client)
        await debugger.start()
        await debugger.prompt()

asyncio.run(main())
```

### Environment setup helper

Programmatically configure the shell environment:

```python
import asyncio
from bashclient import BashClient

async def setup_environment(client, config):
    """Set up shell environment from a configuration dict."""

    # Set variables
    for name, value in config.get("variables", {}).items():
        attrs = []
        if config.get("export_all", False):
            attrs.append("exported")
        await client.state.set_var(name, value, attributes=attrs)

    # Set aliases
    for name, value in config.get("aliases", {}).items():
        await client.state.set_alias(name, value)

    # Define functions
    for func_def in config.get("functions", []):
        await client.eval(func_def)

    # Source files
    for source_file in config.get("source_files", []):
        result = await client.eval(f"source {source_file}")
        if result.exit_code != 0:
            print(f"Warning: failed to source {source_file}: {result.stderr}")

async def main():
    config = {
        "variables": {
            "APP_ENV": "production",
            "APP_ROOT": "/opt/myapp",
            "LOG_LEVEL": "info",
        },
        "aliases": {
            "ll": "ls -lah",
            "deploy": "cd $APP_ROOT && ./deploy.sh",
        },
        "functions": [
            "log() { echo \"[$(date)] $*\" >> /tmp/app.log; }",
            "die() { echo \"FATAL: $*\" >&2; return 1; }",
        ],
        "source_files": [
            "/etc/profile",
            "$APP_ROOT/.env",
        ],
        "export_all": True,
    }

    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth(token)
        await setup_environment(client, config)
        print("Environment configured.")

        # Verify
        var = await client.state.get_var("APP_ENV")
        print(f"APP_ENV = {var.value}")

asyncio.run(main())
```

### Health check / keepalive

Keep the connection alive and verify the server is responsive:

```python
import asyncio
from bashclient import BashClient, BashClientError

async def health_check(client, interval=30.0):
    """Periodic health check via ping."""
    consecutive_failures = 0

    while True:
        try:
            await client.ping()
            consecutive_failures = 0
        except BashClientError as e:
            consecutive_failures += 1
            print(f"Health check failed ({consecutive_failures}): {e}")
            if consecutive_failures >= 3:
                print("Server appears down!")
                return False

        await asyncio.sleep(interval)

async def main():
    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth(token)

        # Run health checks in background
        health_task = asyncio.ensure_future(health_check(client, interval=10.0))

        # Do other work...
        for i in range(10):
            result = await client.eval(f"echo iteration {i}")
            print(result.stdout.strip())
            await asyncio.sleep(5)

        health_task.cancel()
        try:
            await health_task
        except asyncio.CancelledError:
            pass

asyncio.run(main())
```

### Multiplexed state snapshot

Capture the full shell state at a point in time:

```python
import asyncio
import json
from bashclient import BashClient

async def capture_state(client):
    """Capture a snapshot of the shell's complete state."""
    snapshot = {}

    # Capture variables
    vars_list = await client.state.inspect("vars")
    snapshot["variables"] = {
        item["name"]: {
            "value": item.get("value", ""),
            "attributes": item.get("attributes", []),
        }
        for item in vars_list
    }

    # Capture functions
    funcs_list = await client.state.inspect("functions")
    snapshot["functions"] = {
        item["name"]: item.get("value", "")
        for item in funcs_list
    }

    # Capture aliases
    aliases_list = await client.state.inspect("aliases")
    snapshot["aliases"] = {
        item["name"]: item.get("value", "")
        for item in aliases_list
    }

    # Capture traps
    traps_list = await client.state.inspect("traps")
    snapshot["traps"] = {
        item.get("signal", ""): item.get("command", "")
        for item in traps_list
    }

    # Capture some runtime info via eval
    pwd_result = await client.eval("pwd")
    snapshot["cwd"] = pwd_result.stdout.strip()

    umask_result = await client.eval("umask")
    snapshot["umask"] = umask_result.stdout.strip()

    return snapshot

async def restore_state(client, snapshot):
    """Restore shell state from a snapshot (partial -- variables and aliases)."""
    for name, info in snapshot.get("variables", {}).items():
        await client.state.set_var(name, info["value"], attributes=info["attributes"])

    for name, value in snapshot.get("aliases", {}).items():
        await client.state.set_alias(name, value)

    if "cwd" in snapshot:
        await client.eval(f"cd {snapshot['cwd']}")

async def main():
    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth(token)

        # Set up some state
        await client.eval("export FOO=bar")
        await client.state.set_alias("ll", "ls -la")

        # Take a snapshot
        state = await capture_state(client)
        print(json.dumps(state, indent=2, default=str))

        # Save to file
        with open("/tmp/shell-state.json", "w") as f:
            json.dump(state, f, indent=2)

asyncio.run(main())
```

---

## Appendix: Constants and Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `CHAN_CONTROL` | 0 | Control channel ID |
| `CHAN_COMMAND` | 1 | Command channel ID |
| `CHAN_STATE` | 2 | State channel ID |
| `CHAN_OBSERVE` | 3 | Observe channel ID |
| `CHAN_DEBUG` | 4 | Debug channel ID |
| `CHAN_PTY` | 5 | PTY channel ID |
| `FRAME_MAX_PAYLOAD` | 1,048,576 | Maximum NDJSON frame size (1 MB) |
| `TOKEN_HEXLEN` | 64 | Auth token length in hex characters (256 bits) |
| `OBSERVE_LEVEL_OFF` | 0 | Observation disabled |
| `OBSERVE_LEVEL_COMMAND` | 1 | Command-level observation |

---

## Appendix: Data Types Quick Reference

| Type | Fields | Used By |
|------|--------|---------|
| `EvalResult` | `stdout`, `stderr`, `exit_code` | `client.eval()`, `command.eval()` |
| `VarInfo` | `name`, `value`, `attributes` | `state.get_var()` |
| `FuncInfo` | `name`, `definition` | `state.get_func()` |
| `AliasInfo` | `name`, `value` | `state.get_alias()` |
| `TrapInfo` | `signal`, `command` | (future use) |
| `PreCommandEvent` | `seq`, `timestamp`, `command`, `cwd`, `line_number`, `is_subshell`, `is_async` | observe `"pre_command"` |
| `PostCommandEvent` | `seq`, `timestamp`, `command`, `exit_status`, `signal_number`, `duration_ms` | observe `"post_command"` |
| `Breakpoint` | `id`, `kind`, `enabled`, `hit_count`, `pattern`, `line`, `condition` | `debug.list_breakpoints()` |
| `BreakHitEvent` | `line`, `command`, `depth` | debug `"break_hit"` |
| `DebugStatus` | `active`, `mode`, `breakpoints`, `depth` | `debug.status()` |
| `PtyInfo` | `rows`, `cols`, `pid`, `strip_ansi` | `pty.spawn()` |

---

## Appendix: Channel Method Quick Reference

### ControlChannel (`client.control`)

| Method | Description |
|--------|-------------|
| `auth(token)` | Authenticate with 64-char hex token |
| `ping()` | Ping the server (raises on failure) |
| `configure(**kwargs)` | Send configuration to server |
| `disconnect()` | Gracefully disconnect |

### CommandChannel (`client.command`)

| Method | Description |
|--------|-------------|
| `eval(command, id=None, timeout=30.0)` | Evaluate command, return `EvalResult` |

### StateChannel (`client.state`)

| Method | Description |
|--------|-------------|
| `get_var(name)` | Get variable info (`VarInfo`) |
| `set_var(name, value, attributes=None)` | Set variable |
| `unset_var(name)` | Unset variable |
| `get_func(name)` | Get function definition (`FuncInfo`) |
| `unset_func(name)` | Unset function |
| `get_alias(name)` | Get alias value (`AliasInfo`) |
| `set_alias(name, value)` | Set alias |
| `unset_alias(name)` | Unset alias |
| `set_trap(signal, command)` | Set trap handler |
| `unset_trap(signal)` | Remove trap handler |
| `inspect(query)` | List entities (`"vars"`, `"functions"`, `"aliases"`, `"traps"`) |

### ObserveChannel (`client.observe`)

| Method | Description |
|--------|-------------|
| `subscribe(level=1)` | Start receiving events |
| `unsubscribe()` | Stop receiving events |
| `on(event, callback)` | Register callback (`"pre_command"`, `"post_command"`) |
| `off(event, callback=None)` | Remove callback(s) |

### DebugChannel (`client.debug`)

| Method | Description |
|--------|-------------|
| `enable()` | Enable debugger |
| `disable()` | Disable debugger |
| `status()` | Get debugger status (`DebugStatus`) |
| `add_breakpoint(kind, pattern=None, line=None, condition=None)` | Add breakpoint, return ID |
| `remove_breakpoint(bp_id)` | Remove breakpoint |
| `enable_breakpoint(bp_id)` | Enable a disabled breakpoint |
| `disable_breakpoint(bp_id)` | Disable a breakpoint |
| `list_breakpoints()` | List all breakpoints (`List[Breakpoint]`) |
| `continue_()` | Continue execution |
| `step()` | Step into |
| `next()` | Step over |
| `finish()` | Finish current function |
| `skip()` | Skip current command |
| `inspect_ast()` | Inspect current command AST |
| `on(event, callback)` | Register callback (`"break_hit"`) |
| `off(event, callback=None)` | Remove callback(s) |

### PtyChannel (`client.pty`)

| Method | Description |
|--------|-------------|
| `spawn(rows=24, cols=80, shell=None, strip_ansi=None)` | Spawn PTY (`PtyInfo`) |
| `write_input(data)` | Send input to PTY |
| `resize(rows, cols)` | Resize terminal |
| `signal(name)` | Send signal to PTY process |
| `close()` | Close PTY session |
| `on(event, callback)` | Register callback (`"output"`, `"exit"`) |
| `off(event, callback=None)` | Remove callback(s) |
