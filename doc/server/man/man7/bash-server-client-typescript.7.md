# BASH-SERVER-CLIENT-TYPESCRIPT(7) -- TypeScript client binding for bash-server

# DESCRIPTION

The TypeScript client binding provides a Promise-based interface to the
bash-server v2 NDJSON protocol.  It is implemented as the `bashclient`
npm package, requires Node.js 16 or later, and depends only on the
Node.js standard library (`net`, `child_process`, `readline`).

The binding uses Promises and async/await throughout.  A background
reader loop dispatches incoming messages to per-channel Promise queues
and registered event callbacks.

# CONCEPTS

# Installation

The package is located at `clients/typescript/` and uses npm:

```sh
cd clients/typescript
npm install
npm run build
```

This compiles TypeScript sources to both ESM and CommonJS outputs
under `dist/`.  The package exports both formats:

```json
{
  "main": "dist/cjs/index.js",
  "module": "dist/esm/index.js",
  "types": "dist/types/index.d.ts"
}
```

Tests are run with Jest:

```sh
npm test
```

# Package structure

```
src/
    index.ts         -- Package exports
    client.ts        -- BashClient class
    channels/        -- Six channel classes
        control.ts
        command.ts
        state.ts
        observe.ts
        debug.ts
        pty.ts
    errors.ts        -- Error class hierarchy
    protocol.ts      -- NDJSON encode/decode
    transport.ts     -- Four transport implementations
    types.ts         -- TypeScript interface definitions
```

# Architecture

The central class is `BashClient`.  It owns a `Transport` instance and
runs a background reader loop (a non-blocking async function) that
continuously reads NDJSON lines from the transport.

Incoming messages are routed by channel ID:

- **Request/response messages** are placed on per-channel Promise
  queues.  Each queue entry is a `{ resolve, reject, timer }` object.
  When a channel method sends a request, it creates a new Promise and
  pushes it to the queue; the reader loop resolves it when the
  response arrives.

- **Server-push messages** (observe events, debug break_hit, PTY
  output/exit) are dispatched directly to registered callbacks via the
  channel's `dispatch()` method.

Each channel is exposed as a public readonly property:

```typescript
client.control   // ControlChannel  (CHAN_CONTROL = 0)
client.command   // CommandChannel  (CHAN_COMMAND = 1)
client.state     // StateChannel    (CHAN_STATE = 2)
client.observe   // ObserveChannel  (CHAN_OBSERVE = 3)
client.debug     // DebugChannel    (CHAN_DEBUG = 4)
client.pty       // PtyChannel      (CHAN_PTY = 5)
```

# Connection

`BashClient` provides four static factory methods for establishing
connections, each returning a `Promise<BashClient>`:

```typescript
// Unix domain socket (default)
const client = await BashClient.connect("/tmp/bash-server-1000/sock");

// Subprocess (--stdio mode)
const client = await BashClient.connectStdio("bash-server", "--stdio");

// Inherited file descriptor
const client = await BashClient.connectFd(3);

// Windows Named Pipe (Cygwin)
const client = await BashClient.connectNamedPipe("\\\\.\\pipe\\bash-server-myname");

// From existing transport
const client = BashClient.fromTransport(myTransport);
```

Note that `fromTransport()` is synchronous -- it takes an already-
connected transport and immediately starts the reader loop.

# Lifecycle management

TypeScript does not have a built-in `using` statement equivalent to
Python's `async with`.  Use `try`/`finally` to ensure cleanup:

```typescript
const client = await BashClient.connect(socketPath);
try {
    await client.auth(token);
    const result = await client.eval("echo hello");
    console.log(result.stdout);
} finally {
    await client.close();
}
```

The `close()` method stops the reader loop, sends a `disconnect`
message, closes the transport, and rejects any pending Promise queue
entries with `TransportError("connection closed")`.

# Authentication

After connecting, authenticate before using channels other than
CHAN_CONTROL:

```typescript
await client.auth(token);
```

The token is a 64-character hex string.  If authentication fails,
`AuthError` is thrown.  Check authentication status with:

```typescript
if (client.isAuthenticated) {
    // safe to use all channels
}
```

# Type system

All protocol objects are defined as TypeScript interfaces in
`src/types.ts`:

