# Architecture

## bashclient TypeScript/Node.js Internal Design

This document describes the internal architecture of the `bashclient` package:
module structure, data flow, concurrency model, transport abstraction, protocol
handling, and testing strategy.

---

## Table of Contents

1. [Module Structure](#module-structure)
2. [Dependency Graph](#dependency-graph)
3. [Data Flow](#data-flow)
4. [BashClient Internals](#bashclient-internals)
5. [Transport Layer](#transport-layer)
6. [Protocol Layer](#protocol-layer)
7. [Channel Layer](#channel-layer)
8. [Error Handling Architecture](#error-handling-architecture)
9. [Event System](#event-system)
10. [Testing Architecture](#testing-architecture)

---

## Module Structure

```
src/
├── index.ts              # Public API barrel export
├── client.ts             # BashClient class - primary entry point
├── types.ts              # All TypeScript interfaces and constants
├── protocol.ts           # NDJSON frame encoding/decoding
├── transport.ts          # Transport interface + 4 implementations
├── errors.ts             # Error class hierarchy
└── channels/
    ├── control.ts        # ControlChannel (ch=0)
    ├── command.ts        # CommandChannel (ch=1)
    ├── state.ts          # StateChannel (ch=2)
    ├── observe.ts        # ObserveChannel (ch=3)
    ├── debug.ts          # DebugChannel (ch=4)
    └── pty.ts            # PtyChannel (ch=5)
```

### Module Responsibilities

| Module           | Responsibility                                          | Key Exports                        |
|------------------|---------------------------------------------------------|------------------------------------|
| `index.ts`       | Barrel re-export of public API                          | Everything consumer-visible        |
| `client.ts`      | Connection lifecycle, message routing, reader loop       | `BashClient`                       |
| `types.ts`       | Data type definitions, channel constants                 | All interfaces, `CHAN_*` constants  |
| `protocol.ts`    | NDJSON serialization/deserialization                     | `encodeMessage`, `decodeMessage`, `FrameReader` |
| `transport.ts`   | I/O abstraction over sockets, stdio, fds, Named Pipes   | `Transport`, `SocketTransport`, `StdioTransport`, `FdTransport`, `NamedPipeTransport` |
| `errors.ts`      | Typed error classes                                      | `AuthError`, `ProtocolError`, `TimeoutError`, `TransportError`, `ServerError` |
| `channels/*.ts`  | Channel-specific message construction and response parsing | `ControlChannel`, `CommandChannel`, `StateChannel`, `ObserveChannel`, `DebugChannel`, `PtyChannel` |

### index.ts Re-exports

The barrel export provides a flat namespace:

```typescript
// index.ts
export { BashClient } from "./client";
export {
  Message, EvalResult, VarInfo, FuncInfo, AliasInfo, TrapInfo,
  PreCommandEvent, PostCommandEvent, Breakpoint, BreakHitEvent,
  DebugStatus, PtyInfo, InspectItem,
  CHAN_CONTROL, CHAN_COMMAND, CHAN_STATE, CHAN_OBSERVE, CHAN_DEBUG, CHAN_PTY,
  FRAME_MAX_PAYLOAD, TOKEN_HEXLEN,
  OBSERVE_LEVEL_OFF, OBSERVE_LEVEL_COMMAND,
} from "./types";
export {
  AuthError, ProtocolError, TimeoutError, TransportError, ServerError,
} from "./errors";
```

Consumers import from the package root:

```typescript
import { BashClient, EvalResult, AuthError, CHAN_COMMAND } from "bashclient";
```

---

## Dependency Graph

Arrows indicate "depends on" (imports from):

```
index.ts
  ├── client.ts
  │     ├── types.ts
  │     ├── protocol.ts
  │     │     └── types.ts
  │     ├── transport.ts
  │     │     ├── types.ts
  │     │     └── errors.ts
  │     ├── errors.ts
  │     └── channels/
  │           ├── control.ts  ──► types.ts, errors.ts
  │           ├── command.ts  ──► types.ts, errors.ts
  │           ├── state.ts    ──► types.ts, errors.ts
  │           ├── observe.ts  ──► types.ts, errors.ts
  │           ├── debug.ts    ──► types.ts, errors.ts
  │           └── pty.ts      ──► types.ts, errors.ts
  ├── types.ts (leaf)
  └── errors.ts (leaf)
```

Key design constraints:

- `types.ts` and `errors.ts` are leaf modules with no internal imports.
- `protocol.ts` depends only on `types.ts`.
- `transport.ts` depends on `types.ts` and `errors.ts`.
- Channel modules depend on `types.ts` and `errors.ts` only.
- `client.ts` is the integration point that wires everything together.
- No circular dependencies exist.

---

## Data Flow

### Outbound (Client to Server)

```
User code
  │
  ▼
Channel method (e.g., command.eval("ls"))
  │  Constructs Message object: { ch: 1, type: "eval", command: "ls" }
  │
  ▼
BashClient.send(message)
  │  Registers QueueEntry with Promise resolve/reject
  │  Passes message to protocol layer
  │
  ▼
protocol.encodeMessage(message)
  │  Serializes to JSON string + newline: '{"ch":1,"type":"eval","command":"ls"}\n'
  │
  ▼
transport.write(buffer)
  │  Writes bytes to underlying I/O (socket, pipe, fd, stdout)
  │
  ▼
bash-server
```

### Inbound (Server to Client)

```
bash-server
  │
  ▼
transport (data event)
  │  Raw bytes arrive from socket/pipe/fd/stdin
  │
  ▼
FrameReader.feed(chunk)
  │  Buffers data, splits on newline boundaries
  │  Yields complete JSON lines
  │
  ▼
protocol.decodeMessage(line)
  │  Parses JSON, validates ch and type fields
  │  Returns typed Message object
  │
  ▼
BashClient.dispatch(message)
  │  Routes by message.ch to appropriate channel
  │  If channel has pending QueueEntry, resolves its Promise
  │  If channel has event listener, invokes callback
  │
  ▼
Channel handler
  │  Transforms raw message into typed result
  │  (e.g., EvalResult, VarInfo, PreCommandEvent)
  │
  ▼
Promise resolves → User code receives result
```

### Streaming Data Flow (Observe, PTY)

Streaming channels differ from request-response channels:

```
bash-server pushes events
  │
  ▼
BashClient.dispatch(message)
  │  message.ch == CHAN_OBSERVE or CHAN_PTY
  │
  ▼
Channel.handleEvent(message)
  │  Transforms to typed event
  │
  ▼
EventEmitter.emit(eventType, typedEvent)
  │
  ▼
Registered callback(s) invoked synchronously
```

---

## BashClient Internals

### Class Structure

```typescript
class BashClient {
  // --- Transport ---
  private transport: Transport;

  // --- Protocol ---
  private frameReader: FrameReader;

  // --- Request Queue ---
  private queue: Map<string, QueueEntry>;
  private nextRequestId: number;

  // --- Channels (public) ---
  readonly control: ControlChannel;
  readonly command: CommandChannel;
  readonly state: StateChannel;
  readonly observe: ObserveChannel;
  readonly debug: DebugChannel;
  readonly pty: PtyChannel;

  // --- State ---
  private closed: boolean;

  // --- Static factory methods ---
  static async connect(options?): Promise<BashClient>;
  static async connectStdio(options?): Promise<BashClient>;
  static async connectFd(options?): Promise<BashClient>;
  static async connectNamedPipe(options?): Promise<BashClient>;

  // --- Instance methods ---
  async close(): Promise<void>;
  send(message: Message): Promise<Message>;
}
```

### QueueEntry: The Promise Queue

Every outgoing request creates a `QueueEntry` that tracks the pending response:

```typescript
interface QueueEntry {
  resolve: (message: Message) => void;
  reject: (error: Error) => void;
  timer: NodeJS.Timeout | null;
  channel: number;
  type: string;
}
```

The queue is keyed by a request identifier. When a response arrives,
`BashClient.dispatch()` looks up the matching `QueueEntry` and resolves
or rejects its Promise.

**Request-response matching:** bash-server processes requests sequentially
per channel. The client maintains a per-channel FIFO queue. When a response
arrives on channel N, it resolves the oldest pending request for that channel.

```typescript
// Simplified dispatch logic
private dispatch(message: Message): void {
  const ch = message.ch;

  // Check for streaming channels first
  if (ch === CHAN_OBSERVE || ch === CHAN_PTY) {
    this.channels[ch].handleEvent(message);
    return;
  }

  // Check for error responses
  if (message.type === "error") {
    const entry = this.dequeueForChannel(ch);
    if (entry) {
      entry.reject(new ServerError(message.error as string));
    }
    return;
  }

  // Normal response - resolve pending request
  const entry = this.dequeueForChannel(ch);
  if (entry) {
    if (entry.timer) clearTimeout(entry.timer);
    entry.resolve(message);
  }
}
```

### Reader Loop

The reader loop runs continuously while the connection is open, processing
incoming data from the transport:

```typescript
private startReaderLoop(): void {
  this.transport.on("data", (chunk: Buffer) => {
    const messages = this.frameReader.feed(chunk);
    for (const msg of messages) {
      try {
        const decoded = decodeMessage(msg);
        this.dispatch(decoded);
      } catch (e) {
        // Protocol error - reject all pending requests
        this.rejectAll(new ProtocolError(e.message));
      }
    }
  });

  this.transport.on("close", () => {
    this.rejectAll(new TransportError("Connection closed"));
    this.closed = true;
  });

  this.transport.on("error", (err: Error) => {
    this.rejectAll(new TransportError(err.message));
  });
}
```

### Connection Lifecycle

```
connect() / connectStdio() / connectFd() / connectNamedPipe()
  │
  ├── Create Transport instance
  ├── Establish connection (socket connect / spawn process / open fd)
  ├── Create BashClient with transport
  ├── Start reader loop
  ├── Perform authentication handshake
  │     ├── Send: { ch: 0, type: "auth", token: "..." }
  │     └── Recv: { ch: 0, type: "auth_ok" }  or  AuthError
  └── Return connected BashClient
         │
         ├── ... use client ...
         │
  close()
  │
  ├── Send: { ch: 0, type: "disconnect" }
  ├── Drain pending queue (reject with TransportError)
  ├── Close transport
  └── Mark client as closed
```

### Timeout Handling

Each `send()` call can optionally set a timeout:

```typescript
async send(message: Message, timeoutMs?: number): Promise<Message> {
  if (this.closed) {
    throw new TransportError("Client is closed");
  }

  return new Promise<Message>((resolve, reject) => {
    const entry: QueueEntry = {
      resolve,
      reject,
      timer: null,
      channel: message.ch,
      type: message.type,
    };

    if (timeoutMs && timeoutMs > 0) {
      entry.timer = setTimeout(() => {
        this.removeFromQueue(entry);
        reject(new TimeoutError(
          `Timed out after ${timeoutMs}ms waiting for ${message.type} response`
        ));
      }, timeoutMs);
    }

    this.enqueue(entry);
    const encoded = encodeMessage(message);
    this.transport.write(encoded);
  });
}
```

---

## Transport Layer

### Transport Interface

All transports implement a common interface:

```typescript
interface Transport {
  write(data: Buffer): void;
  on(event: "data", handler: (chunk: Buffer) => void): void;
  on(event: "close", handler: () => void): void;
  on(event: "error", handler: (err: Error) => void): void;
  close(): Promise<void>;
}
```

### Transport Implementations

#### SocketTransport

Wraps a Node.js `net.Socket` connected to a Unix domain socket:

```typescript
class SocketTransport implements Transport {
  private socket: net.Socket;

  constructor(socketPath: string);
  async connect(): Promise<void>;
  write(data: Buffer): void;
  on(event: string, handler: Function): void;
  async close(): Promise<void>;
}
```

Key behavior:
- Uses `net.createConnection()` with `path` option for Unix sockets.
- Emits `data` events as `Buffer` chunks (may be partial lines).
- Handles `ECONNREFUSED`, `ENOENT` errors as `TransportError`.
- `close()` calls `socket.end()` then waits for `close` event.

#### StdioTransport

Spawns bash-server as a child process and communicates via stdin/stdout:

```typescript
class StdioTransport implements Transport {
  private child: ChildProcess;

  constructor(serverPath: string, args: string[], env?: NodeJS.ProcessEnv);
  async connect(): Promise<void>;
  write(data: Buffer): void;
  on(event: string, handler: Function): void;
  async close(): Promise<void>;
}
```

Key behavior:
- Uses `child_process.spawn()` with `stdio: ["pipe", "pipe", "pipe"]`.
- Reads from `child.stdout`, writes to `child.stdin`.
- Server stderr is captured for error diagnostics.
- `close()` sends SIGTERM, waits for exit, falls back to SIGKILL.
- Child process lifecycle is tied to transport lifecycle.

#### FdTransport

Wraps pre-opened file descriptors:

```typescript
class FdTransport implements Transport {
  private readStream: fs.ReadStream;
  private writeStream: fs.WriteStream;

  constructor(readFd: number, writeFd?: number);
  async connect(): Promise<void>;
  write(data: Buffer): void;
  on(event: string, handler: Function): void;
  async close(): Promise<void>;
}
```

Key behavior:
- Creates `fs.createReadStream(null, { fd: readFd })`.
- Creates `fs.createWriteStream(null, { fd: writeFd })`.
- If only one fd provided, used for both read and write.
- `close()` destroys both streams.

#### NamedPipeTransport

Connects to a Windows Named Pipe (Cygwin only):

```typescript
class NamedPipeTransport implements Transport {
  private socket: net.Socket;

  constructor(pipePath: string);
  async connect(): Promise<void>;
  write(data: Buffer): void;
  on(event: string, handler: Function): void;
  async close(): Promise<void>;
}
```

Key behavior:
- Uses `net.createConnection()` with the Named Pipe path.
- Named Pipe paths follow the format `\\.\pipe\bash-server-<name>`.
- On Cygwin, the pipe path is translated to the POSIX equivalent.
- DACL security is enforced by the server side, not the client.

### Transport Selection

`BashClient` static methods select the transport:

| Factory Method         | Transport Created     |
|------------------------|-----------------------|
| `connect()`            | `SocketTransport`     |
| `connectStdio()`       | `StdioTransport`      |
| `connectFd()`          | `FdTransport`         |
| `connectNamedPipe()`   | `NamedPipeTransport`  |

---

## Protocol Layer

### NDJSON Framing

The protocol layer handles Newline-Delimited JSON (NDJSON) framing.
Each message is a single JSON object followed by a newline (`\n`).

#### Encoding

```typescript
function encodeMessage(message: Message): Buffer {
  const json = JSON.stringify(message);
  return Buffer.from(json + "\n", "utf-8");
}
```

Encoding is straightforward: serialize to JSON, append newline, convert to
UTF-8 bytes.

#### Decoding

```typescript
function decodeMessage(line: string): Message {
  const obj = JSON.parse(line);
  if (typeof obj.ch !== "number" || typeof obj.type !== "string") {
    throw new ProtocolError("Invalid message: missing ch or type");
  }
  return obj as Message;
}
```

### FrameReader

The `FrameReader` class handles the fact that TCP/socket data arrives in
arbitrary chunks that may not align with newline boundaries:

```typescript
class FrameReader {
  private buffer: string;

  constructor();
  feed(chunk: Buffer): string[];
  reset(): void;
}
```

Implementation:

```typescript
class FrameReader {
  private buffer = "";

  feed(chunk: Buffer): string[] {
    this.buffer += chunk.toString("utf-8");
    const lines: string[] = [];
    let newlineIndex: number;

    while ((newlineIndex = this.buffer.indexOf("\n")) !== -1) {
      const line = this.buffer.substring(0, newlineIndex);
      this.buffer = this.buffer.substring(newlineIndex + 1);
      if (line.length > 0) {
        lines.push(line);
      }
    }

    return lines;
  }

  reset(): void {
    this.buffer = "";
  }
}
```

The `FrameReader` accumulates partial data in an internal buffer and yields
complete lines as they become available. This handles:

- **Partial reads**: A single JSON message split across multiple `data` events.
- **Batched reads**: Multiple JSON messages arriving in a single `data` event.
- **Mixed reads**: Partial end of one message plus start of another.

### Protocol Constants

```typescript
const FRAME_MAX_PAYLOAD = 10 * 1024 * 1024;  // 10 MiB max message size
const TOKEN_HEXLEN = 64;                       // 256-bit token as hex
```

---

## Channel Layer

Each channel module follows the same structural pattern:

1. Constructor receives a reference to `BashClient.send()`.
2. Public methods construct `Message` objects, call `send()`, and transform
   the response into a typed result.
3. Streaming channels extend `EventEmitter` and expose `on()`/`off()`.

### Channel Base Pattern

```typescript
class SomeChannel {
  private send: (msg: Message, timeout?: number) => Promise<Message>;

  constructor(send: (msg: Message, timeout?: number) => Promise<Message>) {
    this.send = send;
  }

  async someMethod(arg: string): Promise<SomeResult> {
    const response = await this.send({
      ch: CHAN_SOME,
      type: "some_request",
      arg,
    });
    return {
      field: response.field as string,
    };
  }
}
```

### Channel Module Details

#### ControlChannel (ch=0)

```typescript
class ControlChannel {
  async ping(): Promise<number>;
  async disconnect(): Promise<void>;
  async configure(options: Record<string, unknown>): Promise<void>;
  async info(): Promise<Record<string, unknown>>;
}
```

Messages:
- `{ ch: 0, type: "ping" }` → `{ ch: 0, type: "pong" }`
- `{ ch: 0, type: "disconnect" }` → (connection closes)
- `{ ch: 0, type: "configure", ... }` → `{ ch: 0, type: "configured" }`

#### CommandChannel (ch=1)

```typescript
class CommandChannel {
  async eval(command: string, options?: { timeout?: number }): Promise<EvalResult>;
  async evalParsed(commandTree: unknown, options?: { timeout?: number }): Promise<EvalResult>;
}
```

Messages:
- `{ ch: 1, type: "eval", command: "..." }` → `{ ch: 1, type: "complete", stdout, stderr, exit_code }`
- `{ ch: 1, type: "eval_parsed", command: {...} }` → `{ ch: 1, type: "complete", ... }`

The `eval` method waits for the `complete` response which contains the
aggregated stdout, stderr, and exit code.

#### StateChannel (ch=2)

```typescript
class StateChannel {
  // Variables
  async getVar(name: string): Promise<VarInfo>;
  async setVar(name: string, value: string, options?: { attributes?: string[] }): Promise<void>;
  async unsetVar(name: string): Promise<void>;

  // Functions
  async getFunc(name: string): Promise<FuncInfo>;
  async setFunc(name: string, definition: string): Promise<void>;
  async unsetFunc(name: string): Promise<void>;

  // Aliases
  async getAlias(name: string): Promise<AliasInfo>;
  async setAlias(name: string, value: string): Promise<void>;
  async unsetAlias(name: string): Promise<void>;

  // Traps
  async getTrap(signal: string): Promise<TrapInfo>;
  async setTrap(signal: string, command: string): Promise<void>;
  async unsetTrap(signal: string): Promise<void>;

  // Inspection
  async inspect(namespace: string): Promise<InspectItem[]>;
}
```

Messages use a uniform pattern:
- `{ ch: 2, type: "get", namespace: "variables", name: "HOME" }` → `{ ch: 2, type: "value", ... }`
- `{ ch: 2, type: "set", namespace: "variables", name: "X", value: "Y" }` → `{ ch: 2, type: "ok" }`
- `{ ch: 2, type: "unset", namespace: "variables", name: "X" }` → `{ ch: 2, type: "ok" }`
- `{ ch: 2, type: "inspect", namespace: "variables" }` → `{ ch: 2, type: "items", items: [...] }`

The state channel methods translate between the human-friendly API (`getVar`,
`setAlias`) and the uniform wire protocol (`get`/`set`/`unset` + `namespace`).

#### ObserveChannel (ch=3)

```typescript
class ObserveChannel extends EventEmitter {
  async setLevel(level: number): Promise<void>;

  // EventEmitter methods:
  on(event: "pre_command", handler: (e: PreCommandEvent) => void): this;
  on(event: "post_command", handler: (e: PostCommandEvent) => void): this;
  off(event: string, handler?: Function): this;

  // Internal:
  handleEvent(message: Message): void;
}
```

The `handleEvent` method is called by `BashClient.dispatch()` for all
incoming messages on channel 3. It transforms the raw message into a
typed event and emits it.

#### DebugChannel (ch=4)

```typescript
class DebugChannel extends EventEmitter {
  async addBreakpoint(options: BreakpointOptions): Promise<Breakpoint>;
  async removeBreakpoint(id: number): Promise<void>;
  async removeAllBreakpoints(): Promise<void>;
  async enableBreakpoint(id: number, enabled: boolean): Promise<void>;
  async listBreakpoints(): Promise<Breakpoint[]>;
  async continue(): Promise<void>;
  async step(): Promise<void>;
  async next(): Promise<void>;
  async finish(): Promise<void>;
  async skip(): Promise<void>;
  async inspectAst(): Promise<unknown>;
  async status(): Promise<DebugStatus>;

  // Events:
  on(event: "break_hit", handler: (e: BreakHitEvent) => void): this;

  // Internal:
  handleEvent(message: Message): void;
}
```

#### PtyChannel (ch=5)

```typescript
class PtyChannel extends EventEmitter {
  async spawn(options?: PtySpawnOptions): Promise<PtyInfo>;
  async writeInput(data: string): Promise<void>;
  async resize(rows: number, cols: number): Promise<void>;
  async signal(sig: string): Promise<void>;
  async close(): Promise<void>;

  // Events:
  on(event: "output", handler: (data: string) => void): this;
  on(event: "close", handler: () => void): this;

  // Internal:
  handleEvent(message: Message): void;
}
```

---

## Error Handling Architecture

### Error Class Hierarchy

All errors extend the built-in `Error` class:

```typescript
class AuthError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "AuthError";
  }
}

class ProtocolError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ProtocolError";
  }
}

class TimeoutError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "TimeoutError";
  }
}

class TransportError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "TransportError";
  }
}

class ServerError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ServerError";
  }
}
```

### Error Source Mapping

| Error Type      | Source                                                   |
|-----------------|----------------------------------------------------------|
| `AuthError`     | Server responds with `auth_fail` to auth request         |
| `ProtocolError` | Malformed JSON, missing fields, unexpected message type  |
| `TimeoutError`  | `QueueEntry` timer fires before response arrives         |
| `TransportError`| Socket error, connection refused, pipe broken, EOF       |
| `ServerError`   | Server sends `{ type: "error", error: "..." }` response |

### Error Propagation

Errors propagate through the Promise chain:

```
Transport error event
  │
  ▼
BashClient.rejectAll(TransportError)
  │  Iterates all pending QueueEntry items
  │  Calls reject() on each with TransportError
  │
  ▼
Promise rejects → catch block in user code
```

For server errors on individual requests:

```
Server sends { ch: N, type: "error", error: "msg" }
  │
  ▼
BashClient.dispatch()
  │  Finds QueueEntry for channel N
  │  Calls reject(new ServerError("msg"))
  │
  ▼
Promise rejects → catch block in user code
```

---

## Event System

### EventEmitter Usage

Three channels use Node.js `EventEmitter` for streaming data:

- `ObserveChannel`: `pre_command`, `post_command` events
- `DebugChannel`: `break_hit` events
- `PtyChannel`: `output`, `close` events

### Synchronous Callbacks

All event callbacks are invoked **synchronously** from the reader loop.
This is a deliberate design choice:

1. Predictable execution order.
2. No risk of interleaved async operations.
3. Simpler mental model for consumers.

Consequence: callbacks must not perform async operations or call `await`.
If async work is needed, queue it:

```typescript
client.observe.on("pre_command", (event) => {
  // DON'T: await someAsyncOp()
  // DO: queue for later
  setImmediate(() => handleEventAsync(event));
});
```

### Event Flow

```
Transport data arrives
  │
  ▼
FrameReader yields complete line
  │
  ▼
decodeMessage() parses JSON
  │
  ▼
dispatch() routes to channel
  │
  ▼
channel.handleEvent(message)
  │
  ▼
EventEmitter.emit(eventType, typedEvent)
  │
  ▼
All registered handlers called synchronously
  │
  ▼
Control returns to reader loop
```

---

## Testing Architecture

### Test Framework

Tests use **Jest** with **ts-jest** for TypeScript support.

### Test Organization

```
tests/
├── client.test.ts          # BashClient lifecycle, connect/close, send/receive
├── protocol.test.ts        # encodeMessage, decodeMessage, FrameReader
├── transport.test.ts       # Transport implementations (mocked I/O)
├── errors.test.ts          # Error class construction and instanceof checks
├── channels/
│   ├── control.test.ts     # ping, disconnect, configure
│   ├── command.test.ts     # eval, evalParsed, timeout
│   ├── state.test.ts       # get/set/unset for all 4 namespaces
│   ├── observe.test.ts     # setLevel, event emission
│   ├── debug.test.ts       # breakpoints, stepping, inspectAst
│   └── pty.test.ts         # spawn, writeInput, resize, signal
└── integration/
    └── basic.test.ts       # End-to-end with real bash-server
```

### Mocking Strategy

Unit tests mock the transport layer to avoid requiring a running bash-server:

```typescript
class MockTransport implements Transport {
  private handlers: Map<string, Function[]> = new Map();
  public written: Buffer[] = [];

  write(data: Buffer): void {
    this.written.push(data);
  }

  on(event: string, handler: Function): void {
    if (!this.handlers.has(event)) {
      this.handlers.set(event, []);
    }
    this.handlers.get(event)!.push(handler);
  }

  // Test helper: simulate incoming data
  simulateData(data: string): void {
    const handlers = this.handlers.get("data") || [];
    for (const h of handlers) {
      h(Buffer.from(data, "utf-8"));
    }
  }

  async close(): Promise<void> {}
}
```

### Test Patterns

#### Request-Response Test

```typescript
it("should eval a command", async () => {
  const mock = new MockTransport();
  const client = new BashClient(mock);

  // Simulate server response after client sends
  setTimeout(() => {
    mock.simulateData(
      '{"ch":1,"type":"complete","stdout":"hello\\n","stderr":"","exit_code":0}\n'
    );
  }, 10);

  const result = await client.command.eval("echo hello");
  expect(result.stdout).toBe("hello\n");
  expect(result.exit_code).toBe(0);

  // Verify what was sent
  const sent = JSON.parse(mock.written[0].toString());
  expect(sent.ch).toBe(1);
  expect(sent.type).toBe("eval");
  expect(sent.command).toBe("echo hello");
});
```

#### Event Test

```typescript
it("should emit pre_command events", async () => {
  const mock = new MockTransport();
  const client = new BashClient(mock);

  const events: PreCommandEvent[] = [];
  client.observe.on("pre_command", (e) => events.push(e));

  mock.simulateData(
    '{"ch":3,"type":"pre_command","seq":1,"timestamp":123,"command":"ls","cwd":"/","line_number":1,"is_subshell":false,"is_async":false}\n'
  );

  expect(events).toHaveLength(1);
  expect(events[0].command).toBe("ls");
  expect(events[0].seq).toBe(1);
});
```

#### Error Test

```typescript
it("should throw TimeoutError on timeout", async () => {
  const mock = new MockTransport();
  const client = new BashClient(mock);

  // Never send a response - let it time out
  await expect(
    client.command.eval("sleep 100", { timeout: 100 })
  ).rejects.toThrow(TimeoutError);
});
```

### Coverage Requirements

The jest configuration enforces 80% minimum coverage:

| Metric     | Threshold |
|------------|-----------|
| Branches   | 80%       |
| Functions  | 80%       |
| Lines      | 80%       |
| Statements | 80%       |

### Integration Test Setup

Integration tests use a helper that starts and stops bash-server:

```typescript
let server: ChildProcess;
let client: BashClient;

beforeAll(async () => {
  server = spawn("bash-server", ["--name", "jest-test"]);
  await new Promise((r) => setTimeout(r, 500)); // Wait for server startup
  client = await BashClient.connect({ name: "jest-test" });
});

afterAll(async () => {
  await client.close();
  server.kill();
});
```

---

## Design Decisions

### Why No External Dependencies?

- Zero supply-chain risk.
- No version conflicts with consumer projects.
- Smaller install footprint.
- Node.js stdlib provides everything needed (net, child_process, fs, events).

### Why NDJSON (Not Binary v2)?

- Simpler implementation (JSON.parse/stringify vs. binary framing).
- Human-readable wire format for debugging.
- bash-server auto-detects the format, so clients choose freely.
- Performance difference is negligible for typical shell operations.

### Why Synchronous Event Callbacks?

- Matches Node.js EventEmitter convention.
- Prevents race conditions from interleaved async callbacks.
- Simpler error handling (no unhandled rejections from callbacks).
- Consumers can easily queue async work if needed.

### Why Per-Channel FIFO (Not Request IDs)?

- Matches bash-server's sequential processing model.
- Simpler protocol (no request_id field needed).
- Per-channel ordering is sufficient because bash processes commands serially.
- Parallel requests on different channels work naturally.
