# Architecture

## bashclient - Internal Design and Module Structure

This document describes the internal architecture of the bashclient Python
package, including module responsibilities, data flow, message routing,
and design decisions.

---

## Table of Contents

- [Overview](#overview)
- [Module Structure](#module-structure)
- [Module Dependency Graph](#module-dependency-graph)
- [Data Flow](#data-flow)
  - [Outbound (Client to Server)](#outbound-client-to-server)
  - [Inbound (Server to Client)](#inbound-server-to-client)
- [BashClient Internals](#bashclient-internals)
  - [Connection Lifecycle](#connection-lifecycle)
  - [Reader Loop](#reader-loop)
  - [Channel Queues](#channel-queues)
  - [Server-Push Dispatch](#server-push-dispatch)
  - [Timeout Handling](#timeout-handling)
- [Transport Layer](#transport-layer)
  - [Transport ABC](#transport-abc)
  - [UnixSocketTransport](#unixsockettransport)
  - [StdioTransport](#stdiotransport)
  - [FdTransport](#fdtransport)
  - [NamedPipeTransport](#namedpipetransport)
- [Protocol Layer](#protocol-layer)
  - [NDJSON Framing](#ndjson-framing)
  - [Frame Format](#frame-format)
  - [Encoding and Decoding](#encoding-and-decoding)
  - [Base64 Helpers](#base64-helpers)
- [Channel Layer](#channel-layer)
  - [Channel IDs](#channel-ids)
  - [Request-Response Pattern](#request-response-pattern)
  - [Server-Push Pattern](#server-push-pattern)
  - [Channel Implementations](#channel-implementations)
- [Message Routing](#message-routing)
  - [Reader Loop Detail](#reader-loop-detail)
  - [Dispatch Table](#dispatch-table)
  - [Queue Selection](#queue-selection)
- [Type System](#type-system)
  - [Dataclass Types](#dataclass-types)
  - [Type Annotations](#type-annotations)
- [Error Hierarchy](#error-hierarchy)
- [Testing Architecture](#testing-architecture)
  - [Unit Tests](#unit-tests)
  - [Channel Tests](#channel-tests)
  - [Integration Tests](#integration-tests)
  - [Test Utilities](#test-utilities)
- [Design Decisions](#design-decisions)
  - [Why asyncio](#why-asyncio)
  - [Why No External Dependencies](#why-no-external-dependencies)
  - [Why NDJSON Over Binary](#why-ndjson-over-binary)
  - [Why Queues Per Channel](#why-queues-per-channel)
  - [Why Callbacks for Server-Push](#why-callbacks-for-server-push)
  - [Why Dataclasses Over Dicts](#why-dataclasses-over-dicts)

---

## Overview

bashclient is a pure-Python async client for the bash-server v2 NDJSON
protocol. It provides a high-level API for evaluating shell commands,
inspecting state, observing execution, debugging, and managing PTY
sessions through a multiplexed channel architecture.

The package is organized into six modules plus an `__init__.py` that
re-exports the public API. Each module has a single responsibility:

```
bashclient/
    __init__.py       Public API, re-exports, __version__, __all__
    client.py         BashClient class - main entry point
    channels.py       Channel method implementations (6 channels)
    protocol.py       NDJSON wire protocol encode/decode
    transport.py      I/O abstraction (4 transport backends)
    types.py          Dataclass types for results and events
    errors.py         Exception hierarchy
```

---

## Module Structure

### __init__.py

The package entry point. Defines `__version__` and `__all__`. Re-exports
the primary classes and types so users can write:

```python
from bashclient import BashClient
from bashclient import EvalResult, ObserveEvent
from bashclient import BashClientError
```

Rather than importing from submodules directly. All public names are
listed in `__all__` to support `from bashclient import *` (though
explicit imports are preferred).

**Exports**:
- `BashClient` (from `client`)
- All dataclass types (from `types`)
- All exception classes (from `errors`)
- `__version__`: str

### client.py

Contains the `BashClient` class, which is the sole entry point for
users. BashClient composes:

- A transport instance (from `transport.py`)
- Protocol encode/decode functions (from `protocol.py`)
- Channel method implementations (mixed in from `channels.py`)
- Channel queues and the reader loop

BashClient manages the connection lifecycle (connect, authenticate,
operate, close) and owns the background reader loop task.

**Key attributes**:

| Attribute          | Type                              | Purpose                          |
|--------------------|-----------------------------------|----------------------------------|
| `_transport`       | `Transport`                       | Active I/O backend               |
| `_reader_task`     | `asyncio.Task`                    | Background frame reader          |
| `_channel_queues`  | `dict[int, asyncio.Queue]`        | Per-channel response queues      |
| `_push_callbacks`  | `dict[int, list[Callable]]`       | Server-push event callbacks      |
| `_connected`       | `bool`                            | Connection state flag            |
| `_authenticated`   | `bool`                            | Authentication state flag        |
| `_default_timeout` | `float`                           | Default timeout for operations   |

### channels.py

Implements the channel-specific methods that BashClient exposes. Each
v2 protocol channel maps to a set of methods:

| Channel      | ID | Methods                                                  |
|--------------|----|----------------------------------------------------------|
| CONTROL      | 0  | `auth()`, `ping()`, `close()`, `configure()`            |
| COMMAND      | 1  | `eval()`, `eval_parsed()`                                |
| STATE        | 2  | `get_var()`, `set_var()`, `unset_var()`, `get_func()`,  |
|              |    | `set_func()`, `unset_func()`, `get_alias()`,            |
|              |    | `set_alias()`, `unset_alias()`, `get_trap()`,           |
|              |    | `set_trap()`, `inspect()`                                |
| OBSERVE      | 3  | `observe_start()`, `observe_stop()`,                     |
|              |    | `on_observe(callback)`                                   |
| DEBUG        | 4  | `breakpoint_set()`, `breakpoint_clear()`,                |
|              |    | `debug_step()`, `debug_next()`, `debug_finish()`,       |
|              |    | `debug_skip()`, `debug_continue()`,                     |
|              |    | `inspect_ast()`, `on_debug(callback)`                   |
| PTY          | 5  | `pty_spawn()`, `pty_write()`, `pty_resize()`,           |
|              |    | `pty_signal()`, `pty_close()`, `on_pty(callback)`       |

These methods are defined as functions that take a `BashClient` instance
as their first argument and are bound to the class in `client.py`. This
separation keeps `client.py` focused on connection management while
`channels.py` holds the protocol-level details.

### protocol.py

Handles NDJSON wire protocol encoding and decoding. This module is
stateless -- all functions are pure.

**Functions**:

| Function        | Signature                              | Purpose                        |
|-----------------|----------------------------------------|--------------------------------|
| `encode_frame`  | `(channel: int, data: dict) -> bytes`  | Encode a JSON frame + newline  |
| `decode_frame`  | `(line: bytes) -> tuple[int, dict]`    | Decode a JSON line to channel+data |
| `encode_base64` | `(data: bytes) -> str`                 | Base64 encode for binary data  |
| `decode_base64` | `(s: str) -> bytes`                    | Base64 decode                  |

The protocol is NDJSON (Newline-Delimited JSON): each frame is a single
JSON object on one line, terminated by `\n`. The channel ID is carried
inside the JSON as the `"ch"` field.

### transport.py

Defines the `Transport` abstract base class and four concrete
implementations. Each transport provides async read/write of raw bytes
over a connection to bash-server.

**Classes**:

| Class                  | Transport Mode   | Connection Method               |
|------------------------|------------------|---------------------------------|
| `UnixSocketTransport`  | Unix socket      | `connect(path)`                 |
| `StdioTransport`       | Subprocess stdio | `connect_stdio(cmd)`            |
| `FdTransport`          | File descriptors | `connect_fd(read_fd, write_fd)` |
| `NamedPipeTransport`   | Windows pipe     | `connect_named_pipe(name)`      |

All transports implement the same ABC interface:

```python
class Transport(ABC):
    async def connect(self, **kwargs) -> None: ...
    async def read_line(self) -> bytes: ...
    async def write(self, data: bytes) -> None: ...
    async def close(self) -> None: ...
    @property
    def is_connected(self) -> bool: ...
```

### types.py

Contains dataclass definitions for all structured data returned by
channel methods. These provide typed access to results instead of
raw dictionaries.

**Dataclasses** (12 total):

| Type              | Used By           | Fields                                      |
|-------------------|-------------------|----------------------------------------------|
| `EvalResult`      | COMMAND channel   | `stdout`, `stderr`, `exit_code`, `duration`  |
| `PingResult`      | CONTROL channel   | `latency`, `server_version`                  |
| `AuthResult`      | CONTROL channel   | `success`, `message`                         |
| `VarInfo`         | STATE channel     | `name`, `value`, `type`, `attributes`        |
| `FuncInfo`        | STATE channel     | `name`, `body`                               |
| `AliasInfo`       | STATE channel     | `name`, `value`                              |
| `TrapInfo`        | STATE channel     | `signal`, `action`                           |
| `InspectResult`   | STATE channel     | `namespace`, `items`                         |
| `ObserveEvent`    | OBSERVE channel   | `type`, `command`, `cwd`, `timestamp`, ...   |
| `DebugEvent`      | DEBUG channel     | `type`, `location`, `command_ast`, ...       |
| `BreakpointInfo`  | DEBUG channel     | `id`, `type`, `target`, `enabled`            |
| `PtyEvent`        | PTY channel       | `type`, `data`, `exit_code`                  |

### errors.py

Defines the exception hierarchy. All exceptions inherit from
`BashClientError`.

```
BashClientError
    ConnectionError         Connection/transport failures
    AuthenticationError     Auth failures (wrong token, not authenticated)
    ProtocolError           Malformed frames, unexpected responses
    TimeoutError            Operation timeouts
    ChannelError            Channel-specific errors (e.g., eval failure)
```

---

## Module Dependency Graph

```
__init__.py
    imports from: client, types, errors

client.py
    imports from: channels, protocol, transport, types, errors

channels.py
    imports from: protocol, types, errors

protocol.py
    imports from: (stdlib only: json, base64)

transport.py
    imports from: errors
    imports from: (stdlib: asyncio, socket, subprocess, os)

types.py
    imports from: (stdlib only: dataclasses, typing)

errors.py
    imports from: (nothing)
```

The dependency graph is strictly layered with no circular imports:

```
Layer 0 (no internal deps):  errors.py, types.py, protocol.py
Layer 1 (depends on L0):     transport.py, channels.py
Layer 2 (depends on L0+L1):  client.py
Layer 3 (re-exports):        __init__.py
```

---

## Data Flow

### Outbound (Client to Server)

```
User code
    |
    v
BashClient.eval("ls -la")          # High-level API call
    |
    v
channels.py: _eval()               # Build channel message dict
    |                               # {"ch": 1, "op": "eval", "cmd": "ls -la"}
    v
protocol.py: encode_frame()        # Serialize to NDJSON bytes
    |                               # b'{"ch":1,"op":"eval","cmd":"ls -la"}\n'
    v
transport.py: write()              # Send bytes over connection
    |
    v
[Network / Pipe / Socket]
    |
    v
bash-server                        # Receives and processes
```

### Inbound (Server to Client)

```
bash-server                        # Sends response
    |
    v
[Network / Pipe / Socket]
    |
    v
transport.py: read_line()         # Read one NDJSON line
    |                              # b'{"ch":1,"op":"complete","exit_code":0,...}\n'
    v
protocol.py: decode_frame()       # Parse JSON, extract channel ID
    |                              # (1, {"op": "complete", "exit_code": 0, ...})
    v
BashClient._reader_loop()         # Route by channel ID
    |
    +--[request/response]--> _channel_queues[ch].put()
    |                              # Waiting coroutine picks up via queue.get()
    |
    +--[server-push]-------> _push_callbacks[ch](event)
                                   # Invoke registered callbacks
```

---

## BashClient Internals

### Connection Lifecycle

```
BashClient()                       # 1. Construct (no connection)
    |
await client.connect(path)         # 2. Open transport + start reader loop
    |
await client.auth(token=...)       # 3. Authenticate (CONTROL channel)
    |
await client.eval("echo hi")      # 4. Use any channel method
await client.get_var("PATH")       #    (multiple calls, any order)
    |
await client.close()               # 5. Close transport + cancel reader
```

BashClient also supports the async context manager protocol:

```python
async with BashClient() as client:
    await client.connect(path)
    await client.auth(token=token)
    result = await client.eval("echo hello")
# __aexit__ calls close() automatically
```

### Reader Loop

The reader loop is a long-running asyncio Task created when the
transport connects. It continuously reads frames from the server and
routes them to the appropriate destination.

```python
async def _reader_loop(self):
    """Background task: read frames and dispatch."""
    try:
        while self._connected:
            line = await self._transport.read_line()
            if not line:
                break
            channel, data = decode_frame(line)
            await self._dispatch(channel, data)
    except asyncio.CancelledError:
        pass
    except Exception as e:
        self._reader_error = e
        self._connected = False
```

The reader loop:

1. Reads one NDJSON line at a time from the transport
2. Decodes the line into a channel ID and data dict
3. Dispatches based on whether this is a request-response or server-push
4. On EOF or error, marks the client as disconnected
5. On cancellation (from `close()`), exits cleanly

### Channel Queues

Each of the 6 channels has a dedicated `asyncio.Queue`. When a channel
method sends a request and awaits a response, it does:

```python
# In a channel method (simplified)
async def eval(self, cmd: str, timeout: float = None) -> EvalResult:
    # 1. Send the request frame
    await self._send(CHAN_COMMAND, {"op": "eval", "cmd": cmd})

    # 2. Collect responses until "complete"
    stdout_parts = []
    stderr_parts = []
    while True:
        msg = await asyncio.wait_for(
            self._channel_queues[CHAN_COMMAND].get(),
            timeout=timeout or self._default_timeout
        )
        if msg["op"] == "stdout":
            stdout_parts.append(msg["data"])
        elif msg["op"] == "stderr":
            stderr_parts.append(msg["data"])
        elif msg["op"] == "complete":
            return EvalResult(
                stdout="".join(stdout_parts),
                stderr="".join(stderr_parts),
                exit_code=msg["exit_code"],
                duration=msg.get("duration")
            )
        elif msg["op"] == "error":
            raise ChannelError(msg.get("message", "eval failed"))
```

The queue acts as a rendezvous point between the reader loop (producer)
and the channel method (consumer). This allows multiple channels to
operate concurrently -- each channel's responses go to its own queue.

### Server-Push Dispatch

Some channels produce unsolicited messages (server-push events):

- **OBSERVE** (channel 3): `pre_command`, `post_command` events
- **DEBUG** (channel 4): `breakpoint_hit`, `step_complete` events
- **PTY** (channel 5): `output`, `exit` events

These are dispatched to registered callbacks rather than queues:

```python
async def _dispatch(self, channel: int, data: dict):
    op = data.get("op", "")

    # Check if this is a server-push event
    if self._is_push_event(channel, op):
        for callback in self._push_callbacks.get(channel, []):
            if asyncio.iscoroutinefunction(callback):
                await callback(data)
            else:
                callback(data)
    else:
        # Request-response: put in channel queue
        await self._channel_queues[channel].put(data)
```

Users register callbacks via:

```python
client.on_observe(my_callback)    # OBSERVE events
client.on_debug(my_callback)      # DEBUG events
client.on_pty(my_callback)        # PTY events
```

Both sync and async callbacks are supported.

### Timeout Handling

Every operation that awaits a response has a timeout:

1. **Default timeout**: Set at construction (`BashClient(timeout=30.0)`)
2. **Per-call timeout**: Override on any method (`client.eval("...", timeout=60.0)`)
3. **Implementation**: Uses `asyncio.wait_for()` around queue gets

When a timeout fires, `asyncio.TimeoutError` is caught and re-raised
as `bashclient.errors.TimeoutError` with context about which operation
timed out.

---

## Transport Layer

### Transport ABC

All transports implement this interface:

```python
from abc import ABC, abstractmethod

class Transport(ABC):
    @abstractmethod
    async def connect(self, **kwargs) -> None:
        """Establish connection to bash-server."""
        ...

    @abstractmethod
    async def read_line(self) -> bytes:
        """Read one newline-terminated line. Returns b'' on EOF."""
        ...

    @abstractmethod
    async def write(self, data: bytes) -> None:
        """Write data to server."""
        ...

    @abstractmethod
    async def close(self) -> None:
        """Close the connection."""
        ...

    @property
    @abstractmethod
    def is_connected(self) -> bool:
        """Whether the transport is connected."""
        ...
```

### UnixSocketTransport

The default and most common transport. Connects to a bash-server Unix
domain socket.

```python
transport = UnixSocketTransport()
await transport.connect(path="/tmp/bash-server-1000/sock")
```

**Implementation details**:

- Uses `asyncio.open_unix_connection()` for non-blocking I/O
- Returns `asyncio.StreamReader` / `asyncio.StreamWriter` pair
- `read_line()` uses `reader.readline()` which buffers efficiently
- Socket path is validated before connection attempt
- Connection errors are wrapped in `ConnectionError`

### StdioTransport

Launches bash-server as a subprocess and communicates via stdin/stdout.

```python
transport = StdioTransport()
await transport.connect(cmd=["bash-server", "--stdio"])
```

**Implementation details**:

- Uses `asyncio.create_subprocess_exec()` with `PIPE` for stdin/stdout
- Server stderr is either captured or redirected to `/dev/null`
- `read_line()` reads from subprocess stdout
- `write()` writes to subprocess stdin
- `close()` sends QUIT, then terminates the subprocess
- Subprocess is cleaned up on garbage collection (via `__del__`)

### FdTransport

Connects via pre-existing file descriptors. Used when the connection
is already established by the parent process.

```python
transport = FdTransport()
await transport.connect(read_fd=3, write_fd=4)
```

**Implementation details**:

- Uses `asyncio.get_event_loop().connect_read_pipe()` and
  `connect_write_pipe()` to wrap raw FDs in async streams
- FDs must be open and valid before calling connect
- Does not close FDs on `close()` (caller is responsible)

### NamedPipeTransport

Connects to a Windows Named Pipe. Used on Cygwin or Windows for
cross-environment communication.

```python
transport = NamedPipeTransport()
await transport.connect(pipe_name=r"\\.\pipe\bash-server")
```

**Implementation details**:

- On Cygwin: Opens the Named Pipe as a regular file via the POSIX
  `/proc/sys/` interface or direct Cygwin path translation
- Wraps the pipe file descriptor in asyncio streams
- Named Pipe must already exist (created by bash-server)
- Authentication token may be delivered via the pipe path

---

## Protocol Layer

### NDJSON Framing

bashclient uses NDJSON (Newline-Delimited JSON) as its wire format.
This is the simplest framing option supported by bash-server v2.

Each frame is:

1. A single JSON object (no embedded newlines)
2. Terminated by a newline character (`\n`, 0x0A)

```
{"ch":0,"op":"auth","token":"abc123"}\n
{"ch":0,"op":"auth_ok","message":"authenticated"}\n
{"ch":1,"op":"eval","cmd":"echo hello"}\n
{"ch":1,"op":"stdout","data":"hello\n"}\n
{"ch":1,"op":"complete","exit_code":0}\n
```

### Frame Format

Every frame contains at minimum:

| Field  | Type | Required | Description                    |
|--------|------|----------|--------------------------------|
| `ch`   | int  | Yes      | Channel ID (0-5)              |
| `op`   | str  | Yes      | Operation name                 |

Additional fields depend on the channel and operation. For example,
an eval command frame:

```json
{"ch": 1, "op": "eval", "cmd": "ls -la"}
```

An eval response frame:

```json
{"ch": 1, "op": "stdout", "data": "total 42\n..."}
```

### Encoding and Decoding

**encode_frame(channel, data)**:

1. Merge `{"ch": channel}` into `data`
2. Serialize to JSON with `json.dumps()` (compact, no spaces)
3. Append `\n`
4. Encode to UTF-8 bytes

```python
def encode_frame(channel: int, data: dict) -> bytes:
    frame = {"ch": channel, **data}
    return (json.dumps(frame, separators=(",", ":")) + "\n").encode("utf-8")
```

**decode_frame(line)**:

1. Decode UTF-8 bytes
2. Strip trailing whitespace
3. Parse JSON with `json.loads()`
4. Extract and remove `"ch"` field
5. Return `(channel, remaining_data)`

```python
def decode_frame(line: bytes) -> tuple:
    obj = json.loads(line.decode("utf-8").rstrip())
    channel = obj.pop("ch")
    return (channel, obj)
```

### Base64 Helpers

Binary data (e.g., PTY output) is base64-encoded in JSON frames:

```python
def encode_base64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")

def decode_base64(s: str) -> bytes:
    return base64.b64decode(s)
```

---

## Channel Layer

### Channel IDs

```python
CHAN_CONTROL = 0
CHAN_COMMAND = 1
CHAN_STATE   = 2
CHAN_OBSERVE = 3
CHAN_DEBUG   = 4
CHAN_PTY     = 5
```

### Request-Response Pattern

Most channel operations follow request-response:

```
Client                              Server
  |                                   |
  |--- {"ch":2,"op":"get",...} ------>|
  |                                   |
  |<-- {"ch":2,"op":"result",...} ----|
  |                                   |
```

The channel method:

1. Calls `_send(channel, request_data)` to write the request frame
2. Calls `await _recv(channel, timeout)` to read from the channel queue
3. The reader loop receives the response and puts it in the queue
4. The channel method processes the response and returns a typed result

### Server-Push Pattern

Observe, debug, and PTY channels can produce events at any time:

```
Client                              Server
  |                                   |
  |--- {"ch":3,"op":"start"} ------->|   (subscribe)
  |                                   |
  |<-- {"ch":3,"op":"pre_cmd",...} ---|   (push event)
  |<-- {"ch":3,"op":"post_cmd",...} --|   (push event)
  |<-- {"ch":3,"op":"pre_cmd",...} ---|   (push event)
  |                                   |
  |--- {"ch":3,"op":"stop"} -------->|   (unsubscribe)
  |                                   |
```

Push events are routed to callbacks, not queues. The reader loop
identifies push events by operation name and channel ID.

### Channel Implementations

**CONTROL (channel 0)**:

- `auth(token)`: Send auth token, receive auth_ok or auth_fail
- `ping()`: Send ping, receive pong with server info
- `close()`: Send disconnect, close transport
- `configure(**opts)`: Set session options (observe level, wire format)

**COMMAND (channel 1)**:

- `eval(cmd, timeout)`: Execute command, collect stdout/stderr/exit_code
- `eval_parsed(ast_json, timeout)`: Execute pre-parsed COMMAND JSON

Eval collects multiple response frames (stdout chunks, stderr chunks)
until a `complete` or `error` frame arrives.

**STATE (channel 2)**:

- Variable operations: `get_var`, `set_var`, `unset_var`
- Function operations: `get_func`, `set_func`, `unset_func`
- Alias operations: `get_alias`, `set_alias`, `unset_alias`
- Trap operations: `get_trap`, `set_trap`
- `inspect(namespace)`: List all items in a namespace

Each operation is a single request-response exchange.

**OBSERVE (channel 3)**:

- `observe_start(level)`: Subscribe to command events
- `observe_stop()`: Unsubscribe
- `on_observe(callback)`: Register push event callback

Levels: `"basic"` (command + exit code), `"detailed"` (+ cwd, timing)

**DEBUG (channel 4)**:

- Breakpoint management: `breakpoint_set`, `breakpoint_clear`
- Execution control: `debug_step`, `debug_next`, `debug_finish`,
  `debug_skip`, `debug_continue`
- AST inspection: `inspect_ast()`
- `on_debug(callback)`: Register push event callback

**PTY (channel 5)**:

- `pty_spawn(cmd, rows, cols, strip_ansi)`: Create PTY session
- `pty_write(data)`: Send input to PTY
- `pty_resize(rows, cols)`: Resize terminal
- `pty_signal(sig)`: Send signal to PTY process
- `pty_close()`: Close PTY session
- `on_pty(callback)`: Register push event callback

---

## Message Routing

### Reader Loop Detail

The reader loop is the central dispatcher. Its complete logic:

```
read_line() from transport
    |
    v
decode_frame() -> (channel_id, data)
    |
    v
Is this a push event?
    |
    +-- YES --> iterate _push_callbacks[channel_id]
    |               invoke each callback(data)
    |
    +-- NO  --> _channel_queues[channel_id].put(data)
                    (consumer coroutine picks up via get())
```

### Dispatch Table

The reader loop uses operation names to distinguish push events from
request-response messages:

| Channel | Push Event Operations                          | Response Operations                          |
|---------|------------------------------------------------|----------------------------------------------|
| 0       | (none)                                         | auth_ok, auth_fail, pong, configure_ok       |
| 1       | (none)                                         | stdout, stderr, complete, error              |
| 2       | (none)                                         | result, ok, error                            |
| 3       | pre_command, post_command                       | start_ok, stop_ok                            |
| 4       | breakpoint_hit, step_complete                  | bp_set_ok, bp_clear_ok, ast_result           |
| 5       | output, exit                                   | spawn_ok, resize_ok, signal_ok, close_ok     |

### Queue Selection

When the reader loop determines a message is a response (not a push
event), it routes to the appropriate queue:

```python
PUSH_EVENTS = {
    CHAN_OBSERVE: {"pre_command", "post_command"},
    CHAN_DEBUG:   {"breakpoint_hit", "step_complete"},
    CHAN_PTY:     {"output", "exit"},
}

async def _dispatch(self, channel: int, data: dict):
    op = data.get("op", "")
    push_ops = PUSH_EVENTS.get(channel, set())

    if op in push_ops:
        for cb in self._push_callbacks.get(channel, []):
            if asyncio.iscoroutinefunction(cb):
                await cb(data)
            else:
                cb(data)
    else:
        await self._channel_queues[channel].put(data)
```

---

## Type System

### Dataclass Types

All result and event types are Python `dataclasses` with full type
annotations. This provides:

1. Structured attribute access (`result.stdout` vs `result["stdout"]`)
2. IDE auto-completion and type checking
3. Immutable-by-default semantics (`frozen=True`)
4. Readable `repr()` output for debugging

Example:

```python
@dataclass(frozen=True)
class EvalResult:
    """Result of a command evaluation."""
    stdout: str
    stderr: str
    exit_code: int
    duration: Optional[float] = None
```

### Type Annotations

The package uses `typing` module annotations throughout, compatible with
Python 3.8:

- `Optional[X]` for nullable fields
- `List[X]`, `Dict[K, V]` for containers (not `list[X]` which requires 3.9+)
- `Union[X, Y]` for alternatives
- `Callable[..., Any]` for callbacks
- `Awaitable` for async callables
- String literals for forward references where needed

---

## Error Hierarchy

```python
class BashClientError(Exception):
    """Base exception for all bashclient errors."""
    pass

class ConnectionError(BashClientError):
    """Transport connection failures."""
    pass

class AuthenticationError(BashClientError):
    """Authentication failures."""
    pass

class ProtocolError(BashClientError):
    """Wire protocol errors (malformed frames, unexpected data)."""
    pass

class TimeoutError(BashClientError):
    """Operation timeout."""
    pass

class ChannelError(BashClientError):
    """Channel-level errors (e.g., eval returned error op)."""
    pass
```

Each exception carries a descriptive message. `ConnectionError` and
`AuthenticationError` may also carry the server's error message if one
was received before the error.

---

## Testing Architecture

### Unit Tests

**File**: `tests/test_protocol.py`

Tests the protocol module in isolation. No I/O, no async. Pure
function tests:

- `encode_frame()` produces correct NDJSON
- `decode_frame()` parses valid frames
- `decode_frame()` raises `ProtocolError` on malformed input
- `encode_base64()` / `decode_base64()` round-trip correctly
- Edge cases: empty data, unicode, large payloads

### Channel Tests

**File**: `tests/test_channels.py`

Tests channel methods with a mock transport. The mock transport has
pre-loaded response sequences that simulate server replies.

```python
class MockTransport(Transport):
    def __init__(self, responses: List[bytes]):
        self._responses = iter(responses)
        self._written: List[bytes] = []

    async def read_line(self) -> bytes:
        return next(self._responses, b"")

    async def write(self, data: bytes) -> None:
        self._written.append(data)
    ...
```

Tests verify:

- Channel methods send correct request frames
- Channel methods parse response frames into correct types
- Error responses raise appropriate exceptions
- Timeouts raise `TimeoutError`
- Multi-frame responses (eval stdout chunks) are assembled correctly

### Integration Tests

**File**: `tests/test_integration.py`

Tests against a real bash-server instance using stdio transport.
These tests launch `bash-server --stdio` as a subprocess.

```python
@pytest.fixture
async def client():
    c = BashClient()
    await c.connect_stdio(["bash-server", "--stdio"])
    token = ...  # read from server output
    await c.auth(token=token)
    yield c
    await c.close()
```

Integration tests verify end-to-end behavior:

- Connect and authenticate
- Eval simple commands
- Eval commands with stdout and stderr
- State operations (get/set/unset variables)
- Observe events during command execution
- PTY spawn and I/O

Integration tests are skipped if `bash-server` is not available on PATH.

### Test Utilities

**File**: `tests/conftest.py`

Shared fixtures and helpers:

- `mock_transport`: Factory for MockTransport with pre-loaded responses
- `client_with_mock`: BashClient wired to a mock transport
- `bash_server_available`: Skip marker for integration tests
- `token_from_stderr`: Extract auth token from server startup output

---

## Design Decisions

### Why asyncio

bash-server uses a multiplexed protocol where the server can send
messages at any time (push events from observe, debug, PTY channels).
A synchronous client would need threads to handle push events while
waiting for responses. asyncio provides:

1. **Natural concurrency**: The reader loop and channel methods run as
   cooperating coroutines without threads.
2. **Multiplexing**: Multiple channel operations can be in-flight
   simultaneously.
3. **Callback support**: Push events are dispatched to callbacks
   naturally within the event loop.
4. **Standard library**: asyncio is built into Python 3.8+.

### Why No External Dependencies

bashclient has zero runtime dependencies beyond the Python standard
library. This decision prioritizes:

1. **Easy installation**: `pip install` with no dependency resolution
2. **No version conflicts**: Cannot conflict with user's other packages
3. **Security**: No supply chain attack surface from third-party packages
4. **Stability**: Standard library APIs are stable across Python versions
5. **Simplicity**: The protocol (NDJSON) is simple enough that `json`
   and `asyncio` are sufficient

The trade-off is that we implement some things from scratch (e.g., NDJSON
framing) rather than using a library. Given the protocol's simplicity,
this is a net positive.

### Why NDJSON Over Binary

bash-server v2 supports both binary framing (6-byte length-prefix) and
NDJSON. bashclient uses NDJSON exclusively because:

1. **Debuggability**: NDJSON is human-readable. You can inspect traffic
   with standard tools (`cat`, `jq`, `socat`).
2. **Simplicity**: No binary header parsing, no endianness concerns.
3. **Python affinity**: `json.loads()` / `json.dumps()` are fast and
   well-tested in Python.
4. **Line-based I/O**: Leverages `readline()` which is efficiently
   buffered in asyncio streams.

The overhead of JSON vs binary is negligible for a shell evaluation
protocol where the bottleneck is command execution, not serialization.

### Why Queues Per Channel

Each channel has its own `asyncio.Queue` rather than a single shared
queue. This enables:

1. **Concurrent channel use**: An eval on channel 1 and a get_var on
   channel 2 can be in-flight simultaneously.
2. **Simple routing**: The reader loop puts the message in the right
   queue; the consumer does not need to filter.
3. **No message ordering issues**: Channel 2 responses do not block
   channel 1 consumers.

The downside is slightly more memory (6 Queue objects), which is
negligible.

### Why Callbacks for Server-Push

Server-push events (observe, debug, PTY) use callbacks rather than
queues because:

1. **Multiple consumers**: Several parts of the user's code may want
   to observe the same events.
2. **Fire-and-forget**: Push events do not have a matching request
   that is waiting for a response.
3. **Flexibility**: Users can register sync or async callbacks, or
   use `on_observe()` in combination with their own queues.

An alternative design would be `async for event in client.observe():`
using async iterators. This may be added in a future version as a
complementary API.

### Why Dataclasses Over Dicts

Channel methods return dataclass instances rather than raw dicts:

1. **Type safety**: Attributes are defined with types, enabling
   static analysis and IDE support.
2. **Documentation**: Dataclass fields serve as self-documenting API.
3. **Immutability**: `frozen=True` prevents accidental mutation.
4. **Attribute access**: `result.stdout` is cleaner than `result["stdout"]`.
5. **repr()**: Dataclasses provide readable string representations.

The dict from the server response is converted to a dataclass in the
channel method, so the conversion cost is paid once per response.
