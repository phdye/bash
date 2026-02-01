# API Reference

## bashclient TypeScript/Node.js API

Complete API reference for the `bashclient` package. Every constant, interface,
error class, transport, channel, and method is documented here.

---

## Table of Contents

1. [Constants](#constants)
2. [Interfaces](#interfaces)
3. [Error Classes](#error-classes)
4. [Transport Interface](#transport-interface)
5. [Transport Implementations](#transport-implementations)
6. [BashClient](#bashclient)
7. [ControlChannel](#controlchannel)
8. [CommandChannel](#commandchannel)
9. [StateChannel](#statechannel)
10. [ObserveChannel](#observechannel)
11. [DebugChannel](#debugchannel)
12. [PtyChannel](#ptychannel)

---

## Constants

All constants are exported from the package root.

```typescript
import {
  CHAN_CONTROL,
  CHAN_COMMAND,
  CHAN_STATE,
  CHAN_OBSERVE,
  CHAN_DEBUG,
  CHAN_PTY,
  FRAME_MAX_PAYLOAD,
  TOKEN_HEXLEN,
  OBSERVE_LEVEL_OFF,
  OBSERVE_LEVEL_COMMAND,
} from "bashclient";
```

### Channel IDs

| Constant       | Value | Description                              |
|----------------|-------|------------------------------------------|
| `CHAN_CONTROL`  | 0     | Control channel (auth, ping, configure)  |
| `CHAN_COMMAND`  | 1     | Command channel (eval, output)           |
| `CHAN_STATE`    | 2     | State channel (vars, funcs, aliases)     |
| `CHAN_OBSERVE`  | 3     | Observe channel (command events)         |
| `CHAN_DEBUG`    | 4     | Debug channel (breakpoints, stepping)    |
| `CHAN_PTY`      | 5     | PTY channel (terminal I/O)              |

### Protocol Constants

| Constant            | Value       | Description                         |
|---------------------|-------------|-------------------------------------|
| `FRAME_MAX_PAYLOAD` | 10485760    | Maximum message payload (10 MiB)    |
| `TOKEN_HEXLEN`      | 64          | Authentication token length (hex)   |

### Observe Levels

| Constant               | Value | Description                          |
|------------------------|-------|--------------------------------------|
| `OBSERVE_LEVEL_OFF`    | 0     | Observation disabled                 |
| `OBSERVE_LEVEL_COMMAND`| 1     | Observe command execution events     |

---

## Interfaces

All interfaces are exported from the package root.

```typescript
import {
  Message,
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
  InspectItem,
} from "bashclient";
```

### Message

Base message type for all protocol communication.

```typescript
interface Message {
  ch: number;
  type: string;
  [key: string]: unknown;
}
```

| Field  | Type    | Description                          |
|--------|---------|--------------------------------------|
| `ch`   | number  | Channel number (0-5)                 |
| `type` | string  | Message type identifier              |
| `...`  | unknown | Additional message-specific fields   |

### EvalResult

Result of command evaluation.

```typescript
interface EvalResult {
  stdout: string;
  stderr: string;
  exit_code: number;
}
```

| Field       | Type   | Description                              |
|-------------|--------|------------------------------------------|
| `stdout`    | string | Standard output from the command         |
| `stderr`    | string | Standard error from the command          |
| `exit_code` | number | Exit status (0 = success)                |

### VarInfo

Shell variable information.

```typescript
interface VarInfo {
  name: string;
  value: string;
  attributes: string[];
}
```

| Field        | Type     | Description                                       |
|-------------|----------|---------------------------------------------------|
| `name`       | string   | Variable name                                     |
| `value`      | string   | Variable value                                    |
| `attributes` | string[] | Attributes: exported, readonly, integer, array, associative, nameref, lowercase, uppercase, trace |

### FuncInfo

Shell function information.

```typescript
interface FuncInfo {
  name: string;
  definition: string;
}
```

| Field        | Type   | Description                          |
|-------------|--------|--------------------------------------|
| `name`       | string | Function name                        |
| `definition` | string | Full function definition text        |

### AliasInfo

Shell alias information.

```typescript
interface AliasInfo {
  name: string;
  value: string;
}
```

| Field  | Type   | Description                          |
|--------|--------|--------------------------------------|
| `name`  | string | Alias name                           |
| `value` | string | Alias expansion value                |

### TrapInfo

Shell trap information.

```typescript
interface TrapInfo {
  signal: string;
  command: string;
}
```

| Field    | Type   | Description                              |
|----------|--------|------------------------------------------|
| `signal`  | string | Signal name (EXIT, INT, TERM, etc.)     |
| `command` | string | Command executed when signal is received |

### PreCommandEvent

Event emitted before a command executes.

```typescript
interface PreCommandEvent {
  seq: number;
  timestamp: number;
  command: string;
  cwd: string;
  line_number: number;
  is_subshell: boolean;
  is_async: boolean;
}
```

| Field         | Type    | Description                              |
|---------------|---------|------------------------------------------|
| `seq`          | number  | Sequence number (monotonically increasing) |
| `timestamp`    | number  | Unix timestamp (seconds, fractional)     |
| `command`      | string  | Command text about to execute            |
| `cwd`          | string  | Current working directory                |
| `line_number`  | number  | Line number in the script/input          |
| `is_subshell`  | boolean | True if running in a subshell            |
| `is_async`     | boolean | True if running as a background job      |

### PostCommandEvent

Event emitted after a command completes.

```typescript
interface PostCommandEvent {
  seq: number;
  timestamp: number;
  command: string;
  exit_status: number;
  signal_number: number;
  duration_ms: number;
}
```

| Field           | Type   | Description                              |
|-----------------|--------|------------------------------------------|
| `seq`            | number | Sequence number (matches pre_command)    |
| `timestamp`      | number | Unix timestamp (seconds, fractional)     |
| `command`        | string | Command text that was executed           |
| `exit_status`    | number | Exit code of the command                 |
| `signal_number`  | number | Signal that killed the command (0 = none)|
| `duration_ms`    | number | Execution duration in milliseconds       |

### Breakpoint

Debug breakpoint definition.

```typescript
interface Breakpoint {
  id: number;
  kind: string;
  enabled: boolean;
  hit_count: number;
  pattern?: string;
  line?: number;
  condition?: string;
}
```

| Field       | Type    | Description                                    |
|-------------|---------|------------------------------------------------|
| `id`         | number  | Unique breakpoint identifier                  |
| `kind`       | string  | Breakpoint type: "command", "line", "function" |
| `enabled`    | boolean | Whether the breakpoint is active               |
| `hit_count`  | number  | Number of times the breakpoint has been hit    |
| `pattern`    | string? | Glob pattern (command/function breakpoints)    |
| `line`       | number? | Line number (line breakpoints)                 |
| `condition`  | string? | Bash expression that must be true to break     |

### BreakHitEvent

Event emitted when a breakpoint is hit.

```typescript
interface BreakHitEvent {
  line: number;
  command: string;
  depth: number;
}
```

| Field    | Type   | Description                                  |
|----------|--------|----------------------------------------------|
| `line`    | number | Line number where execution is paused       |
| `command` | string | Command text at the breakpoint               |
| `depth`   | number | Call stack depth                             |

### DebugStatus

Current debug session status.

```typescript
interface DebugStatus {
  active: boolean;
  mode: string;
  breakpoints: Breakpoint[];
  depth: number;
}
```

| Field         | Type         | Description                              |
|---------------|-------------|------------------------------------------|
| `active`       | boolean      | Whether debugging is active              |
| `mode`         | string       | Current mode: "running", "paused", etc.  |
| `breakpoints`  | Breakpoint[] | All registered breakpoints               |
| `depth`        | number       | Current call stack depth                 |

### PtyInfo

PTY session information.

```typescript
interface PtyInfo {
  rows: number;
  cols: number;
  pid: number;
  strip_ansi: boolean;
}
```

| Field        | Type    | Description                                |
|-------------|---------|---------------------------------------------|
| `rows`       | number  | Terminal row count                          |
| `cols`       | number  | Terminal column count                       |
| `pid`        | number  | Process ID of the PTY child                 |
| `strip_ansi` | boolean | Whether ANSI escape sequences are stripped  |

### InspectItem

Item returned by state inspection.

```typescript
interface InspectItem {
  name: string;
  value?: string;
  definition?: string;
  attributes?: string[];
  signal?: string;
  command?: string;
}
```

| Field        | Type     | Description                                    |
|-------------|----------|------------------------------------------------|
| `name`       | string   | Item name                                      |
| `value`      | string?  | Value (variables, aliases)                     |
| `definition` | string?  | Definition text (functions)                    |
| `attributes` | string[]?| Variable attributes                            |
| `signal`     | string?  | Signal name (traps)                            |
| `command`    | string?  | Trap command (traps)                           |

---

## Error Classes

All error classes extend `Error` and are exported from the package root.

```typescript
import {
  AuthError,
  ProtocolError,
  TimeoutError,
  TransportError,
  ServerError,
} from "bashclient";
```

### AuthError

Authentication failure.

```typescript
class AuthError extends Error {
  name: "AuthError";
  constructor(message: string);
}
```

Thrown when:
- Token is invalid or missing.
- Server rejects the auth handshake.

Example:

```typescript
try {
  await BashClient.connect({ token: "bad-token" });
} catch (e) {
  if (e instanceof AuthError) {
    console.error("Bad token:", e.message);
  }
}
```

### ProtocolError

Protocol violation or malformed message.

```typescript
class ProtocolError extends Error {
  name: "ProtocolError";
  constructor(message: string);
}
```

Thrown when:
- Incoming JSON is malformed (parse error).
- Message missing required `ch` or `type` fields.
- Unexpected message type received.
- Message exceeds `FRAME_MAX_PAYLOAD`.

Example:

```typescript
try {
  const result = await client.command.eval("echo test");
} catch (e) {
  if (e instanceof ProtocolError) {
    console.error("Protocol error:", e.message);
  }
}
```

### TimeoutError

Operation exceeded its timeout.

```typescript
class TimeoutError extends Error {
  name: "TimeoutError";
  constructor(message: string);
}
```

Thrown when:
- A command evaluation exceeds the specified timeout.
- A connection attempt exceeds the connection timeout.

Example:

```typescript
try {
  await client.command.eval("sleep 300", { timeout: 5000 });
} catch (e) {
  if (e instanceof TimeoutError) {
    console.error("Timed out:", e.message);
  }
}
```

### TransportError

Transport-level failure.

```typescript
class TransportError extends Error {
  name: "TransportError";
  constructor(message: string);
}
```

Thrown when:
- Socket connection refused (`ECONNREFUSED`).
- Socket path not found (`ENOENT`).
- Connection closed unexpectedly.
- Write to closed transport.
- Pipe broken.

Example:

```typescript
try {
  await BashClient.connect({ socketPath: "/nonexistent" });
} catch (e) {
  if (e instanceof TransportError) {
    console.error("Cannot connect:", e.message);
  }
}
```

### ServerError

Server returned an error response.

```typescript
class ServerError extends Error {
  name: "ServerError";
  constructor(message: string);
}
```

Thrown when:
- Server sends `{ type: "error", error: "..." }` response.
- Operation is not supported.
- Requested resource not found.

Example:

```typescript
try {
  await client.state.getVar("NONEXISTENT");
} catch (e) {
  if (e instanceof ServerError) {
    console.error("Server says:", e.message);
  }
}
```

---

## Transport Interface

The `Transport` interface defines the contract for all transport implementations.

```typescript
interface Transport {
  write(data: Buffer): void;
  on(event: "data", handler: (chunk: Buffer) => void): void;
  on(event: "close", handler: () => void): void;
  on(event: "error", handler: (err: Error) => void): void;
  close(): Promise<void>;
}
```

### Methods

#### write(data)

Write data to the transport.

| Parameter | Type   | Description               |
|-----------|--------|---------------------------|
| `data`    | Buffer | Bytes to write            |

**Returns**: `void`

**Throws**: `TransportError` if the transport is closed or write fails.

#### on(event, handler)

Register an event handler.

| Parameter | Type     | Description               |
|-----------|----------|---------------------------|
| `event`   | string   | Event name                |
| `handler` | Function | Callback function         |

Events:

| Event   | Handler Signature           | Description                    |
|---------|-------------------------------|--------------------------------|
| `data`  | `(chunk: Buffer) => void`    | Incoming data received         |
| `close` | `() => void`                 | Transport connection closed    |
| `error` | `(err: Error) => void`       | Transport error occurred       |

**Returns**: `void`

#### close()

Close the transport and release resources.

**Returns**: `Promise<void>`

---

## Transport Implementations

### SocketTransport

Unix domain socket transport.

```typescript
class SocketTransport implements Transport {
  constructor(socketPath: string);
  async connect(): Promise<void>;
  write(data: Buffer): void;
  on(event: string, handler: Function): void;
  async close(): Promise<void>;
}
```

#### constructor(socketPath)

| Parameter    | Type   | Description                     |
|-------------|--------|---------------------------------|
| `socketPath` | string | Path to the Unix domain socket  |

#### connect()

Establish the socket connection.

**Returns**: `Promise<void>`

**Throws**: `TransportError` on connection failure.

### StdioTransport

Stdio transport (spawns bash-server as child process).

```typescript
class StdioTransport implements Transport {
  constructor(serverPath: string, args?: string[], env?: NodeJS.ProcessEnv);
  async connect(): Promise<void>;
  write(data: Buffer): void;
  on(event: string, handler: Function): void;
  async close(): Promise<void>;
}
```

#### constructor(serverPath, args?, env?)

| Parameter    | Type              | Default          | Description                    |
|-------------|-------------------|------------------|--------------------------------|
| `serverPath` | string            | "bash-server"    | Path to bash-server binary     |
| `args`       | string[]          | []               | Additional server arguments    |
| `env`        | NodeJS.ProcessEnv | process.env      | Environment for child process  |

#### connect()

Spawn the bash-server process and establish stdio communication.

**Returns**: `Promise<void>`

**Throws**: `TransportError` if spawn fails.

#### close()

Send SIGTERM to the child process, wait for exit, then SIGKILL if needed.

**Returns**: `Promise<void>`

### FdTransport

File descriptor transport.

```typescript
class FdTransport implements Transport {
  constructor(readFd: number, writeFd?: number);
  async connect(): Promise<void>;
  write(data: Buffer): void;
  on(event: string, handler: Function): void;
  async close(): Promise<void>;
}
```

#### constructor(readFd, writeFd?)

| Parameter | Type   | Default  | Description                       |
|-----------|--------|----------|-----------------------------------|
| `readFd`  | number | required | File descriptor for reading       |
| `writeFd` | number | readFd   | File descriptor for writing       |

### NamedPipeTransport

Windows Named Pipe transport (Cygwin only).

```typescript
class NamedPipeTransport implements Transport {
  constructor(pipePath: string);
  async connect(): Promise<void>;
  write(data: Buffer): void;
  on(event: string, handler: Function): void;
  async close(): Promise<void>;
}
```

#### constructor(pipePath)

| Parameter  | Type   | Description                                  |
|-----------|--------|----------------------------------------------|
| `pipePath` | string | Full Named Pipe path (e.g., `\\.\pipe\...`) |

---

## BashClient

The main client class. Provides static factory methods for creating connections
and instance methods for communication.

```typescript
import { BashClient } from "bashclient";
```

### Static Methods

#### BashClient.connect(options?)

Connect to bash-server via Unix socket.

```typescript
static async connect(options?: ConnectOptions): Promise<BashClient>;
```

**Parameters**:

| Option       | Type   | Default          | Description                       |
|-------------|--------|------------------|-----------------------------------|
| `socketPath` | string | auto-discovered  | Unix socket path                  |
| `name`       | string | -                | Server name for path resolution   |
| `token`      | string | auto-discovered  | Authentication token              |
| `timeout`    | number | 30000            | Connection timeout (ms)           |

**Returns**: `Promise<BashClient>` &mdash; Connected and authenticated client.

**Throws**:
- `TransportError` &mdash; Connection failed.
- `AuthError` &mdash; Authentication failed.
- `TimeoutError` &mdash; Connection timed out.

**Example**:

```typescript
const client = await BashClient.connect();
const client2 = await BashClient.connect({ name: "myserver" });
const client3 = await BashClient.connect({
  socketPath: "/tmp/bash-server-1000/sock",
  token: "abc123...",
  timeout: 5000,
});
```

#### BashClient.connectStdio(options?)

Connect via stdio transport (spawns bash-server).

```typescript
static async connectStdio(options?: StdioOptions): Promise<BashClient>;
```

**Parameters**:

| Option       | Type              | Default        | Description                     |
|-------------|-------------------|----------------|---------------------------------|
| `serverPath` | string            | "bash-server"  | Path to bash-server binary      |
| `args`       | string[]          | []             | Additional server arguments     |
| `env`        | NodeJS.ProcessEnv | process.env    | Environment for server process  |
| `timeout`    | number            | 30000          | Connection timeout (ms)         |

**Returns**: `Promise<BashClient>` &mdash; Connected client with owned server process.

**Throws**:
- `TransportError` &mdash; Failed to spawn server.
- `TimeoutError` &mdash; Server did not respond in time.

**Example**:

```typescript
const client = await BashClient.connectStdio();
const client2 = await BashClient.connectStdio({
  serverPath: "/usr/local/bin/bash-server",
  args: ["--login", "--init", "/path/to/init.sh"],
});
```

#### BashClient.connectFd(options)

Connect via pre-opened file descriptors.

```typescript
static async connectFd(options: FdOptions): Promise<BashClient>;
```

**Parameters**:

| Option    | Type   | Default  | Description                         |
|-----------|--------|----------|-------------------------------------|
| `fd`      | number | -        | File descriptor (read + write)      |
| `readFd`  | number | -        | Read file descriptor                |
| `writeFd` | number | -        | Write file descriptor               |
| `token`   | string | -        | Authentication token                |
| `timeout` | number | 30000    | Connection timeout (ms)             |

Either `fd` or both `readFd` and `writeFd` must be provided.

**Returns**: `Promise<BashClient>` &mdash; Connected client.

**Throws**:
- `TransportError` &mdash; Invalid file descriptor.
- `AuthError` &mdash; Authentication failed.

**Example**:

```typescript
const client = await BashClient.connectFd({ fd: 3 });
const client2 = await BashClient.connectFd({ readFd: 3, writeFd: 4 });
```

#### BashClient.connectNamedPipe(options)

Connect via Windows Named Pipe (Cygwin only).

```typescript
static async connectNamedPipe(options: NamedPipeOptions): Promise<BashClient>;
```

**Parameters**:

| Option     | Type   | Default  | Description                          |
|-----------|--------|----------|--------------------------------------|
| `pipeName` | string | -        | Named Pipe name                      |
| `pipePath` | string | -        | Full Named Pipe path                 |
| `token`    | string | -        | Authentication token                 |
| `timeout`  | number | 30000    | Connection timeout (ms)              |

Either `pipeName` or `pipePath` must be provided.

**Returns**: `Promise<BashClient>` &mdash; Connected client.

**Throws**:
- `TransportError` &mdash; Named Pipe not found or connection failed.
- `AuthError` &mdash; Authentication failed.

**Example**:

```typescript
const client = await BashClient.connectNamedPipe({ pipeName: "myapp" });
```

### Instance Properties

#### client.control

```typescript
readonly control: ControlChannel;
```

Access to the control channel (ch=0). See [ControlChannel](#controlchannel).

#### client.command

```typescript
readonly command: CommandChannel;
```

Access to the command channel (ch=1). See [CommandChannel](#commandchannel).

#### client.state

```typescript
readonly state: StateChannel;
```

Access to the state channel (ch=2). See [StateChannel](#statechannel).

#### client.observe

```typescript
readonly observe: ObserveChannel;
```

Access to the observe channel (ch=3). See [ObserveChannel](#observechannel).

#### client.debug

```typescript
readonly debug: DebugChannel;
```

Access to the debug channel (ch=4). See [DebugChannel](#debugchannel).

#### client.pty

```typescript
readonly pty: PtyChannel;
```

Access to the PTY channel (ch=5). See [PtyChannel](#ptychannel).

### Instance Methods

#### client.close()

Close the connection and release all resources.

```typescript
async close(): Promise<void>;
```

**Returns**: `Promise<void>`

Sends a disconnect message, drains pending operations (rejecting them with
`TransportError`), and closes the transport. Safe to call multiple times.

**Example**:

```typescript
const client = await BashClient.connect();
try {
  // ... use client ...
} finally {
  await client.close();
}
```

#### client.send(message, timeout?)

Send a raw message and wait for the response. This is a low-level method;
prefer using channel methods instead.

```typescript
async send(message: Message, timeout?: number): Promise<Message>;
```

**Parameters**:

| Parameter | Type    | Default   | Description                     |
|-----------|---------|----------|---------------------------------|
| `message` | Message | required | Message to send                 |
| `timeout` | number  | -        | Timeout in milliseconds         |

**Returns**: `Promise<Message>` &mdash; The server's response message.

**Throws**:
- `TransportError` &mdash; Client is closed or connection lost.
- `TimeoutError` &mdash; No response within timeout.
- `ProtocolError` &mdash; Invalid response.
- `ServerError` &mdash; Server returned error.

---

## ControlChannel

Channel 0: Connection lifecycle and configuration.

Access via `client.control`.

### control.ping()

Ping the server and measure round-trip latency.

```typescript
async ping(): Promise<number>;
```

**Returns**: `Promise<number>` &mdash; Round-trip time in milliseconds.

**Throws**: `TransportError`, `TimeoutError`

**Example**:

```typescript
const latency = await client.control.ping();
console.log(`${latency}ms`);
```

### control.disconnect()

Gracefully disconnect from the server.

```typescript
async disconnect(): Promise<void>;
```

**Returns**: `Promise<void>`

**Note**: After calling disconnect, the client is closed. Use `client.close()`
for the standard cleanup pattern.

### control.configure(options)

Set session-level configuration.

```typescript
async configure(options: Record<string, unknown>): Promise<void>;
```

**Parameters**:

| Parameter | Type                    | Description              |
|-----------|-------------------------|--------------------------|
| `options` | Record<string, unknown> | Configuration key-values |

Known options:

| Key              | Type   | Description                              |
|-----------------|--------|------------------------------------------|
| `observe_level` | string | Observation level: "off", "command"      |
| `wire_format`   | string | Wire format: "ndjson", "binary"          |

**Returns**: `Promise<void>`

**Throws**: `ServerError` if an option is invalid.

**Example**:

```typescript
await client.control.configure({ observe_level: "command" });
```

### control.info()

Query server information.

```typescript
async info(): Promise<Record<string, unknown>>;
```

**Returns**: `Promise<Record<string, unknown>>` &mdash; Server information including
version, protocol, PID, uptime.

**Example**:

```typescript
const info = await client.control.info();
console.log("Version:", info.version);
console.log("PID:", info.pid);
```

---

## CommandChannel

Channel 1: Command execution.

Access via `client.command`.

### command.eval(command, options?)

Execute a bash command and return the result.

```typescript
async eval(command: string, options?: EvalOptions): Promise<EvalResult>;
```

**Parameters**:

| Parameter  | Type        | Description                     |
|-----------|-------------|---------------------------------|
| `command`  | string      | Bash command to execute         |
| `options`  | EvalOptions | Optional evaluation settings    |

`EvalOptions`:

| Option    | Type   | Default | Description                     |
|-----------|--------|--------|---------------------------------|
| `timeout` | number | -      | Command timeout in milliseconds |

**Returns**: `Promise<EvalResult>` &mdash; Object with `stdout`, `stderr`, `exit_code`.

**Throws**:
- `TimeoutError` &mdash; Command exceeded timeout.
- `TransportError` &mdash; Connection lost during execution.
- `ServerError` &mdash; Server-side error.

**Example**:

```typescript
const result = await client.command.eval("echo hello");
console.log(result.stdout);     // "hello\n"
console.log(result.stderr);     // ""
console.log(result.exit_code);  // 0

// With timeout
const result2 = await client.command.eval("find / -name '*.log'", {
  timeout: 10000,
});
```

### command.evalParsed(commandTree, options?)

Execute a pre-parsed COMMAND tree (JSON representation).

```typescript
async evalParsed(commandTree: unknown, options?: EvalOptions): Promise<EvalResult>;
```

**Parameters**:

| Parameter     | Type        | Description                        |
|--------------|-------------|------------------------------------|
| `commandTree` | unknown     | JSON COMMAND tree (see protocol docs) |
| `options`     | EvalOptions | Optional evaluation settings       |

**Returns**: `Promise<EvalResult>`

**Throws**: Same as `eval()`.

**Example**:

```typescript
const result = await client.command.evalParsed({
  type: "simple",
  words: ["echo", "hello"],
  redirects: [],
});
console.log(result.stdout);  // "hello\n"
```

---

## StateChannel

Channel 2: Shell state manipulation.

Access via `client.state`.

### Variables

#### state.getVar(name)

Get a shell variable's value and attributes.

```typescript
async getVar(name: string): Promise<VarInfo>;
```

**Parameters**:

| Parameter | Type   | Description        |
|-----------|--------|--------------------|
| `name`    | string | Variable name      |

**Returns**: `Promise<VarInfo>` &mdash; Object with `name`, `value`, `attributes`.

**Throws**:
- `ServerError` &mdash; Variable not found.

**Example**:

```typescript
const info = await client.state.getVar("HOME");
console.log(info.value);       // "/home/user"
console.log(info.attributes);  // ["exported"]
```

#### state.setVar(name, value, options?)

Set a shell variable.

```typescript
async setVar(name: string, value: string, options?: SetVarOptions): Promise<void>;
```

**Parameters**:

| Parameter | Type           | Description              |
|-----------|----------------|--------------------------|
| `name`    | string         | Variable name            |
| `value`   | string         | Variable value           |
| `options` | SetVarOptions  | Optional settings        |

`SetVarOptions`:

| Option       | Type     | Default | Description                        |
|-------------|----------|---------|------------------------------------|
| `attributes` | string[] | []      | Attributes to set on the variable  |

**Returns**: `Promise<void>`

**Throws**:
- `ServerError` &mdash; Variable is readonly or invalid name.

**Example**:

```typescript
await client.state.setVar("MY_VAR", "hello");
await client.state.setVar("MY_INT", "42", { attributes: ["integer"] });
await client.state.setVar("PATH_EXTRA", "/opt/bin", { attributes: ["exported"] });
```

#### state.unsetVar(name)

Unset (remove) a shell variable.

```typescript
async unsetVar(name: string): Promise<void>;
```

**Parameters**:

| Parameter | Type   | Description        |
|-----------|--------|--------------------|
| `name`    | string | Variable name      |

**Returns**: `Promise<void>`

**Throws**:
- `ServerError` &mdash; Variable is readonly.

**Example**:

```typescript
await client.state.unsetVar("MY_VAR");
```

### Functions

#### state.getFunc(name)

Get a shell function's definition.

```typescript
async getFunc(name: string): Promise<FuncInfo>;
```

**Parameters**:

| Parameter | Type   | Description        |
|-----------|--------|--------------------|
| `name`    | string | Function name      |

**Returns**: `Promise<FuncInfo>` &mdash; Object with `name`, `definition`.

**Throws**:
- `ServerError` &mdash; Function not found.

**Example**:

```typescript
const func = await client.state.getFunc("greet");
console.log(func.definition);  // "greet () { echo \"Hello, $1!\" ; }"
```

#### state.setFunc(name, definition)

Define or replace a shell function.

```typescript
async setFunc(name: string, definition: string): Promise<void>;
```

**Parameters**:

| Parameter    | Type   | Description                    |
|-------------|--------|--------------------------------|
| `name`       | string | Function name                  |
| `definition` | string | Full function definition text  |

**Returns**: `Promise<void>`

**Example**:

```typescript
await client.state.setFunc("add", "add() { echo $(($1 + $2)); }");
```

#### state.unsetFunc(name)

Remove a shell function.

```typescript
async unsetFunc(name: string): Promise<void>;
```

**Parameters**:

| Parameter | Type   | Description        |
|-----------|--------|--------------------|
| `name`    | string | Function name      |

**Returns**: `Promise<void>`

**Example**:

```typescript
await client.state.unsetFunc("add");
```

### Aliases

#### state.getAlias(name)

Get an alias definition.

```typescript
async getAlias(name: string): Promise<AliasInfo>;
```

**Parameters**:

| Parameter | Type   | Description    |
|-----------|--------|----------------|
| `name`    | string | Alias name     |

**Returns**: `Promise<AliasInfo>` &mdash; Object with `name`, `value`.

**Throws**:
- `ServerError` &mdash; Alias not found.

**Example**:

```typescript
const alias = await client.state.getAlias("ll");
console.log(alias.value);  // "ls -la"
```

#### state.setAlias(name, value)

Define or replace an alias.

```typescript
async setAlias(name: string, value: string): Promise<void>;
```

**Parameters**:

| Parameter | Type   | Description           |
|-----------|--------|-----------------------|
| `name`    | string | Alias name            |
| `value`   | string | Alias expansion value |

**Returns**: `Promise<void>`

**Example**:

```typescript
await client.state.setAlias("ll", "ls -la --color=auto");
```

#### state.unsetAlias(name)

Remove an alias.

```typescript
async unsetAlias(name: string): Promise<void>;
```

**Parameters**:

| Parameter | Type   | Description    |
|-----------|--------|----------------|
| `name`    | string | Alias name     |

**Returns**: `Promise<void>`

**Example**:

```typescript
await client.state.unsetAlias("ll");
```

### Traps

#### state.getTrap(signal)

Get a trap handler.

```typescript
async getTrap(signal: string): Promise<TrapInfo>;
```

**Parameters**:

| Parameter | Type   | Description                          |
|-----------|--------|--------------------------------------|
| `signal`  | string | Signal name (EXIT, INT, TERM, etc.) |

**Returns**: `Promise<TrapInfo>` &mdash; Object with `signal`, `command`.

**Throws**:
- `ServerError` &mdash; No trap set for this signal.

**Example**:

```typescript
const trap = await client.state.getTrap("EXIT");
console.log(trap.command);  // "cleanup_function"
```

#### state.setTrap(signal, command)

Set a trap handler.

```typescript
async setTrap(signal: string, command: string): Promise<void>;
```

**Parameters**:

| Parameter | Type   | Description                          |
|-----------|--------|--------------------------------------|
| `signal`  | string | Signal name (EXIT, INT, TERM, etc.) |
| `command` | string | Command to execute on signal         |

**Returns**: `Promise<void>`

**Example**:

```typescript
await client.state.setTrap("EXIT", "echo 'Goodbye'");
```

#### state.unsetTrap(signal)

Remove a trap handler.

```typescript
async unsetTrap(signal: string): Promise<void>;
```

**Parameters**:

| Parameter | Type   | Description    |
|-----------|--------|----------------|
| `signal`  | string | Signal name    |

**Returns**: `Promise<void>`

**Example**:

```typescript
await client.state.unsetTrap("EXIT");
```

### Inspection

#### state.inspect(namespace)

List all items in a state namespace.

```typescript
async inspect(namespace: string): Promise<InspectItem[]>;
```

**Parameters**:

| Parameter   | Type   | Description                                         |
|------------|--------|-----------------------------------------------------|
| `namespace` | string | One of: "variables", "functions", "aliases", "traps" |

**Returns**: `Promise<InspectItem[]>` &mdash; Array of items in the namespace.

**Example**:

```typescript
const vars = await client.state.inspect("variables");
for (const v of vars) {
  console.log(`${v.name}=${v.value}`);
}

const funcs = await client.state.inspect("functions");
const aliases = await client.state.inspect("aliases");
const traps = await client.state.inspect("traps");
```

---

## ObserveChannel

Channel 3: Command execution event subscription.

Access via `client.observe`.

### observe.setLevel(level)

Set the observation level.

```typescript
async setLevel(level: number): Promise<void>;
```

**Parameters**:

| Parameter | Type   | Description                                    |
|-----------|--------|------------------------------------------------|
| `level`   | number | `OBSERVE_LEVEL_OFF` (0) or `OBSERVE_LEVEL_COMMAND` (1) |

**Returns**: `Promise<void>`

**Example**:

```typescript
import { OBSERVE_LEVEL_COMMAND, OBSERVE_LEVEL_OFF } from "bashclient";

await client.observe.setLevel(OBSERVE_LEVEL_COMMAND);
// ... commands execute, events fire ...
await client.observe.setLevel(OBSERVE_LEVEL_OFF);
```

### observe.on(event, handler)

Register an event handler. Callbacks are synchronous.

```typescript
on(event: "pre_command", handler: (e: PreCommandEvent) => void): this;
on(event: "post_command", handler: (e: PostCommandEvent) => void): this;
```

**Parameters**:

| Parameter | Type     | Description                          |
|-----------|----------|--------------------------------------|
| `event`   | string   | Event name: "pre_command" or "post_command" |
| `handler` | Function | Synchronous callback                 |

**Returns**: `this` (for chaining)

**Example**:

```typescript
client.observe.on("pre_command", (e) => {
  console.log(`Running: ${e.command} in ${e.cwd}`);
});

client.observe.on("post_command", (e) => {
  console.log(`Done: ${e.command} (exit ${e.exit_status}, ${e.duration_ms}ms)`);
});
```

### observe.off(event, handler?)

Remove an event handler.

```typescript
off(event: string, handler?: Function): this;
```

**Parameters**:

| Parameter | Type     | Description                              |
|-----------|----------|------------------------------------------|
| `event`   | string   | Event name                               |
| `handler` | Function | Specific handler to remove (optional)    |

If `handler` is omitted, all handlers for the event are removed.

**Returns**: `this` (for chaining)

**Example**:

```typescript
// Remove specific handler
client.observe.off("pre_command", myHandler);

// Remove all handlers for an event
client.observe.off("pre_command");
```

---

## DebugChannel

Channel 4: Debugging and breakpoints.

Access via `client.debug`.

### debug.addBreakpoint(options)

Add a new breakpoint.

```typescript
async addBreakpoint(options: BreakpointOptions): Promise<Breakpoint>;
```

**Parameters** (`BreakpointOptions`):

| Field       | Type    | Required | Description                                 |
|------------|---------|----------|---------------------------------------------|
| `kind`      | string  | yes      | "command", "line", or "function"            |
| `pattern`   | string  | *        | Glob pattern (required for command/function) |
| `line`      | number  | *        | Line number (required for line breakpoints)  |
| `condition` | string  | no       | Bash expression condition                    |

**Returns**: `Promise<Breakpoint>` &mdash; The created breakpoint with its assigned ID.

**Throws**: `ServerError` if the breakpoint specification is invalid.

**Example**:

```typescript
const bp = await client.debug.addBreakpoint({
  kind: "command",
  pattern: "rm *",
});
console.log("Created breakpoint:", bp.id);

const bp2 = await client.debug.addBreakpoint({
  kind: "line",
  line: 10,
});

const bp3 = await client.debug.addBreakpoint({
  kind: "function",
  pattern: "deploy_*",
  condition: "$DEBUG == 1",
});
```

### debug.removeBreakpoint(id)

Remove a breakpoint by ID.

```typescript
async removeBreakpoint(id: number): Promise<void>;
```

**Parameters**:

| Parameter | Type   | Description             |
|-----------|--------|-------------------------|
| `id`      | number | Breakpoint ID to remove |

**Returns**: `Promise<void>`

**Throws**: `ServerError` if breakpoint not found.

**Example**:

```typescript
await client.debug.removeBreakpoint(bp.id);
```

### debug.removeAllBreakpoints()

Remove all breakpoints.

```typescript
async removeAllBreakpoints(): Promise<void>;
```

**Returns**: `Promise<void>`

**Example**:

```typescript
await client.debug.removeAllBreakpoints();
```

### debug.enableBreakpoint(id, enabled)

Enable or disable a breakpoint.

```typescript
async enableBreakpoint(id: number, enabled: boolean): Promise<void>;
```

**Parameters**:

| Parameter | Type    | Description                  |
|-----------|---------|------------------------------|
| `id`      | number  | Breakpoint ID                |
| `enabled` | boolean | true to enable, false to disable |

**Returns**: `Promise<void>`

**Example**:

```typescript
await client.debug.enableBreakpoint(bp.id, false);  // Disable
await client.debug.enableBreakpoint(bp.id, true);   // Re-enable
```

### debug.listBreakpoints()

List all breakpoints.

```typescript
async listBreakpoints(): Promise<Breakpoint[]>;
```

**Returns**: `Promise<Breakpoint[]>`

**Example**:

```typescript
const breakpoints = await client.debug.listBreakpoints();
for (const bp of breakpoints) {
  console.log(`#${bp.id} ${bp.kind} ${bp.pattern ?? bp.line} [${bp.enabled ? "on" : "off"}]`);
}
```

### debug.continue()

Continue execution after a breakpoint hit.

```typescript
async continue(): Promise<void>;
```

**Returns**: `Promise<void>`

### debug.step()

Step to the next command (step into functions).

```typescript
async step(): Promise<void>;
```

**Returns**: `Promise<void>`

### debug.next()

Step over (execute functions without stopping inside).

```typescript
async next(): Promise<void>;
```

**Returns**: `Promise<void>`

### debug.finish()

Step out (finish current function, stop at caller).

```typescript
async finish(): Promise<void>;
```

**Returns**: `Promise<void>`

### debug.skip()

Skip the current command (do not execute, move to next).

```typescript
async skip(): Promise<void>;
```

**Returns**: `Promise<void>`

### debug.inspectAst()

Inspect the AST of the current command at a breakpoint.

```typescript
async inspectAst(): Promise<unknown>;
```

**Returns**: `Promise<unknown>` &mdash; JSON representation of the COMMAND AST.

**Example**:

```typescript
const ast = await client.debug.inspectAst();
console.log(JSON.stringify(ast, null, 2));
```

### debug.status()

Query the current debug session status.

```typescript
async status(): Promise<DebugStatus>;
```

**Returns**: `Promise<DebugStatus>` &mdash; Object with `active`, `mode`, `breakpoints`, `depth`.

**Example**:

```typescript
const status = await client.debug.status();
console.log("Active:", status.active);
console.log("Mode:", status.mode);
console.log("Breakpoints:", status.breakpoints.length);
```

### debug.on(event, handler)

Register a breakpoint hit event handler.

```typescript
on(event: "break_hit", handler: (e: BreakHitEvent) => void): this;
```

**Parameters**:

| Parameter | Type     | Description               |
|-----------|----------|---------------------------|
| `event`   | string   | "break_hit"               |
| `handler` | Function | Synchronous callback      |

**Returns**: `this`

**Example**:

```typescript
client.debug.on("break_hit", (e) => {
  console.log(`Break at line ${e.line}: ${e.command}`);
});
```

### debug.off(event, handler?)

Remove a breakpoint hit event handler.

```typescript
off(event: string, handler?: Function): this;
```

**Returns**: `this`

---

## PtyChannel

Channel 5: Pseudo-terminal management.

Access via `client.pty`.

### pty.spawn(options?)

Spawn a new PTY session.

```typescript
async spawn(options?: PtySpawnOptions): Promise<PtyInfo>;
```

**Parameters** (`PtySpawnOptions`):

| Field        | Type    | Default | Description                              |
|-------------|---------|---------|------------------------------------------|
| `rows`       | number  | 24      | Terminal rows                            |
| `cols`       | number  | 80      | Terminal columns                         |
| `strip_ansi` | boolean | false   | Strip ANSI escape sequences from output  |

**Returns**: `Promise<PtyInfo>` &mdash; Object with `rows`, `cols`, `pid`, `strip_ansi`.

**Throws**: `ServerError` if PTY creation fails.

**Example**:

```typescript
const info = await client.pty.spawn({ rows: 24, cols: 80 });
console.log("PID:", info.pid);

const info2 = await client.pty.spawn({
  rows: 40,
  cols: 120,
  strip_ansi: true,
});
```

### pty.writeInput(data)

Write data to the PTY's stdin.

```typescript
async writeInput(data: string): Promise<void>;
```

**Parameters**:

| Parameter | Type   | Description                     |
|-----------|--------|---------------------------------|
| `data`    | string | Data to write (may include escape sequences) |

**Returns**: `Promise<void>`

**Example**:

```typescript
await client.pty.writeInput("ls -la\n");
await client.pty.writeInput("\x03");  // Ctrl-C
await client.pty.writeInput("\t");    // Tab completion
```

### pty.resize(rows, cols)

Resize the PTY terminal.

```typescript
async resize(rows: number, cols: number): Promise<void>;
```

**Parameters**:

| Parameter | Type   | Description        |
|-----------|--------|--------------------|
| `rows`    | number | New row count      |
| `cols`    | number | New column count   |

**Returns**: `Promise<void>`

**Example**:

```typescript
await client.pty.resize(40, 120);
```

### pty.signal(sig)

Send a signal to the PTY's process group.

```typescript
async signal(sig: string): Promise<void>;
```

**Parameters**:

| Parameter | Type   | Description                              |
|-----------|--------|------------------------------------------|
| `sig`     | string | Signal name: "INT", "TERM", "KILL", "TSTP", "CONT" |

**Returns**: `Promise<void>`

**Example**:

```typescript
await client.pty.signal("INT");   // Ctrl-C
await client.pty.signal("TERM");  // Terminate
await client.pty.signal("KILL");  // Force kill
```

### pty.close()

Close the PTY session.

```typescript
async close(): Promise<void>;
```

**Returns**: `Promise<void>`

**Example**:

```typescript
await client.pty.close();
```

### pty.on(event, handler)

Register PTY event handlers.

```typescript
on(event: "output", handler: (data: string) => void): this;
on(event: "close", handler: () => void): this;
```

**Parameters**:

| Parameter | Type     | Description                              |
|-----------|----------|------------------------------------------|
| `event`   | string   | "output" (PTY output) or "close" (PTY closed) |
| `handler` | Function | Synchronous callback                     |

**Returns**: `this`

**Example**:

```typescript
client.pty.on("output", (data) => {
  process.stdout.write(data);
});

client.pty.on("close", () => {
  console.log("PTY session ended");
});
```

### pty.off(event, handler?)

Remove PTY event handlers.

```typescript
off(event: string, handler?: Function): this;
```

**Returns**: `this`

---

## Protocol Functions

These are internal functions used by `BashClient`. Not typically called
directly by consumers, but exported for advanced use cases.

### encodeMessage(message)

Serialize a message to NDJSON format.

```typescript
function encodeMessage(message: Message): Buffer;
```

**Parameters**:

| Parameter | Type    | Description            |
|-----------|---------|------------------------|
| `message` | Message | Message to serialize   |

**Returns**: `Buffer` &mdash; UTF-8 encoded JSON with trailing newline.

### decodeMessage(line)

Deserialize an NDJSON line to a message.

```typescript
function decodeMessage(line: string): Message;
```

**Parameters**:

| Parameter | Type   | Description                   |
|-----------|--------|-------------------------------|
| `line`    | string | JSON string (without newline) |

**Returns**: `Message`

**Throws**: `ProtocolError` if JSON is malformed or missing required fields.

### FrameReader

Accumulates partial data and yields complete NDJSON lines.

```typescript
class FrameReader {
  constructor();
  feed(chunk: Buffer): string[];
  reset(): void;
}
```

#### feed(chunk)

Feed a chunk of data and return any complete lines.

| Parameter | Type   | Description        |
|-----------|--------|--------------------|
| `chunk`   | Buffer | Incoming data      |

**Returns**: `string[]` &mdash; Array of complete JSON lines (may be empty).

#### reset()

Clear the internal buffer.

**Returns**: `void`