| Interface            | Fields                                          |
|----------------------|-------------------------------------------------|
| `Message`            | `ch`, `type`, `[key: string]: unknown`          |
| `EvalResult`         | `stdout`, `stderr`, `exit_code`                 |
| `VarInfo`            | `name`, `value`, `attributes`                   |
| `FuncInfo`           | `name`, `definition`                            |
| `AliasInfo`          | `name`, `value`                                 |
| `TrapInfo`           | `signal`, `command`                             |
| `PreCommandEvent`    | `seq`, `timestamp`, `command`, `cwd`, `line_number`, `is_subshell`, `is_async` |
| `PostCommandEvent`   | `seq`, `timestamp`, `command`, `exit_status`, `signal_number`, `duration_ms` |
| `Breakpoint`         | `id`, `kind`, `enabled`, `hit_count`, `pattern?`, `line?`, `condition?` |
| `BreakHitEvent`      | `line`, `command`, `depth`                      |
| `DebugStatus`        | `active`, `mode`, `breakpoints`, `depth`        |
| `PtyInfo`            | `rows`, `cols`, `pid`, `strip_ansi`             |
| `InspectItem`        | `name`, `value?`, `definition?`, `attributes?`, `signal?`, `command?` |

The `Message` interface is the base wire type with `ch` (channel ID)
and `type` (message type) fields, plus arbitrary additional fields.

# Error handling

All errors extend `BashClientError`, which extends the built-in
`Error` class:

```
BashClientError
    AuthError          -- Authentication failed
    ProtocolError      -- Malformed frame or message
    TimeoutError       -- Operation timed out
    TransportError     -- Socket or I/O error
    ServerError        -- Server returned error response
        .channel       -- Channel that produced the error
```

Standard `try`/`catch` patterns:

```typescript
import { AuthError, TimeoutError, ServerError } from "bashclient";

try {
    await client.auth(wrongToken);
} catch (e) {
    if (e instanceof AuthError) {
        console.error("bad token");
    }
}

try {
    const result = await client.eval("sleep 60", 5000);
} catch (e) {
    if (e instanceof TimeoutError) {
        console.error("command timed out");
    }
}
```

Each error class sets a descriptive `name` property (`"AuthError"`,
`"ProtocolError"`, etc.) for easy identification in stack traces.

# Naming conventions

The TypeScript binding uses `camelCase` for all method and property
names, following standard TypeScript/JavaScript conventions:

| Python equivalent    | TypeScript method              |
|----------------------|--------------------------------|
| `state.get_var()`    | `state.getVar()`               |
| `state.set_var()`    | `state.setVar()`               |
| `state.unset_var()`  | `state.unsetVar()`             |
| `state.get_func()`   | `state.getFunc()`              |
| `state.get_alias()`  | `state.getAlias()`             |
| `debug.add_breakpoint()` | `debug.addBreakpoint()`   |
| `debug.remove_breakpoint()` | `debug.removeBreakpoint()` |
| `debug.list_breakpoints()` | `debug.listBreakpoints()` |
| `debug.inspect_ast()`| `debug.inspectAst()`           |
| `pty.write_input()`  | `pty.writeInput()`             |

Wire-format field names in interfaces retain `snake_case` to match
the JSON protocol (e.g., `exit_code`, `hit_count`, `strip_ansi`).

# CONTROL channel (channel 0)

```typescript
// Authenticate
await client.auth(token);

// Ping
await client.ping();

// Configure
await client.control.configure({ observe_level: 1 });

// Disconnect
await client.control.disconnect();
```

# COMMAND channel (channel 1)

```typescript
// Simple eval
const result = await client.eval("echo hello");
console.log(result.stdout);    // "hello\n"
console.log(result.stderr);    // ""
console.log(result.exit_code); // 0

// With timeout (milliseconds)
const result2 = await client.eval("sleep 2 && echo done", 10000);

// Via channel object
const result3 = await client.command.eval("ls -la", { timeout: 5000 });
```

# STATE channel (channel 2)

```typescript
// Variables
const info = await client.state.getVar("HOME");
console.log(info.value);        // "/home/user"
console.log(info.attributes);   // []

await client.state.setVar("MY_VAR", "hello", ["-x"]);
await client.state.unsetVar("MY_VAR");

// Functions
const func = await client.state.getFunc("my_function");
console.log(func.definition);

await client.state.unsetFunc("my_function");

// Aliases
const alias = await client.state.getAlias("ll");
await client.state.setAlias("ll", "ls -la --color");
await client.state.unsetAlias("ll");

// Traps
await client.state.setTrap("SIGINT", "echo interrupted");
await client.state.unsetTrap("SIGINT");

// Inspect
const items = await client.state.inspect("variables");
```

# OBSERVE channel (channel 3)

```typescript
// Subscribe
await client.observe.subscribe(1);

// Register callbacks (EventEmitter-style)
client.observe.on("pre_command", (event: PreCommandEvent) => {
    console.log(`[${event.seq}] running: ${event.command}`);
});

client.observe.on("post_command", (event: PostCommandEvent) => {
    console.log(`[${event.seq}] done: exit=${event.exit_status}`);
});

// Execute commands -- callbacks fire automatically
await client.eval("echo hello");

// Unregister
client.observe.off("pre_command", myCallback);
client.observe.off("post_command");  // remove all

// Unsubscribe
await client.observe.unsubscribe();
```

