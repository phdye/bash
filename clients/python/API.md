# API Reference -- bashclient (Python)

> **Package**: `bashclient`
> **Version**: 0.1.0
> **Python**: 3.8+
> **Dependencies**: stdlib only (`asyncio`, `json`, `base64`, `socket`, `os`, `struct`, `sys`, `abc`, `dataclasses`)
> **Protocol**: bash-server v2 NDJSON (newline-delimited JSON)

This document is a complete API reference for every public class, method, type,
constant, and exception in the `bashclient` package. For tutorials and usage
patterns see [GUIDE.md](GUIDE.md). For debugging connection issues see
[TROUBLESHOOTING.md](TROUBLESHOOTING.md).

---

## Table of Contents

- [Package Overview](#package-overview)
- [Quick Start](#quick-start)
- [Constants](#constants)
  - [Channel IDs](#channel-ids)
  - [Protocol Limits](#protocol-limits)
  - [Observe Levels](#observe-levels)
- [Types (Dataclasses)](#types-dataclasses)
  - [EvalResult](#evalresult)
  - [VarInfo](#varinfo)
  - [FuncInfo](#funcinfo)
  - [AliasInfo](#aliasinfo)
  - [TrapInfo](#trapinfo)
  - [PreCommandEvent](#precommandevent)
  - [PostCommandEvent](#postcommandevent)
  - [Breakpoint](#breakpoint)
  - [BreakHitEvent](#breakhitevent)
  - [DebugStatus](#debugstatus)
  - [PtyInfo](#ptyinfo)
- [Errors](#errors)
  - [BashClientError](#bashclienterror)
  - [AuthError](#autherror)
  - [ProtocolError](#protocolerror)
  - [TimeoutError](#timeouterror)
  - [TransportError](#transporterror)
  - [ServerError](#servererror)
- [Transport Classes](#transport-classes)
  - [Transport (ABC)](#transport-abc)
  - [UnixSocketTransport](#unixsockettransport)
  - [StdioTransport](#stdiotransport)
  - [FdTransport](#fdtransport)
  - [NamedPipeTransport](#namedpipetransport)
- [BashClient](#bashclient)
  - [Constructor](#constructor)
  - [Factory Methods](#factory-methods)
  - [Instance Methods](#instance-methods)
  - [Properties](#properties)
  - [Channel Properties](#channel-properties)
  - [Async Context Manager](#async-context-manager)
- [Channel Classes](#channel-classes)
  - [ControlChannel](#controlchannel)
  - [CommandChannel](#commandchannel)
  - [StateChannel](#statechannel)
  - [ObserveChannel](#observechannel)
  - [DebugChannel](#debugchannel)
  - [PtyChannel](#ptychannel)
- [Protocol Functions](#protocol-functions)
  - [encode_frame](#encode_frame)
  - [decode_frame](#decode_frame)
  - [b64encode](#b64encode)
  - [b64decode](#b64decode)
  - [make_msg](#make_msg)
  - [get_channel](#get_channel)
  - [get_type](#get_type)

---

## Package Overview

The `bashclient` package provides an async Python client for the bash-server v2
NDJSON protocol. It communicates with a persistent Bash evaluation daemon over
multiplexed JSON channels, supporting command evaluation, shell state
manipulation, command observation, interactive debugging, and pseudo-terminal
I/O.

**Architecture:**

```
BashClient
  |-- Transport (UnixSocket | Stdio | Fd | NamedPipe)
  |-- _reader_loop() -- background task dispatching messages to channels
  |-- ControlChannel  (ch=0)  auth, ping, configure, disconnect
  |-- CommandChannel   (ch=1)  eval, stdout/stderr/complete collection
  |-- StateChannel     (ch=2)  get/set/unset vars, funcs, aliases, traps
  |-- ObserveChannel   (ch=3)  subscribe to pre/post command events
  |-- DebugChannel     (ch=4)  breakpoints, stepping, AST inspection
  |-- PtyChannel       (ch=5)  spawn PTY, I/O relay, resize, signal
```

Messages are NDJSON lines: one JSON object per line, terminated by `\n`.
Each message has a `"ch"` (channel ID) and `"type"` (message type) field.
See `bash-server-ndjson(5)` for the wire format specification.

**Module structure:**

| Module | Contents |
|--------|----------|
| `bashclient.client` | `BashClient` class |
| `bashclient.channels` | `ControlChannel`, `CommandChannel`, `StateChannel`, `ObserveChannel`, `DebugChannel`, `PtyChannel` |
| `bashclient.transport` | `Transport` (ABC), `UnixSocketTransport`, `StdioTransport`, `FdTransport`, `NamedPipeTransport` |
| `bashclient.types` | Dataclasses and constants |
| `bashclient.errors` | Exception hierarchy |
| `bashclient.protocol` | Frame encode/decode, message construction |

All public names are re-exported from `bashclient.__init__`.

---

## Quick Start

```python
import asyncio
from bashclient import BashClient

async def main():
    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth("a1b2c3...")  # 64 hex chars
        result = await client.eval("echo hello world")
        print(result.stdout)   # "hello world\n"
        print(result.exit_code)  # 0

asyncio.run(main())
```

---

## Constants

Defined in `bashclient.types`. All constants are module-level integers.

### Channel IDs

Each v2 protocol channel has a numeric identifier used in the `"ch"` field of
every NDJSON message. See `bash-server-channels(7)` for the channel
architecture.

| Constant | Value | Description |
|----------|-------|-------------|
| `CHAN_CONTROL` | `0` | Authentication, ping, configuration, disconnect |
| `CHAN_COMMAND` | `1` | Command evaluation with stdout/stderr capture |
| `CHAN_STATE` | `2` | Variable, function, alias, and trap management |
| `CHAN_OBSERVE` | `3` | Pre/post command observation events |
| `CHAN_DEBUG` | `4` | Breakpoints, stepping, AST inspection |
| `CHAN_PTY` | `5` | Pseudo-terminal spawn, I/O, resize, signal |
| `CHAN_MAX` | `5` | Highest valid channel ID |

```python
from bashclient import CHAN_CONTROL, CHAN_COMMAND, CHAN_STATE
from bashclient import CHAN_OBSERVE, CHAN_DEBUG, CHAN_PTY
```

### Protocol Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `FRAME_MAX_PAYLOAD` | `1048576` (1 MB) | Maximum NDJSON line size in bytes. Frames exceeding this limit raise `ProtocolError`. |
| `TOKEN_HEXLEN` | `64` | Expected length of authentication token in hex characters (256 bits). |

### Observe Levels

Control the verbosity of command observation events on channel 3.

| Constant | Value | Description |
|----------|-------|-------------|
| `OBSERVE_LEVEL_OFF` | `0` | Observation disabled; no events emitted. |
| `OBSERVE_LEVEL_COMMAND` | `1` | Emit `pre_command` and `post_command` events for each command executed. |
| `OBSERVE_LEVEL_MAX` | `1` | Highest supported observation level. |

```python
from bashclient.types import OBSERVE_LEVEL_OFF, OBSERVE_LEVEL_COMMAND

await client.observe.subscribe(level=OBSERVE_LEVEL_COMMAND)
```

---

## Types (Dataclasses)

All types are defined in `bashclient.types` using `@dataclass` from the
standard library. They are frozen-friendly plain data containers with no
methods beyond those provided by `@dataclass`.

### EvalResult

```python
@dataclass
class EvalResult:
    stdout: str
    stderr: str
    exit_code: int
```

Result of a command evaluation on channel 1.

**Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `stdout` | `str` | Captured standard output of the command. May contain trailing newlines. |
| `stderr` | `str` | Captured standard error of the command. Empty string if no errors. |
| `exit_code` | `int` | Exit status of the command. `0` indicates success; `1`--`255` indicates failure. `-1` if the completion message was not received. |

**Example:**

```python
result = await client.eval("ls /nonexistent")
if result.exit_code != 0:
    print(f"Error: {result.stderr}")
```

---

### VarInfo

```python
@dataclass
class VarInfo:
    name: str
    value: str
    attributes: List[str] = field(default_factory=list)
```

Information about a shell variable, returned by `StateChannel.get_var()`.

**Fields:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `name` | `str` | -- | Variable name (e.g., `"PATH"`, `"HOME"`). |
| `value` | `str` | -- | Current value of the variable. |
| `attributes` | `List[str]` | `[]` | Bash variable attributes (e.g., `["readonly"]`, `["export"]`, `["integer"]`, `["array"]`). |

**Example:**

```python
var = await client.state.get_var("HOME")
print(f"{var.name}={var.value}")  # HOME=/home/user
print(var.attributes)              # ["export"]
```

---

### FuncInfo

```python
@dataclass
class FuncInfo:
    name: str
    definition: str
```

Information about a shell function, returned by `StateChannel.get_func()`.

**Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | `str` | Function name. |
| `definition` | `str` | Complete function definition body as Bash source code. |

**Example:**

```python
func = await client.state.get_func("my_func")
print(func.definition)
# { echo "hello"; return 0; }
```

---

### AliasInfo

```python
@dataclass
class AliasInfo:
    name: str
    value: str
```

Information about a shell alias, returned by `StateChannel.get_alias()`.

**Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `name` | `str` | Alias name (e.g., `"ll"`). |
| `value` | `str` | Alias expansion text (e.g., `"ls -la"`). |

**Example:**

```python
alias = await client.state.get_alias("ll")
print(f"alias {alias.name}='{alias.value}'")
# alias ll='ls -la'
```

---

### TrapInfo

```python
@dataclass
class TrapInfo:
    signal: str
    command: str
```

Information about a signal trap.

**Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `signal` | `str` | Signal name (e.g., `"INT"`, `"EXIT"`, `"ERR"`, `"DEBUG"`). |
| `command` | `str` | Command string executed when the trap fires. |

**Example:**

```python
trap = TrapInfo(signal="EXIT", command="cleanup")
```

---

### PreCommandEvent

```python
@dataclass
class PreCommandEvent:
    seq: int
    timestamp: int
    command: str
    cwd: str
    line_number: int = 0
    is_subshell: bool = False
    is_async: bool = False
```

Observation event emitted before a command executes. Delivered to callbacks
registered with `ObserveChannel.on("pre_command", ...)`.

**Fields:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `seq` | `int` | -- | Monotonically increasing sequence number. |
| `timestamp` | `int` | -- | Unix timestamp in milliseconds when the event was generated. |
| `command` | `str` | -- | The command string about to be executed. |
| `cwd` | `str` | -- | Current working directory at the time of execution. |
| `line_number` | `int` | `0` | Line number in the script (0 if interactive). |
| `is_subshell` | `bool` | `False` | `True` if the command runs in a subshell. |
| `is_async` | `bool` | `False` | `True` if the command was launched asynchronously (with `&`). |

**Example:**

```python
def on_pre(event: PreCommandEvent):
    print(f"[{event.seq}] About to run: {event.command} in {event.cwd}")

client.observe.on("pre_command", on_pre)
await client.observe.subscribe(level=1)
```

---

### PostCommandEvent

```python
@dataclass
class PostCommandEvent:
    seq: int
    timestamp: int
    command: str
    exit_status: int
    signal_number: int = 0
    duration_ms: int = 0
```

Observation event emitted after a command completes. Delivered to callbacks
registered with `ObserveChannel.on("post_command", ...)`.

**Fields:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `seq` | `int` | -- | Sequence number matching the corresponding `PreCommandEvent`. |
| `timestamp` | `int` | -- | Unix timestamp in milliseconds when the event was generated. |
| `command` | `str` | -- | The command string that completed. |
| `exit_status` | `int` | -- | Exit status of the command. |
| `signal_number` | `int` | `0` | Signal number if the command was killed by a signal; `0` otherwise. |
| `duration_ms` | `int` | `0` | Wall-clock execution time in milliseconds. |

**Example:**

```python
def on_post(event: PostCommandEvent):
    status = "OK" if event.exit_status == 0 else f"FAIL({event.exit_status})"
    print(f"[{event.seq}] {event.command} -> {status} ({event.duration_ms}ms)")

client.observe.on("post_command", on_post)
```

---

### Breakpoint

```python
@dataclass
class Breakpoint:
    id: int
    kind: str
    enabled: bool = True
    hit_count: int = 0
    pattern: Optional[str] = None
    line: Optional[int] = None
    condition: Optional[str] = None
```

A debugger breakpoint, returned by `DebugChannel.list_breakpoints()`.

**Fields:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `id` | `int` | -- | Unique breakpoint identifier assigned by the server. |
| `kind` | `str` | -- | Breakpoint kind: `"command"`, `"line"`, or `"function"`. |
| `enabled` | `bool` | `True` | Whether the breakpoint is currently enabled. |
| `hit_count` | `int` | `0` | Number of times this breakpoint has been hit. |
| `pattern` | `Optional[str]` | `None` | Glob pattern for command/function breakpoints (e.g., `"ls*"`). |
| `line` | `Optional[int]` | `None` | Line number for line breakpoints. |
| `condition` | `Optional[str]` | `None` | Bash expression evaluated as a conditional guard. The breakpoint fires only when the expression evaluates to true (exit code 0). |

**Example:**

```python
bps = await client.debug.list_breakpoints()
for bp in bps:
    state = "ON" if bp.enabled else "OFF"
    print(f"  #{bp.id} [{state}] {bp.kind} hits={bp.hit_count}")
```

---

### BreakHitEvent

```python
@dataclass
class BreakHitEvent:
    line: int
    command: str
    depth: int = 0
```

Event delivered when execution hits a breakpoint. Dispatched to callbacks
registered with `DebugChannel.on("break_hit", ...)`.

**Fields:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `line` | `int` | -- | Source line number where the break occurred. |
| `command` | `str` | -- | The command at the breakpoint. |
| `depth` | `int` | `0` | Function call nesting depth (0 = top level). |

**Example:**

```python
async def on_break(event: BreakHitEvent):
    print(f"Hit breakpoint at line {event.line}: {event.command}")
    ast = await client.debug.inspect_ast()
    print(f"AST: {ast}")
    await client.debug.continue_()

client.debug.on("break_hit", on_break)
```

---

### DebugStatus

```python
@dataclass
class DebugStatus:
    active: bool
    mode: str
    breakpoints: int
    depth: int = 0
```

Current state of the debugger, returned by `DebugChannel.status()`.

**Fields:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `active` | `bool` | -- | `True` if the debugger is currently enabled. |
| `mode` | `str` | -- | Current execution mode: `"run"`, `"step"`, `"next"`, or `"finish"`. |
| `breakpoints` | `int` | -- | Total number of registered breakpoints (enabled and disabled). |
| `depth` | `int` | `0` | Current function call nesting depth. |

**Example:**

```python
status = await client.debug.status()
if status.active:
    print(f"Debugger active, mode={status.mode}, {status.breakpoints} breakpoints")
```

---

### PtyInfo

```python
@dataclass
class PtyInfo:
    rows: int
    cols: int
    pid: int
    strip_ansi: bool = False
```

Information about a spawned PTY session, returned by `PtyChannel.spawn()`.

**Fields:**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `rows` | `int` | -- | Terminal height in rows. |
| `cols` | `int` | -- | Terminal width in columns. |
| `pid` | `int` | -- | Process ID of the PTY child process on the server. |
| `strip_ansi` | `bool` | `False` | `True` if the server strips ANSI escape sequences from output. |

**Example:**

```python
info = await client.pty.spawn(rows=40, cols=120, strip_ansi=True)
print(f"PTY pid={info.pid}, {info.cols}x{info.rows}, strip={info.strip_ansi}")
```

---

## Errors

All exceptions are defined in `bashclient.errors` and re-exported from
`bashclient`. The hierarchy is:

```
Exception
  +-- BashClientError
        +-- AuthError
        +-- ProtocolError
        +-- TimeoutError
        +-- TransportError
        +-- ServerError
```

### BashClientError

```python
class BashClientError(Exception):
    """Base exception for all bashclient errors."""
```

Base class for every exception raised by the `bashclient` package. Catch this
to handle any client error generically.

**Example:**

```python
try:
    await client.eval("exit 1")
except BashClientError as e:
    print(f"Client error: {e}")
```

---

### AuthError

```python
class AuthError(BashClientError):
    """Authentication failed."""
```

Raised when authentication fails. This typically means:

- The token is incorrect.
- The token has the wrong length (expected 64 hex characters).
- The server rejected the auth attempt.

**Raised by:** `BashClient.auth()`, `ControlChannel.auth()`

**Example:**

```python
try:
    await client.auth("bad_token")
except AuthError as e:
    print(f"Auth failed: {e}")
```

---

### ProtocolError

```python
class ProtocolError(BashClientError):
    """Malformed frame or message."""
```

Raised when:

- A received NDJSON line cannot be parsed as valid JSON.
- A frame exceeds `FRAME_MAX_PAYLOAD` (1 MB).
- A message is missing required fields (e.g., no `"type"` field).
- A message cannot be encoded to JSON.
- Base64 decoding fails.

**Raised by:** `decode_frame()`, `encode_frame()`, `b64decode()`, `get_type()`

**Example:**

```python
from bashclient.protocol import decode_frame
from bashclient.errors import ProtocolError

try:
    msg = decode_frame(b"not valid json\n")
except ProtocolError as e:
    print(f"Bad frame: {e}")
```

---

### TimeoutError

```python
class TimeoutError(BashClientError):
    """Operation timed out."""
```

Raised when a response is not received within the specified timeout period.
The default timeout for most operations is 30 seconds.

Note: This is `bashclient.TimeoutError`, not the built-in `TimeoutError`.
They are distinct classes.

**Raised by:** `BashClient._recv()`, `CommandChannel.eval()` (indirectly via `asyncio.wait_for`)

**Example:**

```python
from bashclient import TimeoutError

try:
    result = await client.eval("sleep 60", timeout=5.0)
except TimeoutError:
    print("Command timed out after 5 seconds")
```

---

### TransportError

```python
class TransportError(BashClientError):
    """Socket or I/O error."""
```

Raised when the underlying transport encounters an I/O error:

- Cannot connect to the Unix socket or Named Pipe.
- Connection is closed unexpectedly by the server.
- Read or write fails due to OS-level errors.
- Attempting to read/write on a transport that is not connected.

**Raised by:** All `Transport` subclass methods (`connect`, `read_line`, `write`, `close`)

**Example:**

```python
from bashclient import BashClient
from bashclient.errors import TransportError

try:
    client = await BashClient.connect("/nonexistent/sock")
except TransportError as e:
    print(f"Connection failed: {e}")
```

---

### ServerError

```python
class ServerError(BashClientError):
    """Server returned an error response."""

    def __init__(self, message: str, channel: int = -1):
        super().__init__(message)
        self.channel = channel
```

Raised when the server sends an error response (`"type": "error"`) on any
channel. The `channel` attribute indicates which channel generated the error.

**Attributes:**

| Attribute | Type | Description |
|-----------|------|-------------|
| `channel` | `int` | Channel ID where the error originated. `-1` if unknown. Use `CHAN_*` constants for comparison. |

**Raised by:** `CommandChannel.eval()`, `StateChannel` methods, `ObserveChannel.subscribe()`, `DebugChannel` methods, `PtyChannel` methods

**Example:**

```python
from bashclient import CHAN_STATE
from bashclient.errors import ServerError

try:
    var = await client.state.get_var("NONEXISTENT")
except ServerError as e:
    print(f"Server error on channel {e.channel}: {e}")
    if e.channel == CHAN_STATE:
        print("State operation failed")
```

---

## Transport Classes

Transport classes handle the raw byte-level I/O between the client and the
bash-server process. Each transport implements the `Transport` abstract base
class. Users typically do not interact with transports directly; the
`BashClient` factory methods handle transport creation.

All transports are defined in `bashclient.transport`.

### Transport (ABC)

```python
class Transport(ABC):
    """Abstract transport for reading/writing bytes."""
```

Abstract base class that all transport implementations must subclass.

#### Methods

##### `read_line`

```python
@abstractmethod
async def read_line(self) -> bytes:
```

Read one NDJSON line from the transport, including the trailing newline.

**Returns:** `bytes` -- A single line of bytes ending with `b"\n"`.

**Raises:**
- `TransportError` -- If the transport is not connected or a read error occurs.

---

##### `write`

```python
@abstractmethod
async def write(self, data: bytes) -> None:
```

Write raw bytes to the transport.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `data` | `bytes` | Bytes to write. Typically an encoded NDJSON frame. |

**Raises:**
- `TransportError` -- If the transport is not connected or a write error occurs.

---

##### `close`

```python
@abstractmethod
async def close(self) -> None:
```

Close the transport. Releases all resources. Safe to call multiple times.

---

##### `is_open` (property)

```python
@property
@abstractmethod
def is_open(self) -> bool:
```

Whether the transport is currently open and operational.

**Returns:** `bool` -- `True` if connected and ready for I/O.

---

### UnixSocketTransport

```python
class UnixSocketTransport(Transport):
    """Unix domain socket transport."""
```

Connects to bash-server via a Unix domain socket. This is the default and
most common transport. Corresponds to the server's default socket mode.

#### `__init__`

```python
def __init__(self) -> None:
```

Create an unconnected Unix socket transport. Call `connect()` before use.

#### `connect`

```python
async def connect(self, path: str) -> None:
```

Connect to a Unix domain socket at the given path.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `path` | `str` | Filesystem path to the Unix socket (e.g., `"/tmp/bash-server-1000/sock"`). |

**Raises:**
- `TransportError` -- If the connection fails (socket does not exist, permission denied, etc.).

**Example:**

```python
transport = UnixSocketTransport()
await transport.connect("/tmp/bash-server-1000/sock")
client = await BashClient.from_transport(transport)
```

---

### StdioTransport

```python
class StdioTransport(Transport):
    """Transport over stdin/stdout (for --stdio mode)."""
```

Communicates with bash-server through a subprocess's stdin/stdout pipes.
Used with the server's `--stdio` mode, where the server reads from stdin
and writes to stdout instead of listening on a socket.

#### `__init__`

```python
def __init__(self) -> None:
```

Create an unconnected stdio transport. Call `connect_process()` or
`connect_streams()` before use.

#### `connect_process`

```python
async def connect_process(self, *args: str) -> None:
```

Start a subprocess and connect to its stdin/stdout.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `*args` | `str` | Command and arguments to spawn (e.g., `"bash-server"`, `"--stdio"`). |

**Raises:**
- `TransportError` -- If the subprocess cannot be started.

**Example:**

```python
transport = StdioTransport()
await transport.connect_process("bash-server", "--stdio")
client = await BashClient.from_transport(transport)
```

#### `connect_streams`

```python
async def connect_streams(
    self,
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
) -> None:
```

Use pre-existing asyncio streams instead of spawning a subprocess.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `reader` | `asyncio.StreamReader` | Stream to read server responses from. |
| `writer` | `asyncio.StreamWriter` | Stream to write client requests to. |

**Example:**

```python
reader, writer = await asyncio.open_connection("localhost", 9090)
transport = StdioTransport()
await transport.connect_streams(reader, writer)
```

---

### FdTransport

```python
class FdTransport(Transport):
    """Transport over an inherited file descriptor."""
```

Communicates over a pre-opened file descriptor. Used with the server's
`--fd N` mode where a parent process passes a connected socket as an
inherited file descriptor.

#### `__init__`

```python
def __init__(self) -> None:
```

Create an unconnected fd transport. Call `connect()` before use.

#### `connect`

```python
async def connect(self, fd: int) -> None:
```

Wrap an inherited file descriptor as an async transport.

The file descriptor is duplicated internally for writing so that read and
write operations are independent.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `fd` | `int` | File descriptor number (must be open and readable/writable). |

**Raises:**
- `TransportError` -- If the file descriptor cannot be opened.

**Example:**

```python
import os

# Parent process sets up socketpair and passes one end as fd 3
transport = FdTransport()
await transport.connect(3)
client = await BashClient.from_transport(transport)
```

---

### NamedPipeTransport

```python
class NamedPipeTransport(Transport):
    """Windows Named Pipe transport (Cygwin)."""
```

Connects to bash-server via a Windows Named Pipe. This transport is
available only on Cygwin and corresponds to the server's `--named-pipe`
mode. Native Windows is not yet supported.

#### `__init__`

```python
def __init__(self) -> None:
```

Create an unconnected Named Pipe transport. Call `connect()` before use.

#### `connect`

```python
async def connect(self, pipe_name: str) -> None:
```

Connect to a Windows Named Pipe.

On Cygwin, Named Pipes are accessible through the filesystem. The
`pipe_name` should be the Cygwin-accessible path or the pipe name.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `pipe_name` | `str` | Named Pipe path or name (e.g., `"bash-server"`). |

**Raises:**
- `TransportError` -- If the connection fails or native Windows is detected (not yet supported).

**Example:**

```python
transport = NamedPipeTransport()
await transport.connect("bash-server")
client = await BashClient.from_transport(transport)
```

---

## BashClient

```python
class BashClient:
    """Async client for bash-server v2 NDJSON protocol."""
```

The main entry point for communicating with bash-server. Provides factory
methods for each transport type, convenience wrappers for common operations,
and properties for accessing the six v2 protocol channels.

Defined in `bashclient.client`.

### Constructor

```python
def __init__(self, transport: Transport) -> None:
```

Create a `BashClient` from an already-connected transport.

Most users should use the factory classmethods (`connect`, `connect_stdio`,
etc.) instead, which handle transport creation and start the background
reader task.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `transport` | `Transport` | A connected transport instance. |

**Note:** The constructor does **not** start the background reader loop.
Use `from_transport()` for manual transport construction, which does start
the reader.

---

### Factory Methods

All factory methods are `@classmethod` coroutines that create, connect, and
return a ready-to-use `BashClient`.

#### `connect`

```python
@classmethod
async def connect(cls, socket_path: str) -> "BashClient":
```

Connect to bash-server via a Unix domain socket.

This is the most common way to create a client. The server must already be
running and listening on the given socket path.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `socket_path` | `str` | Path to the Unix domain socket (e.g., `"/tmp/bash-server-1000/sock"`). |

**Returns:** `BashClient` -- A connected client with the background reader started.

**Raises:**
- `TransportError` -- If the socket does not exist or the connection is refused.

**Example:**

```python
client = await BashClient.connect("/tmp/bash-server-1000/sock")
try:
    await client.auth(token)
    result = await client.eval("whoami")
finally:
    await client.close()
```

---

#### `connect_stdio`

```python
@classmethod
async def connect_stdio(cls, *args: str) -> "BashClient":
```

Connect by spawning a bash-server subprocess in `--stdio` mode.

The subprocess is started with the given arguments. Its stdin/stdout are
used as the transport. The subprocess is terminated when the client is
closed.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `*args` | `str` | Command and arguments (e.g., `"bash-server"`, `"--stdio"`, `"--norc"`). |

**Returns:** `BashClient` -- A connected client.

**Raises:**
- `TransportError` -- If the subprocess cannot be started.

**Example:**

```python
client = await BashClient.connect_stdio("bash-server", "--stdio", "--norc")
try:
    await client.auth(token)
    result = await client.eval("echo hello")
finally:
    await client.close()
```

---

#### `connect_fd`

```python
@classmethod
async def connect_fd(cls, fd: int) -> "BashClient":
```

Connect via an inherited file descriptor.

Used in scenarios where a parent process establishes the connection and
passes the file descriptor to the child (e.g., via `--fd N` or
`--auth-fd N`).

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `fd` | `int` | Open file descriptor number. |

**Returns:** `BashClient` -- A connected client.

**Raises:**
- `TransportError` -- If the file descriptor is invalid.

**Example:**

```python
# Assuming fd 3 was inherited from parent process
client = await BashClient.connect_fd(3)
```

---

#### `connect_named_pipe`

```python
@classmethod
async def connect_named_pipe(cls, pipe_name: str) -> "BashClient":
```

Connect via a Windows Named Pipe (Cygwin only).

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `pipe_name` | `str` | Named Pipe path or identifier. |

**Returns:** `BashClient` -- A connected client.

**Raises:**
- `TransportError` -- If the connection fails or the platform is not supported.

**Example:**

```python
client = await BashClient.connect_named_pipe("bash-server")
```

---

#### `from_transport`

```python
@classmethod
async def from_transport(cls, transport: Transport) -> "BashClient":
```

Create a client from a pre-connected transport instance.

Use this when you need full control over transport construction or are
using a custom `Transport` subclass.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `transport` | `Transport` | An already-connected `Transport` instance. |

**Returns:** `BashClient` -- A client wrapping the given transport, with the background reader started.

**Example:**

```python
transport = UnixSocketTransport()
await transport.connect("/tmp/bash-server-1000/sock")
client = await BashClient.from_transport(transport)
```

---

### Instance Methods

#### `auth`

```python
async def auth(self, token: str) -> None:
```

Authenticate with the bash-server.

This must be called before any other operation (except `ping`). The token
is a 64-character hex string (256 bits) that the server generated at
startup. The token can be read from the server's config file or passed
via the `--token` CLI flag.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `token` | `str` | Authentication token, 64 hex characters. |

**Raises:**
- `AuthError` -- If the token is rejected or the response is unexpected.
- `TimeoutError` -- If no response is received within the default timeout.
- `TransportError` -- If the connection is broken.

**Example:**

```python
token = "a1b2c3d4e5f6..."  # 64 hex characters
await client.auth(token)
assert client.is_authenticated
```

---

#### `eval`

```python
async def eval(self, command: str, timeout: float = 30.0) -> EvalResult:
```

Evaluate a shell command and return the result.

This is a convenience wrapper around `CommandChannel.eval()`. The command
is executed in the server's Bash environment; stdout and stderr are
captured and returned along with the exit code.

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `command` | `str` | -- | Shell command to evaluate. |
| `timeout` | `float` | `30.0` | Maximum seconds to wait for completion. |

**Returns:** `EvalResult` -- Captured stdout, stderr, and exit code.

**Raises:**
- `ServerError` -- If the server returns an error.
- `TimeoutError` -- If the command does not complete within `timeout`.
- `TransportError` -- If the connection is broken.

**Example:**

```python
result = await client.eval("echo $((2 + 2))")
assert result.stdout.strip() == "4"
assert result.exit_code == 0
```

```python
# With custom timeout for long-running commands
result = await client.eval("find / -name '*.log' 2>/dev/null", timeout=120.0)
```

---

#### `ping`

```python
async def ping(self) -> None:
```

Ping the server to verify the connection is alive.

Sends a ping on the control channel and waits for a pong response.

**Raises:**
- `ServerError` -- If the response is not a pong.
- `TimeoutError` -- If no response is received.
- `TransportError` -- If the connection is broken.

**Example:**

```python
await client.ping()
print("Server is alive")
```

---

#### `close`

```python
async def close(self) -> None:
```

Close the client connection gracefully.

Sends a disconnect message to the server, cancels the background reader
task, and closes the underlying transport. Safe to call multiple times.

The disconnect message has a 2-second timeout; if the server does not
respond, the connection is closed forcefully.

**Example:**

```python
client = await BashClient.connect(path)
try:
    await client.auth(token)
    # ... use client ...
finally:
    await client.close()
```

---

### Properties

#### `is_connected`

```python
@property
def is_connected(self) -> bool:
```

Whether the underlying transport is open.

**Returns:** `bool` -- `True` if the transport reports `is_open`.

**Example:**

```python
if not client.is_connected:
    client = await BashClient.connect(path)
```

---

#### `is_authenticated`

```python
@property
def is_authenticated(self) -> bool:
```

Whether authentication has completed successfully.

**Returns:** `bool` -- `True` after a successful call to `auth()`.

**Example:**

```python
client = await BashClient.connect(path)
assert not client.is_authenticated
await client.auth(token)
assert client.is_authenticated
```

---

### Channel Properties

The client exposes six channel objects, one per v2 protocol channel. Each
channel provides typed methods for its domain. See
[Channel Classes](#channel-classes) for full documentation.

| Property | Type | Channel ID | Description |
|----------|------|------------|-------------|
| `client.control` | `ControlChannel` | 0 | Auth, ping, configure, disconnect |
| `client.command` | `CommandChannel` | 1 | Command evaluation |
| `client.state` | `StateChannel` | 2 | Variables, functions, aliases, traps |
| `client.observe` | `ObserveChannel` | 3 | Command observation events |
| `client.debug` | `DebugChannel` | 4 | Debugging and breakpoints |
| `client.pty` | `PtyChannel` | 5 | Pseudo-terminal I/O |

**Example:**

```python
# Direct channel access for advanced operations
resp = await client.control.configure(observe_level=1)
var = await client.state.get_var("PATH")
await client.debug.enable()
```

---

### Async Context Manager

`BashClient` implements the async context manager protocol. Using `async with`
ensures that `close()` is called even if an exception occurs.

```python
async def __aenter__(self) -> "BashClient":
async def __aexit__(self, *exc: Any) -> None:
```

**Example:**

```python
async with await BashClient.connect(path) as client:
    await client.auth(token)
    result = await client.eval("date")
    print(result.stdout)
# client.close() is called automatically here
```

```python
# Also works with connect_stdio
async with await BashClient.connect_stdio("bash-server", "--stdio") as client:
    await client.auth(token)
    # ...
```

---

## Channel Classes

Channel classes provide typed interfaces for each of the six v2 protocol
channels. They are instantiated internally by `BashClient` and accessed via
properties. See `bash-server-channels(7)` for the server-side channel
architecture.

All channel classes are defined in `bashclient.channels`.

### ControlChannel

```python
class ControlChannel:
    """Channel 0: auth, ping, configure, disconnect."""
```

Handles authentication, heartbeat, configuration, and graceful disconnect.
This is always channel 0 (`CHAN_CONTROL`).

#### `auth`

```python
async def auth(self, token: str) -> Dict[str, Any]:
```

Send an authentication request.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `token` | `str` | 64-character hex authentication token. |

**Returns:** `Dict[str, Any]` -- The full `auth_ok` response message from the server. Contains at minimum `{"ch": 0, "type": "auth_ok"}` and may include server capabilities.

**Raises:**
- `AuthError` -- If the server responds with an error or unexpected message type.
- `TimeoutError` -- If no response within the default timeout.

**Wire messages:**

```
-> {"ch":0,"type":"auth","token":"a1b2c3..."}
<- {"ch":0,"type":"auth_ok"}
```

**Example:**

```python
resp = await client.control.auth("a1b2c3d4e5f67890...")
print(resp)  # {"ch": 0, "type": "auth_ok", ...}
```

---

#### `ping`

```python
async def ping(self) -> None:
```

Send a ping and wait for a pong response.

**Raises:**
- `ServerError` -- If the server responds with something other than `pong`.
- `TimeoutError` -- If no response within the default timeout.

**Wire messages:**

```
-> {"ch":0,"type":"ping"}
<- {"ch":0,"type":"pong"}
```

**Example:**

```python
await client.control.ping()
```

---

#### `configure`

```python
async def configure(self, **kwargs: Any) -> Dict[str, Any]:
```

Send a configuration request to the server.

Configuration options are passed as keyword arguments and vary by server
version. Common options include `observe_level` and `wire_format`.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `**kwargs` | `Any` | Configuration key-value pairs. |

**Returns:** `Dict[str, Any]` -- The server's configuration response.

**Raises:**
- `TimeoutError` -- If no response within the default timeout.

**Wire messages:**

```
-> {"ch":0,"type":"configure","observe_level":1}
<- {"ch":0,"type":"configured","observe_level":1}
```

**Example:**

```python
resp = await client.control.configure(observe_level=1)
print(resp)
```

---

#### `disconnect`

```python
async def disconnect(self) -> None:
```

Send a graceful disconnect request.

The server may close the connection before responding, so response errors
are silently ignored.

**Wire messages:**

```
-> {"ch":0,"type":"disconnect"}
<- {"ch":0,"type":"bye"}  (may not arrive)
```

**Example:**

```python
await client.control.disconnect()
```

---

### CommandChannel

```python
class CommandChannel:
    """Channel 1: command evaluation."""
```

Evaluates shell commands and collects captured stdout, stderr, and the
exit code. This is channel 1 (`CHAN_COMMAND`).

#### `eval`

```python
async def eval(
    self,
    command: str,
    id: Optional[str] = None,
    timeout: float = 30.0,
) -> EvalResult:
```

Evaluate a shell command in the server's Bash environment.

The server forks a child process for each command, captures stdout and
stderr via pipes, and sends them back as separate messages followed by a
completion message with the exit code.

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `command` | `str` | -- | Shell command to evaluate. May be multi-line. |
| `id` | `Optional[str]` | `None` | Optional request identifier for correlating responses. |
| `timeout` | `float` | `30.0` | Maximum seconds to wait for all three response messages (stdout, stderr, complete). |

**Returns:** `EvalResult` -- The captured stdout, stderr, and exit code.

**Raises:**
- `ServerError` -- If the server returns an error message (channel = `CHAN_COMMAND`).
- `TimeoutError` -- If the command does not complete within `timeout` seconds.

**Wire messages:**

```
-> {"ch":1,"type":"eval","command":"echo hello"}
<- {"ch":1,"type":"stdout","data":"aGVsbG8K","encoding":"base64"}
<- {"ch":1,"type":"stderr","data":"","encoding":"base64"}
<- {"ch":1,"type":"complete","exit_code":0}
```

The server always sends exactly three response messages for a successful
eval: `stdout`, `stderr`, and `complete`, in that order. The `data` fields
use base64 encoding.

**Example:**

```python
result = await client.command.eval("echo hello")
assert result.stdout == "hello\n"
assert result.stderr == ""
assert result.exit_code == 0
```

```python
# With request ID for correlation
result = await client.command.eval("date +%s", id="req-001")
```

```python
# Multi-line command
result = await client.command.eval("""
for i in 1 2 3; do
    echo "item $i"
done
""")
```

---

### StateChannel

```python
class StateChannel:
    """Channel 2: variable, function, alias, and trap management."""
```

Manages the server's shell state: variables, functions, aliases, and traps.
Each operation sends a request and waits for a typed response. This is
channel 2 (`CHAN_STATE`).

All methods use a common internal `_request()` helper that checks for error
responses.

#### `get_var`

```python
async def get_var(self, name: str) -> VarInfo:
```

Get information about a shell variable.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `str` | Variable name (e.g., `"PATH"`, `"HOME"`, `"PS1"`). |

**Returns:** `VarInfo` -- The variable's name, value, and attributes.

**Raises:**
- `ServerError` -- If the variable does not exist or the server returns an error.

**Wire messages:**

```
-> {"ch":2,"type":"get","target":"var","name":"HOME"}
<- {"ch":2,"type":"value","name":"HOME","value":"/home/user","attributes":["export"]}
```

**Example:**

```python
var = await client.state.get_var("PATH")
print(f"PATH={var.value}")
print(f"Attributes: {var.attributes}")
```

---

#### `set_var`

```python
async def set_var(
    self,
    name: str,
    value: str,
    attributes: Optional[List[str]] = None,
) -> None:
```

Set a shell variable.

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `name` | `str` | -- | Variable name. |
| `value` | `str` | -- | Value to assign. |
| `attributes` | `Optional[List[str]]` | `None` | Optional list of attributes to set (e.g., `["export"]`, `["readonly"]`, `["integer"]`). |

**Raises:**
- `ServerError` -- If the variable is readonly or the server rejects the operation.

**Wire messages:**

```
-> {"ch":2,"type":"set","target":"var","name":"FOO","value":"bar","attributes":["export"]}
<- {"ch":2,"type":"ok"}
```

**Example:**

```python
await client.state.set_var("MY_VAR", "hello world")
await client.state.set_var("MY_INT", "42", attributes=["integer", "export"])
```

---

#### `unset_var`

```python
async def unset_var(self, name: str) -> None:
```

Unset (remove) a shell variable.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `str` | Variable name to unset. |

**Raises:**
- `ServerError` -- If the variable is readonly or the server rejects the operation.

**Wire messages:**

```
-> {"ch":2,"type":"unset","target":"var","name":"FOO"}
<- {"ch":2,"type":"ok"}
```

**Example:**

```python
await client.state.unset_var("TEMP_VAR")
```

---

#### `get_func`

```python
async def get_func(self, name: str) -> FuncInfo:
```

Get the definition of a shell function.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `str` | Function name. |

**Returns:** `FuncInfo` -- The function's name and definition body.

**Raises:**
- `ServerError` -- If the function does not exist.

**Wire messages:**

```
-> {"ch":2,"type":"get","target":"function","name":"my_func"}
<- {"ch":2,"type":"value","name":"my_func","value":"{ echo hello; }"}
```

**Example:**

```python
func = await client.state.get_func("my_func")
print(func.definition)
```

---

#### `unset_func`

```python
async def unset_func(self, name: str) -> None:
```

Remove a shell function.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `str` | Function name to remove. |

**Raises:**
- `ServerError` -- If the server rejects the operation.

**Wire messages:**

```
-> {"ch":2,"type":"unset","target":"function","name":"my_func"}
<- {"ch":2,"type":"ok"}
```

**Example:**

```python
await client.state.unset_func("old_function")
```

---

#### `get_alias`

```python
async def get_alias(self, name: str) -> AliasInfo:
```

Get the value of a shell alias.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `str` | Alias name (e.g., `"ll"`, `"la"`). |

**Returns:** `AliasInfo` -- The alias name and its expansion text.

**Raises:**
- `ServerError` -- If the alias does not exist.

**Wire messages:**

```
-> {"ch":2,"type":"get","target":"alias","name":"ll"}
<- {"ch":2,"type":"value","name":"ll","value":"ls -la"}
```

**Example:**

```python
alias = await client.state.get_alias("ll")
print(f"alias {alias.name}='{alias.value}'")
```

---

#### `set_alias`

```python
async def set_alias(self, name: str, value: str) -> None:
```

Create or update a shell alias.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `str` | Alias name. |
| `value` | `str` | Alias expansion text. |

**Raises:**
- `ServerError` -- If the server rejects the operation.

**Wire messages:**

```
-> {"ch":2,"type":"set","target":"alias","name":"ll","value":"ls -la --color"}
<- {"ch":2,"type":"ok"}
```

**Example:**

```python
await client.state.set_alias("ll", "ls -la --color=auto")
```

---

#### `unset_alias`

```python
async def unset_alias(self, name: str) -> None:
```

Remove a shell alias.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `str` | Alias name to remove. |

**Raises:**
- `ServerError` -- If the server rejects the operation.

**Wire messages:**

```
-> {"ch":2,"type":"unset","target":"alias","name":"ll"}
<- {"ch":2,"type":"ok"}
```

**Example:**

```python
await client.state.unset_alias("ll")
```

---

#### `set_trap`

```python
async def set_trap(self, signal: str, command: str) -> None:
```

Set a signal trap.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `signal` | `str` | Signal name (e.g., `"INT"`, `"EXIT"`, `"ERR"`, `"DEBUG"`, `"RETURN"`). |
| `command` | `str` | Command string to execute when the signal is received. |

**Raises:**
- `ServerError` -- If the signal name is invalid or the server rejects the operation.

**Wire messages:**

```
-> {"ch":2,"type":"set","target":"trap","name":"EXIT","value":"cleanup"}
<- {"ch":2,"type":"ok"}
```

**Example:**

```python
await client.state.set_trap("EXIT", "echo 'Goodbye!'")
await client.state.set_trap("INT", "echo 'Interrupted'; exit 130")
```

---

#### `unset_trap`

```python
async def unset_trap(self, signal: str) -> None:
```

Remove a signal trap, restoring the default behavior.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `signal` | `str` | Signal name to clear. |

**Raises:**
- `ServerError` -- If the server rejects the operation.

**Wire messages:**

```
-> {"ch":2,"type":"unset","target":"trap","name":"EXIT"}
<- {"ch":2,"type":"ok"}
```

**Example:**

```python
await client.state.unset_trap("EXIT")
```

---

#### `inspect`

```python
async def inspect(self, query: str) -> List[Dict[str, Any]]:
```

Inspect shell state with a query string.

Returns a list of matching items across all namespaces (variables,
functions, aliases, traps) based on the query. The query format and
matching rules are server-defined.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `query` | `str` | Inspection query (e.g., `"*"` for all, `"export"` for exported variables, a glob pattern). |

**Returns:** `List[Dict[str, Any]]` -- List of matching items. Each dict contains at minimum `"name"` and `"type"` fields; additional fields vary by item type.

**Raises:**
- `ServerError` -- If the query is invalid or the server returns an error.

**Wire messages:**

```
-> {"ch":2,"type":"inspect","query":"*"}
<- {"ch":2,"type":"inspect_result","data":[{"name":"HOME","type":"var",...}, ...]}
```

**Example:**

```python
# List all exported variables
items = await client.state.inspect("export")
for item in items:
    print(f"{item['name']} = {item.get('value', '')}")
```

```python
# List everything
all_items = await client.state.inspect("*")
print(f"Total items: {len(all_items)}")
```

---

### ObserveChannel

```python
class ObserveChannel:
    """Channel 3: command observation events."""
```

Subscribes to real-time command execution events. When observation is active,
the server pushes `pre_command` and `post_command` events for every command
executed in the session. This is channel 3 (`CHAN_OBSERVE`).

Events are delivered to callbacks registered with `on()`. The background
reader loop in `BashClient` dispatches incoming events via the `dispatch()`
method.

#### `subscribe`

```python
async def subscribe(self, level: int = 1) -> None:
```

Start receiving observation events.

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `level` | `int` | `1` | Observation level. Use `OBSERVE_LEVEL_COMMAND` (1) for command events, `OBSERVE_LEVEL_OFF` (0) to disable. |

**Raises:**
- `ServerError` -- If the server rejects the subscription.

**Wire messages:**

```
-> {"ch":3,"type":"subscribe","level":1}
<- {"ch":3,"type":"subscribed"}
```

**Example:**

```python
await client.observe.subscribe(level=OBSERVE_LEVEL_COMMAND)
```

---

#### `unsubscribe`

```python
async def unsubscribe(self) -> None:
```

Stop receiving observation events.

**Raises:**
- `ServerError` -- If the server returns an error.

**Wire messages:**

```
-> {"ch":3,"type":"unsubscribe"}
<- {"ch":3,"type":"unsubscribed"}
```

**Example:**

```python
await client.observe.unsubscribe()
```

---

#### `on`

```python
def on(self, event: str, callback: Callable) -> None:
```

Register a callback for an observation event type.

Multiple callbacks can be registered for the same event. Callbacks are
invoked in registration order. Both synchronous and async callbacks are
supported.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `event` | `str` | Event type: `"pre_command"` or `"post_command"`. |
| `callback` | `Callable` | Function to call when the event occurs. Receives the event dataclass as its argument. |

**Callback signatures:**

```python
# Synchronous callback
def on_pre(event: PreCommandEvent) -> None: ...
def on_post(event: PostCommandEvent) -> None: ...

# Async callback
async def on_pre(event: PreCommandEvent) -> None: ...
async def on_post(event: PostCommandEvent) -> None: ...
```

**Example:**

```python
def log_command(event: PreCommandEvent):
    print(f"[{event.timestamp}] Running: {event.command}")

client.observe.on("pre_command", log_command)

async def log_result(event: PostCommandEvent):
    print(f"[{event.timestamp}] Exit: {event.exit_status} ({event.duration_ms}ms)")

client.observe.on("post_command", log_result)
```

---

#### `off`

```python
def off(self, event: str, callback: Optional[Callable] = None) -> None:
```

Remove observation event callback(s).

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `event` | `str` | -- | Event type to unregister from. |
| `callback` | `Optional[Callable]` | `None` | Specific callback to remove. If `None`, removes **all** callbacks for this event type. |

**Example:**

```python
# Remove a specific callback
client.observe.off("pre_command", log_command)

# Remove all callbacks for an event type
client.observe.off("post_command")
```

---

#### `dispatch`

```python
async def dispatch(self, msg: Dict[str, Any]) -> None:
```

Dispatch a server-push message to registered callbacks.

This method is called internally by the `BashClient` reader loop. Users
do not normally call this directly.

For `pre_command` messages, the raw dict is converted to a
`PreCommandEvent` dataclass before invoking callbacks. For `post_command`
messages, a `PostCommandEvent` is created.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `Dict[str, Any]` | Raw NDJSON message from the server. |

---

### DebugChannel

```python
class DebugChannel:
    """Channel 4: debugging (breakpoints, stepping, AST)."""
```

Provides interactive debugging capabilities: breakpoints (command, line,
function), execution control (step, next, finish, skip, continue), and AST
inspection. This is channel 4 (`CHAN_DEBUG`).

Breakpoint hit events are delivered to callbacks registered with `on()`.

#### `enable`

```python
async def enable(self) -> None:
```

Enable the debugger.

Must be called before setting breakpoints or using stepping commands. The
debugger intercepts command execution and checks breakpoint conditions.

**Raises:**
- `ServerError` -- If the server cannot enable debugging.

**Wire messages:**

```
-> {"ch":4,"type":"enable"}
<- {"ch":4,"type":"ok"}
```

**Example:**

```python
await client.debug.enable()
```

---

#### `disable`

```python
async def disable(self) -> None:
```

Disable the debugger.

All breakpoints are preserved but no longer checked. Execution proceeds
at full speed.

**Raises:**
- `ServerError` -- If the server returns an error.

**Wire messages:**

```
-> {"ch":4,"type":"disable"}
<- {"ch":4,"type":"ok"}
```

**Example:**

```python
await client.debug.disable()
```

---

#### `status`

```python
async def status(self) -> DebugStatus:
```

Get the current debugger status.

**Returns:** `DebugStatus` -- Active state, execution mode, breakpoint count, and call depth.

**Raises:**
- `ServerError` -- If the server returns an error.

**Wire messages:**

```
-> {"ch":4,"type":"status"}
<- {"ch":4,"type":"status","active":true,"mode":"run","breakpoints":2,"depth":0}
```

**Example:**

```python
status = await client.debug.status()
print(f"Active: {status.active}, Mode: {status.mode}")
print(f"Breakpoints: {status.breakpoints}, Depth: {status.depth}")
```

---

#### `add_breakpoint`

```python
async def add_breakpoint(
    self,
    kind: str,
    pattern: Optional[str] = None,
    line: Optional[int] = None,
    condition: Optional[str] = None,
) -> int:
```

Add a new breakpoint.

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `kind` | `str` | -- | Breakpoint type: `"command"`, `"line"`, or `"function"`. |
| `pattern` | `Optional[str]` | `None` | Glob pattern for `"command"` or `"function"` breakpoints (e.g., `"ls*"`, `"my_func"`). |
| `line` | `Optional[int]` | `None` | Line number for `"line"` breakpoints. |
| `condition` | `Optional[str]` | `None` | Bash expression used as a conditional guard. The breakpoint fires only when this expression exits with code 0. |

**Returns:** `int` -- Server-assigned breakpoint ID. Use this ID with `remove_breakpoint()`, `enable_breakpoint()`, and `disable_breakpoint()`.

**Raises:**
- `ServerError` -- If the breakpoint parameters are invalid.

**Wire messages:**

```
-> {"ch":4,"type":"break","kind":"command","pattern":"rm*","condition":"[ -t 0 ]"}
<- {"ch":4,"type":"break_set","id":1}
```

**Example:**

```python
# Break on any "rm" command
bp_id = await client.debug.add_breakpoint("command", pattern="rm*")

# Break at line 42
bp_id = await client.debug.add_breakpoint("line", line=42)

# Break when function "deploy" is called, but only if $ENV == "prod"
bp_id = await client.debug.add_breakpoint(
    "function",
    pattern="deploy",
    condition='[ "$ENV" = "prod" ]',
)
```

---

#### `remove_breakpoint`

```python
async def remove_breakpoint(self, bp_id: int) -> bool:
```

Remove a breakpoint by ID.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `bp_id` | `int` | Breakpoint ID returned by `add_breakpoint()`. |

**Returns:** `bool` -- `True` if the breakpoint was found and removed; `False` if no breakpoint with that ID exists.

**Raises:**
- `ServerError` -- If the server returns an error.

**Wire messages:**

```
-> {"ch":4,"type":"delete","id":1}
<- {"ch":4,"type":"ok","found":true}
```

**Example:**

```python
removed = await client.debug.remove_breakpoint(bp_id)
if not removed:
    print(f"Breakpoint {bp_id} not found")
```

---

#### `enable_breakpoint`

```python
async def enable_breakpoint(self, bp_id: int) -> bool:
```

Enable a previously disabled breakpoint.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `bp_id` | `int` | Breakpoint ID. |

**Returns:** `bool` -- `True` if the breakpoint was found.

**Raises:**
- `ServerError` -- If the server returns an error.

**Wire messages:**

```
-> {"ch":4,"type":"enable_bp","id":1}
<- {"ch":4,"type":"ok","found":true}
```

**Example:**

```python
await client.debug.enable_breakpoint(bp_id)
```

---

#### `disable_breakpoint`

```python
async def disable_breakpoint(self, bp_id: int) -> bool:
```

Disable a breakpoint without removing it.

A disabled breakpoint is preserved (retains its ID and hit count) but
does not trigger during execution. Re-enable it with `enable_breakpoint()`.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `bp_id` | `int` | Breakpoint ID. |

**Returns:** `bool` -- `True` if the breakpoint was found.

**Raises:**
- `ServerError` -- If the server returns an error.

**Wire messages:**

```
-> {"ch":4,"type":"disable_bp","id":1}
<- {"ch":4,"type":"ok","found":true}
```

**Example:**

```python
await client.debug.disable_breakpoint(bp_id)
```

---

#### `list_breakpoints`

```python
async def list_breakpoints(self) -> List[Breakpoint]:
```

List all registered breakpoints.

**Returns:** `List[Breakpoint]` -- All breakpoints (enabled and disabled).

**Raises:**
- `ServerError` -- If the server returns an error.

**Wire messages:**

```
-> {"ch":4,"type":"list"}
<- {"ch":4,"type":"list","data":[{"id":1,"type":"command","enabled":true,...}]}
```

**Example:**

```python
breakpoints = await client.debug.list_breakpoints()
for bp in breakpoints:
    print(f"  #{bp.id} {bp.kind} enabled={bp.enabled} hits={bp.hit_count}")
    if bp.pattern:
        print(f"    pattern: {bp.pattern}")
    if bp.line is not None:
        print(f"    line: {bp.line}")
    if bp.condition:
        print(f"    condition: {bp.condition}")
```

---

#### `continue_`

```python
async def continue_(self) -> None:
```

Resume execution until the next breakpoint or completion.

Note the trailing underscore to avoid shadowing the Python `continue`
keyword.

**Raises:**
- `ServerError` -- If the debugger is not active or returns an error.

**Wire messages:**

```
-> {"ch":4,"type":"continue"}
<- {"ch":4,"type":"ok"}
```

**Example:**

```python
await client.debug.continue_()
```

---

#### `step`

```python
async def step(self) -> None:
```

Execute one command, stepping into function calls.

After the step completes, the debugger pauses and a `break_hit` event
is emitted for the next command.

**Raises:**
- `ServerError` -- If the debugger is not active.

**Wire messages:**

```
-> {"ch":4,"type":"step"}
<- {"ch":4,"type":"ok"}
```

**Example:**

```python
await client.debug.step()
```

---

#### `next`

```python
async def next(self) -> None:
```

Execute one command, stepping over function calls.

Like `step`, but function calls execute to completion without pausing
inside them.

**Raises:**
- `ServerError` -- If the debugger is not active.

**Wire messages:**

```
-> {"ch":4,"type":"next"}
<- {"ch":4,"type":"ok"}
```

**Example:**

```python
await client.debug.next()
```

---

#### `finish`

```python
async def finish(self) -> None:
```

Run until the current function returns.

Execution continues without pausing until the current function scope exits.

**Raises:**
- `ServerError` -- If the debugger is not active or not inside a function.

**Wire messages:**

```
-> {"ch":4,"type":"finish"}
<- {"ch":4,"type":"ok"}
```

**Example:**

```python
await client.debug.finish()
```

---

#### `skip`

```python
async def skip(self) -> None:
```

Skip the current command without executing it.

The command at the current breakpoint is not executed; execution advances
to the next command.

**Raises:**
- `ServerError` -- If the debugger is not active.

**Wire messages:**

```
-> {"ch":4,"type":"skip"}
<- {"ch":4,"type":"ok"}
```

**Example:**

```python
# Skip a dangerous command at a breakpoint
await client.debug.skip()
```

---

#### `inspect_ast`

```python
async def inspect_ast(self) -> Dict[str, Any]:
```

Get the AST (abstract syntax tree) of the current command.

Returns a JSON representation of the Bash COMMAND tree at the current
execution point. Useful for programmatic analysis of what is about to
execute. See `cmd_serialize.c` for the serialization format covering all
10 command types.

**Returns:** `Dict[str, Any]` -- JSON representation of the COMMAND tree. Structure varies by command type (`cm_simple`, `cm_connection`, `cm_for`, `cm_if`, etc.).

**Raises:**
- `ServerError` -- If the debugger is not active or no command is current.

**Wire messages:**

```
-> {"ch":4,"type":"inspect_ast"}
<- {"ch":4,"type":"ast","data":{"type":"cm_simple","words":["echo","hello"],...}}
```

**Example:**

```python
ast = await client.debug.inspect_ast()
print(f"Command type: {ast.get('type')}")
if ast.get("type") == "cm_simple":
    print(f"Words: {ast.get('words')}")
```

---

#### `on`

```python
def on(self, event: str, callback: Callable) -> None:
```

Register a callback for a debug event.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `event` | `str` | Event type: `"break_hit"`. |
| `callback` | `Callable` | Function to call when the event occurs. Receives a `BreakHitEvent` as its argument. Both sync and async callbacks are supported. |

**Example:**

```python
async def on_break(event: BreakHitEvent):
    print(f"Break at line {event.line}: {event.command} (depth={event.depth})")
    await client.debug.continue_()

client.debug.on("break_hit", on_break)
```

---

#### `off`

```python
def off(self, event: str, callback: Optional[Callable] = None) -> None:
```

Remove debug event callback(s).

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `event` | `str` | -- | Event type to unregister from. |
| `callback` | `Optional[Callable]` | `None` | Specific callback to remove. If `None`, removes all callbacks for this event. |

**Example:**

```python
client.debug.off("break_hit", on_break)
client.debug.off("break_hit")  # remove all
```

---

#### `dispatch`

```python
async def dispatch(self, msg: Dict[str, Any]) -> None:
```

Dispatch a server-push debug event to registered callbacks.

Called internally by the `BashClient` reader loop. For `break_hit`
messages, the raw dict is converted to a `BreakHitEvent` before invoking
callbacks.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `Dict[str, Any]` | Raw NDJSON message from the server. |

---

### PtyChannel

```python
class PtyChannel:
    """Channel 5: pseudo-terminal I/O."""
```

Manages a pseudo-terminal (PTY) session on the server. Supports spawning a
PTY, sending input, receiving output, resizing the terminal, sending signals,
and closing the session. This is channel 5 (`CHAN_PTY`).

Output and exit events are delivered to callbacks registered with `on()`.

#### `spawn`

```python
async def spawn(
    self,
    rows: int = 24,
    cols: int = 80,
    shell: Optional[str] = None,
    strip_ansi: Optional[bool] = None,
) -> PtyInfo:
```

Spawn a new PTY session on the server.

The server calls `forkpty()` to create a pseudo-terminal and starts a shell
process inside it. Output from the PTY is relayed to the client as
`"output"` events.

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `rows` | `int` | `24` | Terminal height in rows. |
| `cols` | `int` | `80` | Terminal width in columns. |
| `shell` | `Optional[str]` | `None` | Shell program to run (e.g., `"/bin/bash"`). If `None`, the server uses its default. |
| `strip_ansi` | `Optional[bool]` | `None` | If `True`, the server strips ANSI escape sequences from output before sending. If `None`, the server uses its default. |

**Returns:** `PtyInfo` -- Information about the spawned PTY (rows, cols, pid, strip_ansi).

**Raises:**
- `ServerError` -- If the PTY cannot be spawned.

**Wire messages:**

```
-> {"ch":5,"type":"spawn","rows":24,"cols":80,"strip_ansi":true}
<- {"ch":5,"type":"spawned","rows":24,"cols":80,"pid":12345,"strip_ansi":true}
```

**Example:**

```python
info = await client.pty.spawn(rows=40, cols=120)
print(f"PTY started, pid={info.pid}")
```

```python
# Spawn with ANSI stripping for clean text output
info = await client.pty.spawn(strip_ansi=True)
```

---

#### `write_input`

```python
async def write_input(self, data: str) -> None:
```

Send input to the PTY.

Data is base64-encoded before transmission. The server decodes it and
writes it to the PTY master file descriptor, making it appear as keyboard
input to the shell.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `data` | `str` | Input string to send to the PTY. May include control characters (e.g., `"\r"` for Enter, `"\x03"` for Ctrl-C). |

**Wire messages:**

```
-> {"ch":5,"type":"input","data":"bHMgLWxhCg==","encoding":"base64"}
```

Note: `write_input` is fire-and-forget; no response is expected.

**Example:**

```python
await client.pty.write_input("ls -la\r")
await client.pty.write_input("exit\r")
```

```python
# Send Ctrl-C
await client.pty.write_input("\x03")
```

---

#### `resize`

```python
async def resize(self, rows: int, cols: int) -> None:
```

Resize the PTY terminal dimensions.

Sends a `SIGWINCH` to the PTY child process to notify it of the new size.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `rows` | `int` | New terminal height. |
| `cols` | `int` | New terminal width. |

**Raises:**
- `ServerError` -- If the resize fails.

**Wire messages:**

```
-> {"ch":5,"type":"resize","rows":50,"cols":200}
<- {"ch":5,"type":"resized"}
```

**Example:**

```python
await client.pty.resize(50, 200)
```

---

#### `signal`

```python
async def signal(self, name: str) -> None:
```

Send a signal to the PTY child process.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `name` | `str` | Signal name (e.g., `"INT"`, `"TERM"`, `"HUP"`, `"KILL"`). |

**Raises:**
- `ServerError` -- If the signal cannot be delivered.

**Wire messages:**

```
-> {"ch":5,"type":"signal","signal":"INT"}
<- {"ch":5,"type":"ok"}
```

**Example:**

```python
await client.pty.signal("INT")   # Ctrl-C
await client.pty.signal("TERM")  # graceful termination
```

---

#### `close`

```python
async def close(self) -> None:
```

Close the PTY session.

The server terminates the PTY child process and cleans up resources. An
`"exit"` event is emitted with the child's exit code.

**Wire messages:**

```
-> {"ch":5,"type":"close"}
```

Note: `close` is fire-and-forget. The server sends an `"exit"` event
asynchronously, delivered to registered callbacks.

**Example:**

```python
await client.pty.close()
```

---

#### `on`

```python
def on(self, event: str, callback: Callable) -> None:
```

Register a callback for PTY events.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `event` | `str` | Event type: `"output"` or `"exit"`. |
| `callback` | `Callable` | Function to call when the event occurs. Both sync and async callbacks are supported. |

**Callback signatures:**

| Event | Argument | Description |
|-------|----------|-------------|
| `"output"` | `str` | Decoded PTY output text. Base64-decoded by the dispatch method. |
| `"exit"` | `int` | Exit code of the PTY child process. |

**Example:**

```python
def on_output(data: str):
    print(data, end="")

def on_exit(code: int):
    print(f"\nPTY exited with code {code}")

client.pty.on("output", on_output)
client.pty.on("exit", on_exit)
```

```python
# Async callback
async def on_output(data: str):
    await log_to_file(data)

client.pty.on("output", on_output)
```

---

#### `off`

```python
def off(self, event: str, callback: Optional[Callable] = None) -> None:
```

Remove PTY event callback(s).

**Parameters:**

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `event` | `str` | -- | Event type to unregister from. |
| `callback` | `Optional[Callable]` | `None` | Specific callback to remove. If `None`, removes all callbacks for this event. |

**Example:**

```python
client.pty.off("output", on_output)
client.pty.off("output")  # remove all output callbacks
```

---

#### `dispatch`

```python
async def dispatch(self, msg: Dict[str, Any]) -> None:
```

Dispatch a server-push PTY event to registered callbacks.

Called internally by the `BashClient` reader loop.

- For `"output"` messages: base64-decodes the `"data"` field and passes
  the decoded string to `"output"` callbacks.
- For `"exit"` messages: extracts the `"exit_code"` field and passes it
  to `"exit"` callbacks.
- For other message types: passes the raw message dict to callbacks
  registered for that type.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `Dict[str, Any]` | Raw NDJSON message from the server. |

---

## Protocol Functions

Low-level functions for NDJSON frame encoding, decoding, and message
construction. Defined in `bashclient.protocol`. Most users do not need
these directly; the channel classes handle framing internally.

### `encode_frame`

```python
def encode_frame(msg: Dict[str, Any]) -> bytes:
```

Encode a message dictionary as an NDJSON line.

Serializes the dict as compact JSON (no spaces) with a trailing newline.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `Dict[str, Any]` | Message dictionary to encode. Must be JSON-serializable. |

**Returns:** `bytes` -- UTF-8 encoded JSON followed by `b"\n"`.

**Raises:**
- `ProtocolError` -- If the message cannot be serialized to JSON.

**Example:**

```python
from bashclient.protocol import encode_frame

frame = encode_frame({"ch": 0, "type": "ping"})
# b'{"ch":0,"type":"ping"}\n'
```

---

### `decode_frame`

```python
def decode_frame(line: bytes) -> Dict[str, Any]:
```

Decode an NDJSON line into a message dictionary.

Strips whitespace, validates length against `FRAME_MAX_PAYLOAD`, and
parses JSON.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `line` | `bytes` | Raw bytes of one NDJSON line (may include trailing newline). |

**Returns:** `Dict[str, Any]` -- Parsed message dictionary.

**Raises:**
- `ProtocolError` -- If the line is empty, exceeds the maximum payload size, is not valid JSON, or is not a JSON object.

**Example:**

```python
from bashclient.protocol import decode_frame

msg = decode_frame(b'{"ch":0,"type":"pong"}\n')
# {"ch": 0, "type": "pong"}
```

---

### `b64encode`

```python
def b64encode(data: str) -> str:
```

Base64-encode a UTF-8 string.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `data` | `str` | String to encode. |

**Returns:** `str` -- Base64-encoded ASCII string.

**Example:**

```python
from bashclient.protocol import b64encode

encoded = b64encode("hello world")
# "aGVsbG8gd29ybGQ="
```

---

### `b64decode`

```python
def b64decode(data: str) -> str:
```

Base64-decode a string to UTF-8.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `data` | `str` | Base64-encoded ASCII string. |

**Returns:** `str` -- Decoded UTF-8 string.

**Raises:**
- `ProtocolError` -- If the input is not valid base64 or the decoded bytes are not valid UTF-8.

**Example:**

```python
from bashclient.protocol import b64decode

text = b64decode("aGVsbG8gd29ybGQ=")
# "hello world"
```

---

### `make_msg`

```python
def make_msg(channel: int, msg_type: str, **kwargs: Any) -> Dict[str, Any]:
```

Build a protocol message dictionary.

Creates a dict with `"ch"` and `"type"` fields, plus any additional
keyword arguments.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `channel` | `int` | Channel ID (`CHAN_CONTROL` through `CHAN_PTY`). |
| `msg_type` | `str` | Message type string (e.g., `"auth"`, `"eval"`, `"get"`). |
| `**kwargs` | `Any` | Additional fields to include in the message. |

**Returns:** `Dict[str, Any]` -- Message dictionary ready for `encode_frame()`.

**Example:**

```python
from bashclient.protocol import make_msg
from bashclient.types import CHAN_COMMAND

msg = make_msg(CHAN_COMMAND, "eval", command="echo hello")
# {"ch": 1, "type": "eval", "command": "echo hello"}
```

```python
msg = make_msg(CHAN_STATE, "set", target="var", name="FOO", value="bar")
# {"ch": 2, "type": "set", "target": "var", "name": "FOO", "value": "bar"}
```

---

### `get_channel`

```python
def get_channel(msg: Dict[str, Any]) -> int:
```

Extract the channel ID from a message.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `Dict[str, Any]` | Message dictionary. |

**Returns:** `int` -- Channel ID. Defaults to `0` (`CHAN_CONTROL`) if the `"ch"` field is missing.

**Example:**

```python
from bashclient.protocol import get_channel

ch = get_channel({"ch": 2, "type": "value", "name": "HOME"})
# 2
```

---

### `get_type`

```python
def get_type(msg: Dict[str, Any]) -> str:
```

Extract the message type from a message.

**Parameters:**

| Parameter | Type | Description |
|-----------|------|-------------|
| `msg` | `Dict[str, Any]` | Message dictionary. |

**Returns:** `str` -- Message type string.

**Raises:**
- `ProtocolError` -- If the message has no `"type"` field.

**Example:**

```python
from bashclient.protocol import get_type

t = get_type({"ch": 0, "type": "pong"})
# "pong"
```

---

## See Also

- [GUIDE.md](GUIDE.md) -- Tutorial and usage patterns
- [TROUBLESHOOTING.md](TROUBLESHOOTING.md) -- Debugging connection and protocol issues
- `bash-server-channels(7)` -- Server-side v2 channel architecture
- `bash-server-ndjson(5)` -- NDJSON wire format specification
- `bash-server(1)` -- Server command-line reference
- `bashclient(1)` -- CLI client reference