# DEBUG channel (channel 4)

```typescript
// Enable
await client.debug.enable();

// Status
const status = await client.debug.status();
console.log(`active=${status.active} mode=${status.mode}`);

// Breakpoints
const bpId = await client.debug.addBreakpoint("command", { pattern: "echo*" });
const bpId2 = await client.debug.addBreakpoint("line", { line: 10 });

const bps = await client.debug.listBreakpoints();
for (const bp of bps) {
    console.log(`#${bp.id} ${bp.kind} hits=${bp.hit_count}`);
}

// Break handler
client.debug.on("break_hit", async (event: BreakHitEvent) => {
    console.log(`break at line ${event.line}: ${event.command}`);
    const ast = await client.debug.inspectAst();
    await client.debug.step();
});

// Execution control
await client.debug.continue();
await client.debug.step();
await client.debug.next();
await client.debug.finish();
await client.debug.skip();

// Remove/toggle breakpoints
await client.debug.removeBreakpoint(bpId);
await client.debug.disableBreakpoint(bpId2);
await client.debug.enableBreakpoint(bpId2);

// Disable debugger
await client.debug.disable();
```

# PTY channel (channel 5)

```typescript
// Spawn
const info = await client.pty.spawn({ rows: 24, cols: 80, stripAnsi: true });
console.log(`PTY pid=${info.pid}`);

// Output callbacks
client.pty.on("output", (data: string) => {
    process.stdout.write(data);
});

client.pty.on("exit", (exitCode: number) => {
    console.log(`\nPTY exited: ${exitCode}`);
});

// Input
await client.pty.writeInput("ls -la\n");

// Resize
await client.pty.resize(48, 120);

// Signal
await client.pty.signal("SIGINT");

// Close
await client.pty.close();
```

# Callback model

The observe, debug, and PTY channels use an EventEmitter-style
`on`/`off` pattern:

```typescript
// Register
channel.on("event_name", callback);

// Unregister specific
channel.off("event_name", callback);

// Unregister all for event
channel.off("event_name");
```

Callbacks are always synchronous functions.  If async work is needed,
the callback can return a Promise, but it will not be awaited by the
dispatcher -- use `.then()` or `void` async functions.

Multiple callbacks per event are supported and invoked in registration
order.

# Timeout handling

All request/response operations use a default timeout of 30 seconds
(30000 ms).  Timeouts are implemented with `setTimeout` and
automatically clean up the Promise queue entry on expiry.

Override timeouts per-call:

```typescript
// eval with 10-second timeout
const result = await client.eval("slow-command", 10000);

// channel-level timeout
const info = await client.state.getVar("HOME");  // uses default 30s
```

When a timeout fires, the pending Promise is rejected with
`TimeoutError` and removed from the channel queue.

# Properties

```typescript
client.isConnected      // true if transport is open
client.isAuthenticated  // true if auth() succeeded
```

# Constants

```typescript
import {
    CHAN_CONTROL,          // 0
    CHAN_COMMAND,          // 1
    CHAN_STATE,            // 2
    CHAN_OBSERVE,          // 3
    CHAN_DEBUG,            // 4
    CHAN_PTY,              // 5
    CHAN_MAX,              // 5
    FRAME_MAX_PAYLOAD,    // 1048576
    TOKEN_HEXLEN,         // 64
    OBSERVE_LEVEL_OFF,    // 0
    OBSERVE_LEVEL_COMMAND, // 1
} from "bashclient";
```

# Complete example

```typescript
import { BashClient } from "bashclient";

async function main() {
    const client = await BashClient.connect("/tmp/bash-server-1000/sock");
    try {
        await client.auth("abcdef0123456789...");

        // Simple command
        const result = await client.eval("echo hello world");
        console.log(result.stdout);

        // Set and read a variable
        await client.state.setVar("GREETING", "hello");
        const info = await client.state.getVar("GREETING");
        console.log(info.value);  // "hello"

        // Observe
        const events: PostCommandEvent[] = [];
        client.observe.on("post_command", (e) => events.push(e));
        await client.observe.subscribe();
        await client.eval("true");

        // Wait briefly for push events
        await new Promise(r => setTimeout(r, 100));
        console.log(`Captured ${events.length} events`);
    } finally {
        await client.close();
    }
}

main().catch(console.error);
```

# SEE ALSO

**bash-server-client-api**(7),
**bash-server-client-channels**(7),
**bash-server-client-transports**(7),
**bash-server**(1),
**bash-server-channels**(7)

# AUTHORS

GNU Bash is Copyright (C) Free Software Foundation, Inc.
The bash-server extension was developed as part of the Cygwin Bash project.

# COPYRIGHT

This is free software; see the GNU General Public License v3 or later
for copying conditions.  There is NO warranty.
