# bashclient Usage Guide

## TypeScript/Node.js Client for bash-server

A comprehensive guide to using the `bashclient` package for interacting with
bash-server from TypeScript and Node.js applications.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Core Concepts](#core-concepts)
3. [Getting Started](#getting-started)
4. [Connection and Transport](#connection-and-transport)
5. [Control Channel](#control-channel)
6. [Command Channel](#command-channel)
7. [State Channel](#state-channel)
8. [Observe Channel](#observe-channel)
9. [Debug Channel](#debug-channel)
10. [PTY Channel](#pty-channel)
11. [Error Handling](#error-handling)
12. [Best Practices](#best-practices)
13. [Recipes](#recipes)

---

## Prerequisites

Before using `bashclient`, ensure you have:

1. **Node.js 16+** installed and available in your PATH.
2. **bash-server** built and running (see the main project's build instructions).
3. **bashclient** installed in your project:

   ```bash
   npm install bashclient
   ```

4. For TypeScript usage, ensure your `tsconfig.json` targets ES2020 or later
   and uses `"moduleResolution": "node"`.

### Starting bash-server

The client needs a running bash-server instance. Start one:

```bash
# Default Unix socket transport
bash-server

# With a named socket
bash-server --name myproject

# With stdio transport (for embedding)
bash-server --stdio

# With Named Pipe transport (Cygwin/Windows)
bash-server --named-pipe myapp
```

---

## Core Concepts

### NDJSON Wire Protocol

`bashclient` communicates with bash-server using **Newline-Delimited JSON
(NDJSON)**. Each message is a single JSON object terminated by a newline
character (`\n`). This is the v2 protocol's text-based framing mode.

Every message has at minimum two fields:

```json
{"ch": 0, "type": "ping"}
```

- `ch` &mdash; Channel number (0-5), indicating which subsystem handles the message.
- `type` &mdash; Message type string, determining the operation.

Additional fields are message-specific.

### Channels

bash-server multiplexes six logical channels over a single transport connection:

| Channel      | ID | Purpose                                    |
|-------------|----|--------------------------------------------|
| Control     | 0  | Authentication, ping, disconnect, configure |
| Command     | 1  | Execute bash commands, receive output       |
| State       | 2  | Get/set/unset variables, functions, aliases, traps |
| Observe     | 3  | Subscribe to pre/post command events        |
| Debug       | 4  | Breakpoints, stepping, AST inspection       |
| PTY         | 5  | Pseudo-terminal spawn, I/O, resize          |

Each channel is exposed as a property on the `BashClient` instance, providing
typed methods specific to that channel's operations.

### Promise-Based Async

All operations that communicate with the server return Promises. Use `await`
in async functions or `.then()` chains:

```typescript
// async/await style (recommended)
const result = await client.command.eval("echo hello");

// Promise chain style
client.command.eval("echo hello").then(result => {
  console.log(result.stdout);
});
```

### Message Flow

A typical request-response cycle:

1. Client sends a JSON message with `ch` and `type` fields.
2. The message is serialized to NDJSON and written to the transport.
3. bash-server processes the request and sends one or more response messages.
4. The client's reader loop receives the response and resolves the pending Promise.

Some operations (like observe and PTY) produce **streaming responses** &mdash;
multiple messages over time rather than a single response.

### TypeScript Types

All data structures are defined as TypeScript interfaces in `types.ts`:

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

---

## Getting Started

### Minimal Example

```typescript
import { BashClient } from "bashclient";

async function main() {
  // Connect to bash-server via Unix socket (default)
  const client = await BashClient.connect();

  try {
    // Execute a command
    const result = await client.command.eval("echo 'Hello from bash-server!'");
    console.log(result.stdout);  // "Hello from bash-server!\n"
    console.log(result.exit_code);  // 0
  } finally {
    // Always close the connection
    await client.close();
  }
}

main().catch(console.error);
```

### JavaScript (CommonJS) Example

```javascript
const { BashClient } = require("bashclient");

async function main() {
  const client = await BashClient.connect();
  try {
    const result = await client.command.eval("date");
    console.log(result.stdout.trim());
  } finally {
    await client.close();
  }
}

main().catch(console.error);
```

### Connection with Options

```typescript
import { BashClient } from "bashclient";

async function main() {
  const client = await BashClient.connect({
    socketPath: "/tmp/bash-server-1000/myproject",
    token: "abc123...",
    timeout: 5000,
  });

  try {
    const result = await client.command.eval("whoami");
    console.log("Running as:", result.stdout.trim());
  } finally {
    await client.close();
  }
}

main().catch(console.error);
```

---

## Connection and Transport

`bashclient` supports four transport modes, matching the four modes supported
by bash-server. Each has a static factory method on `BashClient`.

### Unix Socket Transport (Default)

The most common transport. Connects to a Unix domain socket.

```typescript
import { BashClient } from "bashclient";

// Auto-discover socket path (checks env, config, XDG, /tmp)
const client = await BashClient.connect();

// Explicit socket path
const client2 = await BashClient.connect({
  socketPath: "/tmp/bash-server-1000/sock",
});

// Named server (resolves to standard path with server name)
const client3 = await BashClient.connect({
  name: "myproject",
});

// With explicit token (bypasses token file reading)
const client4 = await BashClient.connect({
  socketPath: "/tmp/bash-server-1000/sock",
  token: "a1b2c3d4e5f6...",
});

// With connection timeout
const client5 = await BashClient.connect({
  timeout: 10000,  // 10 seconds
});
```

**Socket path resolution order:**

1. Explicit `socketPath` option
2. `$BASH_SERVER_SOCKET` environment variable
3. `~/.bash-serverrc` configuration file
4. `$XDG_RUNTIME_DIR/bash-server/sock`
5. `/tmp/bash-server-<uid>/sock`

**Token resolution order:**

1. Explicit `token` option
2. Token file next to socket (`.token` suffix)
3. `$BASH_SERVER_TOKEN` environment variable

### stdio Transport

Spawns bash-server as a child process and communicates via stdin/stdout.
Useful for embedding or testing.

```typescript
import { BashClient } from "bashclient";

// Default: spawns "bash-server --stdio"
const client = await BashClient.connectStdio();

// Custom server path
const client2 = await BashClient.connectStdio({
  serverPath: "/usr/local/bin/bash-server",
});

// With additional server arguments
const client3 = await BashClient.connectStdio({
  serverPath: "/usr/local/bin/bash-server",
  args: ["--login", "--init", "/path/to/init.sh"],
});

// With environment variables for the server process
const client4 = await BashClient.connectStdio({
  env: { BASH_ENV: "/path/to/bashrc" },
});
```

The stdio transport has a key advantage: **no authentication required**. Since
the client spawns and owns the server process, the connection is inherently
trusted. The server sends a token on startup which the client reads and uses
automatically.

```typescript
const client = await BashClient.connectStdio();
try {
  // No explicit auth needed - handled automatically
  const result = await client.command.eval("echo 'stdio works'");
  console.log(result.stdout);
} finally {
  // Closing the client also terminates the server process
  await client.close();
}
```

### File Descriptor Transport

Connects using a pre-opened file descriptor. Useful when another process
sets up the connection and passes the fd.

```typescript
import { BashClient } from "bashclient";

// Connect using fd 3 for both read and write
const client = await BashClient.connectFd({
  fd: 3,
});

// Separate read and write fds
const client2 = await BashClient.connectFd({
  readFd: 3,
  writeFd: 4,
});

// With token (auth may still be needed depending on server config)
const client3 = await BashClient.connectFd({
  fd: 3,
  token: "a1b2c3...",
});
```

### Named Pipe Transport (Cygwin/Windows)

Connects via a Windows Named Pipe. Only available on Cygwin.

```typescript
import { BashClient } from "bashclient";

// Connect to a named pipe
const client = await BashClient.connectNamedPipe({
  pipeName: "myapp",
});

// Full pipe path
const client2 = await BashClient.connectNamedPipe({
  pipePath: "\\\\.\\pipe\\bash-server-myapp",
});

// With token
const client3 = await BashClient.connectNamedPipe({
  pipeName: "myapp",
  token: "a1b2c3...",
});
```

Named Pipes use Windows DACL security for access control in addition to
token-based authentication.

### Connection Options Summary

| Option       | Type   | Default          | Description                       |
|-------------|--------|------------------|-----------------------------------|
| `socketPath`| string | auto-discovered  | Unix socket path                  |
| `name`      | string | -                | Server name (for path resolution) |
| `token`     | string | auto-discovered  | Authentication token              |
| `timeout`   | number | 30000            | Connection timeout in ms          |
| `serverPath`| string | "bash-server"    | Server binary path (stdio)        |
| `args`      | string[]| []              | Extra server arguments (stdio)    |
| `env`       | object | process.env      | Environment for server (stdio)    |
| `fd`        | number | -                | File descriptor (fd transport)    |
| `readFd`    | number | -                | Read fd (fd transport)            |
| `writeFd`   | number | -                | Write fd (fd transport)           |
| `pipeName`  | string | -                | Named Pipe name (Windows)         |
| `pipePath`  | string | -                | Full Named Pipe path (Windows)    |

---

## Control Channel

The control channel (`client.control`) handles connection lifecycle and
configuration.

### Ping

Check server responsiveness:

```typescript
const latency = await client.control.ping();
console.log(`Server responded in ${latency}ms`);
```

### Disconnect

Gracefully disconnect from the server:

```typescript
await client.control.disconnect();
// Connection is now closed
```

### Configure

Set session-level configuration options:

```typescript
// Set observe level
await client.control.configure({
  observe_level: "command",
});

// Set wire format preference
await client.control.configure({
  wire_format: "ndjson",
});
```

### Server Info

Query server information:

```typescript
const info = await client.control.info();
console.log("Server version:", info.version);
console.log("Protocol:", info.protocol);
console.log("PID:", info.pid);
```

---

## Command Channel

The command channel (`client.command`) is the primary interface for executing
bash commands and receiving their output.

### Basic Evaluation

```typescript
const result = await client.command.eval("echo hello world");
console.log(result.stdout);     // "hello world\n"
console.log(result.stderr);     // ""
console.log(result.exit_code);  // 0
```

The `EvalResult` interface:

```typescript
interface EvalResult {
  stdout: string;
  stderr: string;
  exit_code: number;
}
```

### Multi-line Commands

```typescript
const result = await client.command.eval(`
  for i in 1 2 3; do
    echo "Number: $i"
  done
`);
console.log(result.stdout);
// "Number: 1\nNumber: 2\nNumber: 3\n"
```

### Commands with stderr

```typescript
const result = await client.command.eval("echo out; echo err >&2");
console.log("stdout:", result.stdout);  // "out\n"
console.log("stderr:", result.stderr);  // "err\n"
```

### Exit Codes

```typescript
const result = await client.command.eval("false");
console.log(result.exit_code);  // 1

const result2 = await client.command.eval("exit 42");
console.log(result2.exit_code);  // 42
```

### Pipelines and Complex Commands

```typescript
// Pipes
const result = await client.command.eval("cat /etc/passwd | grep root | head -1");
console.log(result.stdout);

// Command substitution
const result2 = await client.command.eval("echo \"Today is $(date +%A)\"");
console.log(result2.stdout);

// Arithmetic
const result3 = await client.command.eval("echo $((2 ** 10))");
console.log(result3.stdout);  // "1024\n"

// Here documents
const result4 = await client.command.eval(`cat <<'EOF'
Line 1
Line 2
Line 3
EOF`);
console.log(result4.stdout);
```

### Eval with Parsed Commands

Send a pre-parsed COMMAND tree as JSON, bypassing the parser:

```typescript
const commandTree = {
  type: "simple",
  words: ["echo", "hello"],
  redirects: [],
};

const result = await client.command.evalParsed(commandTree);
console.log(result.stdout);  // "hello\n"
```

This is useful for programmatic command construction or when integrating
with tools that produce ASTs.

### Command Timeout

```typescript
try {
  // Command-level timeout
  const result = await client.command.eval("sleep 60", {
    timeout: 5000,  // 5 second timeout
  });
} catch (e) {
  if (e instanceof TimeoutError) {
    console.log("Command timed out");
  }
}
```

### Sequential Commands with Shared State

bash-server maintains shell state across evaluations within a session:

```typescript
await client.command.eval("export MY_VAR='hello'");
await client.command.eval("MY_COUNT=42");

const result = await client.command.eval("echo $MY_VAR $MY_COUNT");
console.log(result.stdout);  // "hello 42\n"

// Functions persist too
await client.command.eval("greet() { echo \"Hello, $1!\"; }");
const result2 = await client.command.eval("greet World");
console.log(result2.stdout);  // "Hello, World!\n"
```

---

## State Channel

The state channel (`client.state`) provides direct access to bash's internal
state: variables, functions, aliases, and traps. This is more efficient than
using `eval` with echo/declare commands.

### Variables

#### Get a Variable

```typescript
const info = await client.state.getVar("HOME");
console.log(info.name);        // "HOME"
console.log(info.value);       // "/home/user"
console.log(info.attributes);  // ["exported"]
```

The `VarInfo` interface:

```typescript
interface VarInfo {
  name: string;
  value: string;
  attributes: string[];
}
```

Attributes include: `exported`, `readonly`, `integer`, `array`, `associative`,
`nameref`, `lowercase`, `uppercase`, `trace`.

#### Set a Variable

```typescript
// Simple string variable
await client.state.setVar("MY_VAR", "hello");

// With attributes
await client.state.setVar("MY_INT", "42", { attributes: ["integer"] });

// Exported variable
await client.state.setVar("PATH_EXTRA", "/opt/bin", { attributes: ["exported"] });

// Readonly variable (cannot be changed after setting)
await client.state.setVar("VERSION", "1.0", { attributes: ["readonly"] });
```

#### Unset a Variable

```typescript
await client.state.unsetVar("MY_VAR");
```

#### Array Variables

```typescript
// Set array via eval first
await client.command.eval("declare -a MY_ARRAY=(one two three)");

// Get array variable
const info = await client.state.getVar("MY_ARRAY");
console.log(info.attributes);  // ["array"]
console.log(info.value);       // Array representation

// Set an indexed array
await client.state.setVar("COLORS", "(red green blue)", {
  attributes: ["array"],
});

// Associative arrays
await client.command.eval("declare -A MY_MAP=([key1]=val1 [key2]=val2)");
const mapInfo = await client.state.getVar("MY_MAP");
console.log(mapInfo.attributes);  // ["associative"]
```

### Functions

#### Get a Function

```typescript
const func = await client.state.getFunc("greet");
console.log(func.name);        // "greet"
console.log(func.definition);  // "greet () { echo \"Hello, $1!\" ; }"
```

The `FuncInfo` interface:

```typescript
interface FuncInfo {
  name: string;
  definition: string;
}
```

#### Set a Function

```typescript
await client.state.setFunc("add", "add() { echo $(($1 + $2)); }");

// Verify it works
const result = await client.command.eval("add 3 4");
console.log(result.stdout);  // "7\n"
```

#### Unset a Function

```typescript
await client.state.unsetFunc("add");
```

### Aliases

#### Get an Alias

```typescript
const alias = await client.state.getAlias("ll");
console.log(alias.name);   // "ll"
console.log(alias.value);  // "ls -la"
```

The `AliasInfo` interface:

```typescript
interface AliasInfo {
  name: string;
  value: string;
}
```

#### Set an Alias

```typescript
await client.state.setAlias("ll", "ls -la --color=auto");
await client.state.setAlias("gs", "git status");
await client.state.setAlias("...", "cd ../..");
```

#### Unset an Alias

```typescript
await client.state.unsetAlias("ll");
```

### Traps

#### Get a Trap

```typescript
const trap = await client.state.getTrap("EXIT");
console.log(trap.signal);   // "EXIT"
console.log(trap.command);  // "cleanup_function"
```

The `TrapInfo` interface:

```typescript
interface TrapInfo {
  signal: string;
  command: string;
}
```

#### Set a Trap

```typescript
await client.state.setTrap("EXIT", "echo 'Session ending'");
await client.state.setTrap("INT", "echo 'Interrupted'; exit 1");
```

#### Unset a Trap

```typescript
await client.state.unsetTrap("EXIT");
```

### Inspect

Enumerate all items in a namespace:

```typescript
// List all exported variables
const vars = await client.state.inspect("variables");
for (const item of vars) {
  console.log(`${item.name}=${item.value}`);
}

// List all functions
const funcs = await client.state.inspect("functions");
for (const item of funcs) {
  console.log(`${item.name}: ${item.definition?.substring(0, 60)}...`);
}

// List all aliases
const aliases = await client.state.inspect("aliases");
for (const item of aliases) {
  console.log(`alias ${item.name}='${item.value}'`);
}

// List all traps
const traps = await client.state.inspect("traps");
for (const item of traps) {
  console.log(`trap '${item.command}' ${item.signal}`);
}
```

The `InspectItem` interface:

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

---

## Observe Channel

The observe channel (`client.observe`) subscribes to command execution events.
Each time a command runs in the bash session, you receive pre-command and
post-command events with timing, working directory, and exit status.

### Setting the Observe Level

```typescript
import { OBSERVE_LEVEL_COMMAND, OBSERVE_LEVEL_OFF } from "bashclient";

// Enable observation
await client.observe.setLevel(OBSERVE_LEVEL_COMMAND);

// Disable observation
await client.observe.setLevel(OBSERVE_LEVEL_OFF);
```

### Registering Event Handlers

Use `on()` and `off()` methods for event subscription. Callbacks are
synchronous (they must not return Promises).

```typescript
// Pre-command event handler
function onPreCommand(event: PreCommandEvent) {
  console.log(`[${event.seq}] Running: ${event.command}`);
  console.log(`  CWD: ${event.cwd}`);
  console.log(`  Line: ${event.line_number}`);
  if (event.is_subshell) console.log("  (subshell)");
  if (event.is_async) console.log("  (background)");
}

// Post-command event handler
function onPostCommand(event: PostCommandEvent) {
  console.log(`[${event.seq}] Completed: ${event.command}`);
  console.log(`  Exit: ${event.exit_status}`);
  console.log(`  Duration: ${event.duration_ms}ms`);
  if (event.signal_number) {
    console.log(`  Signal: ${event.signal_number}`);
  }
}

// Register handlers
client.observe.on("pre_command", onPreCommand);
client.observe.on("post_command", onPostCommand);

// Enable observation
await client.observe.setLevel(OBSERVE_LEVEL_COMMAND);

// Execute commands - events will fire
await client.command.eval("echo hello");
await client.command.eval("ls /tmp");
await client.command.eval("sleep 1");
```

The `PreCommandEvent` interface:

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

The `PostCommandEvent` interface:

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

### Removing Event Handlers

```typescript
// Remove specific handler
client.observe.off("pre_command", onPreCommand);
client.observe.off("post_command", onPostCommand);

// Remove all handlers for an event type
client.observe.off("pre_command");
client.observe.off("post_command");
```

### Command Profiling Example

```typescript
const timings: Map<number, { command: string; start: number }> = new Map();
const profile: Array<{ command: string; duration: number }> = [];

client.observe.on("pre_command", (event: PreCommandEvent) => {
  timings.set(event.seq, {
    command: event.command,
    start: event.timestamp,
  });
});

client.observe.on("post_command", (event: PostCommandEvent) => {
  const start = timings.get(event.seq);
  if (start) {
    profile.push({
      command: start.command,
      duration: event.duration_ms,
    });
    timings.delete(event.seq);
  }
});

await client.observe.setLevel(OBSERVE_LEVEL_COMMAND);

// Run your workload
await client.command.eval("find /usr -name '*.conf' 2>/dev/null | head -20");
await client.command.eval("grep -r 'pattern' /etc/ 2>/dev/null | wc -l");

// Analyze results
await client.observe.setLevel(OBSERVE_LEVEL_OFF);

for (const entry of profile) {
  console.log(`${entry.duration}ms  ${entry.command}`);
}
```

---

## Debug Channel

The debug channel (`client.debug`) provides debugger-like control over bash
command execution: breakpoints, single-stepping, and AST inspection.

### Setting Breakpoints

```typescript
// Break on a command pattern (glob match)
const bp1 = await client.debug.addBreakpoint({
  kind: "command",
  pattern: "rm *",
});
console.log("Breakpoint ID:", bp1.id);

// Break on a specific line number
const bp2 = await client.debug.addBreakpoint({
  kind: "line",
  line: 10,
});

// Break on function entry
const bp3 = await client.debug.addBreakpoint({
  kind: "function",
  pattern: "deploy_*",
});

// Conditional breakpoint
const bp4 = await client.debug.addBreakpoint({
  kind: "command",
  pattern: "*",
  condition: "$? != 0",  // Break only on failure
});
```

The `Breakpoint` interface:

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

### Listing Breakpoints

```typescript
const breakpoints = await client.debug.listBreakpoints();
for (const bp of breakpoints) {
  const status = bp.enabled ? "enabled" : "disabled";
  console.log(`#${bp.id} [${status}] ${bp.kind}: ${bp.pattern ?? bp.line}`);
  console.log(`  Hits: ${bp.hit_count}`);
  if (bp.condition) {
    console.log(`  Condition: ${bp.condition}`);
  }
}
```

### Enabling and Disabling Breakpoints

```typescript
// Disable a breakpoint (keeps it but stops matching)
await client.debug.enableBreakpoint(bp1.id, false);

// Re-enable
await client.debug.enableBreakpoint(bp1.id, true);
```

### Removing Breakpoints

```typescript
// Remove a specific breakpoint
await client.debug.removeBreakpoint(bp1.id);

// Remove all breakpoints
await client.debug.removeAllBreakpoints();
```

### Break Hit Events

When a breakpoint is hit during command execution, a `break_hit` event fires:

```typescript
client.debug.on("break_hit", (event: BreakHitEvent) => {
  console.log(`Breakpoint hit at line ${event.line}`);
  console.log(`Command: ${event.command}`);
  console.log(`Call depth: ${event.depth}`);
});
```

The `BreakHitEvent` interface:

```typescript
interface BreakHitEvent {
  line: number;
  command: string;
  depth: number;
}
```

### Stepping Controls

When execution is paused at a breakpoint, use stepping commands:

```typescript
// Continue execution until next breakpoint
await client.debug.continue();

// Step to next command (step into functions)
await client.debug.step();

// Step over (execute functions without stopping inside them)
await client.debug.next();

// Step out (finish current function and stop at caller)
await client.debug.finish();

// Skip this command (do not execute it, move to next)
await client.debug.skip();
```

### AST Inspection

Inspect the Abstract Syntax Tree of the current command at a breakpoint:

```typescript
client.debug.on("break_hit", async (event: BreakHitEvent) => {
  // Note: inspectAst must be called from outside the event handler
  // since callbacks are synchronous. Queue it for execution.
  console.log(`Paused at: ${event.command}`);
});

// After break_hit, inspect the AST
const ast = await client.debug.inspectAst();
console.log(JSON.stringify(ast, null, 2));
```

### Debug Status

Query the current debug state:

```typescript
const status = await client.debug.status();
console.log("Active:", status.active);
console.log("Mode:", status.mode);
console.log("Depth:", status.depth);
console.log("Breakpoints:", status.breakpoints.length);
```

The `DebugStatus` interface:

```typescript
interface DebugStatus {
  active: boolean;
  mode: string;
  breakpoints: Breakpoint[];
  depth: number;
}
```

### Debugger Session Example

A complete debugging session:

```typescript
import { BashClient, BreakHitEvent } from "bashclient";

async function debugSession() {
  const client = await BashClient.connect();

  try {
    // Define a function to debug
    await client.command.eval(`
      process_files() {
        for f in "$@"; do
          echo "Processing: $f"
          wc -l "$f"
        done
        echo "Done"
      }
    `);

    // Set breakpoints
    await client.debug.addBreakpoint({
      kind: "command",
      pattern: "wc *",
    });

    // Handle break hits
    let hitCount = 0;
    client.debug.on("break_hit", (event: BreakHitEvent) => {
      hitCount++;
      console.log(`[Break #${hitCount}] ${event.command}`);
    });

    // Run the function - it will pause at each "wc" command
    // (In practice you would handle stepping in the break_hit handler)
    const result = await client.command.eval(
      "process_files /etc/hostname /etc/shells"
    );

    console.log("Exit code:", result.exit_code);
    console.log("Total breakpoint hits:", hitCount);

    // Cleanup
    await client.debug.removeAllBreakpoints();
  } finally {
    await client.close();
  }
}

debugSession().catch(console.error);
```

---

## PTY Channel

The PTY channel (`client.pty`) spawns a pseudo-terminal within the bash-server
session. This enables interactive terminal applications, raw terminal I/O, and
programs that require a TTY.

### Spawning a PTY

```typescript
const ptyInfo = await client.pty.spawn({
  rows: 24,
  cols: 80,
});

console.log("PTY PID:", ptyInfo.pid);
console.log("Size:", `${ptyInfo.cols}x${ptyInfo.rows}`);
```

The `PtyInfo` interface:

```typescript
interface PtyInfo {
  rows: number;
  cols: number;
  pid: number;
  strip_ansi: boolean;
}
```

### Spawning with ANSI Stripping

If you want clean text output without terminal escape sequences:

```typescript
const ptyInfo = await client.pty.spawn({
  rows: 24,
  cols: 80,
  strip_ansi: true,
});
```

### Writing Input

Send data to the PTY's stdin:

```typescript
// Send a command
await client.pty.writeInput("ls -la\n");

// Send special keys
await client.pty.writeInput("\x03");  // Ctrl-C
await client.pty.writeInput("\x04");  // Ctrl-D (EOF)
await client.pty.writeInput("\x1b");  // Escape
await client.pty.writeInput("\t");    // Tab (for completion)
```

### Reading Output

Register a handler for PTY output:

```typescript
client.pty.on("output", (data: string) => {
  process.stdout.write(data);
});

// Spawn and interact
await client.pty.spawn({ rows: 24, cols: 80 });
await client.pty.writeInput("echo hello\n");
```

### Resizing the PTY

Resize the terminal dimensions (e.g., when the user's terminal window changes):

```typescript
await client.pty.resize(40, 120);  // 40 rows, 120 cols
```

### Sending Signals

Send a signal to the PTY's process group:

```typescript
// Interrupt (Ctrl-C)
await client.pty.signal("INT");

// Terminate
await client.pty.signal("TERM");

// Kill
await client.pty.signal("KILL");

// Suspend (Ctrl-Z)
await client.pty.signal("TSTP");

// Continue
await client.pty.signal("CONT");
```

### Closing the PTY

```typescript
await client.pty.close();
```

### Interactive Terminal Example

A simple interactive terminal relay:

```typescript
import { BashClient } from "bashclient";
import * as readline from "readline";

async function interactiveTerminal() {
  const client = await BashClient.connect();

  try {
    // Forward PTY output to our stdout
    client.pty.on("output", (data: string) => {
      process.stdout.write(data);
    });

    // Handle PTY close
    client.pty.on("close", () => {
      console.log("\n[PTY closed]");
      rl.close();
    });

    // Spawn PTY matching our terminal size
    await client.pty.spawn({
      rows: process.stdout.rows || 24,
      cols: process.stdout.columns || 80,
    });

    // Handle terminal resize
    process.stdout.on("resize", async () => {
      await client.pty.resize(
        process.stdout.rows || 24,
        process.stdout.columns || 80
      );
    });

    // Read input and forward to PTY
    const rl = readline.createInterface({
      input: process.stdin,
      output: process.stdout,
      terminal: false,
    });

    // Set raw mode for full terminal passthrough
    if (process.stdin.isTTY) {
      process.stdin.setRawMode(true);
    }

    process.stdin.on("data", async (data: Buffer) => {
      await client.pty.writeInput(data.toString());
    });

    // Wait for PTY to close
    await new Promise<void>((resolve) => {
      client.pty.on("close", resolve);
    });
  } finally {
    if (process.stdin.isTTY) {
      process.stdin.setRawMode(false);
    }
    await client.close();
  }
}

interactiveTerminal().catch(console.error);
```

### Running Interactive Programs

```typescript
const client = await BashClient.connect();

try {
  const output: string[] = [];

  client.pty.on("output", (data: string) => {
    output.push(data);
  });

  // Spawn with ANSI stripping for clean output
  await client.pty.spawn({ rows: 24, cols: 80, strip_ansi: true });

  // Run an interactive program
  await client.pty.writeInput("top -b -n 1 | head -20\n");

  // Wait for output
  await new Promise((resolve) => setTimeout(resolve, 2000));

  // Capture result
  const fullOutput = output.join("");
  console.log("Top output:", fullOutput);

  await client.pty.close();
} finally {
  await client.close();
}
```

---

## Error Handling

`bashclient` defines five error classes, all extending the standard `Error`
class. Every server communication failure throws a typed error.

### Error Class Hierarchy

```
Error
├── AuthError         # Authentication failed
├── ProtocolError     # Protocol violation or malformed message
├── TimeoutError      # Operation timed out
├── TransportError    # Transport-level failure (connection lost, etc.)
└── ServerError       # Server returned an error response
```

### Importing Error Classes

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

Thrown when authentication fails (invalid or missing token):

```typescript
try {
  const client = await BashClient.connect({
    token: "wrong-token",
  });
} catch (e) {
  if (e instanceof AuthError) {
    console.error("Authentication failed:", e.message);
    // e.message: "Authentication failed: invalid token"
  }
}
```

### ProtocolError

Thrown when the server sends malformed data or the protocol state is invalid:

```typescript
try {
  const result = await client.command.eval("echo test");
} catch (e) {
  if (e instanceof ProtocolError) {
    console.error("Protocol error:", e.message);
    // e.message might be: "Unexpected message type: unknown_type"
    // or: "Malformed JSON in response"
  }
}
```

### TimeoutError

Thrown when an operation exceeds its timeout:

```typescript
try {
  const result = await client.command.eval("sleep 300", {
    timeout: 5000,
  });
} catch (e) {
  if (e instanceof TimeoutError) {
    console.error("Timed out after", e.message);
  }
}
```

### TransportError

Thrown when the underlying connection fails:

```typescript
try {
  const client = await BashClient.connect({
    socketPath: "/nonexistent/socket",
  });
} catch (e) {
  if (e instanceof TransportError) {
    console.error("Connection failed:", e.message);
    // e.message: "ENOENT: no such file or directory"
  }
}
```

Also thrown if the connection drops mid-operation:

```typescript
try {
  const result = await client.command.eval("kill -9 $$");
} catch (e) {
  if (e instanceof TransportError) {
    console.error("Connection lost:", e.message);
  }
}
```

### ServerError

Thrown when the server returns an explicit error response:

```typescript
try {
  await client.state.getVar("NONEXISTENT_SPECIAL_VAR");
} catch (e) {
  if (e instanceof ServerError) {
    console.error("Server error:", e.message);
  }
}
```

### Comprehensive Error Handling

```typescript
import {
  BashClient,
  AuthError,
  ProtocolError,
  TimeoutError,
  TransportError,
  ServerError,
} from "bashclient";

async function robustOperation() {
  let client: BashClient | null = null;

  try {
    client = await BashClient.connect({ timeout: 5000 });

    const result = await client.command.eval("echo hello", {
      timeout: 10000,
    });
    console.log(result.stdout);

  } catch (e) {
    if (e instanceof AuthError) {
      console.error("Auth failed - check token:", e.message);
    } else if (e instanceof TimeoutError) {
      console.error("Operation timed out:", e.message);
    } else if (e instanceof TransportError) {
      console.error("Connection problem:", e.message);
    } else if (e instanceof ProtocolError) {
      console.error("Protocol violation:", e.message);
    } else if (e instanceof ServerError) {
      console.error("Server error:", e.message);
    } else {
      console.error("Unexpected error:", e);
    }
  } finally {
    if (client) {
      await client.close().catch(() => {});
    }
  }
}
```

---

## Best Practices

### Always Close Connections

Node.js does not have Python's `with` statement for automatic resource
management. Always use `try/finally` to ensure cleanup:

```typescript
const client = await BashClient.connect();
try {
  // ... use client ...
} finally {
  await client.close();
}
```

### Use a Helper for Connection Lifecycle

Create a utility function to enforce the cleanup pattern:

```typescript
async function withBashClient<T>(
  fn: (client: BashClient) => Promise<T>,
  options?: ConnectOptions
): Promise<T> {
  const client = await BashClient.connect(options);
  try {
    return await fn(client);
  } finally {
    await client.close().catch(() => {});
  }
}

// Usage
const output = await withBashClient(async (client) => {
  const result = await client.command.eval("uname -a");
  return result.stdout.trim();
});
console.log(output);
```

### AbortController for Cancellation

Use `AbortController` for cooperative cancellation of long-running operations:

```typescript
async function cancelableEval(
  client: BashClient,
  command: string,
  signal: AbortSignal
): Promise<EvalResult> {
  return new Promise<EvalResult>((resolve, reject) => {
    if (signal.aborted) {
      reject(new Error("Aborted"));
      return;
    }

    const onAbort = () => {
      reject(new Error("Aborted"));
    };
    signal.addEventListener("abort", onAbort, { once: true });

    client.command.eval(command).then(
      (result) => {
        signal.removeEventListener("abort", onAbort);
        resolve(result);
      },
      (err) => {
        signal.removeEventListener("abort", onAbort);
        reject(err);
      }
    );
  });
}

// Usage
const controller = new AbortController();
setTimeout(() => controller.abort(), 5000);  // Cancel after 5s

try {
  const result = await cancelableEval(client, "sleep 60", controller.signal);
} catch (e) {
  console.log("Operation was cancelled");
}
```

### Reuse Connections

Creating a new connection for every operation is wasteful. Keep a connection
open for the duration of your application or task:

```typescript
// BAD: New connection per operation
for (const cmd of commands) {
  const client = await BashClient.connect();
  await client.command.eval(cmd);
  await client.close();
}

// GOOD: Reuse connection
const client = await BashClient.connect();
try {
  for (const cmd of commands) {
    await client.command.eval(cmd);
  }
} finally {
  await client.close();
}
```

### Handle Reconnection

If the connection drops, reconnect gracefully:

```typescript
class ResilientClient {
  private client: BashClient | null = null;
  private options: ConnectOptions;

  constructor(options: ConnectOptions = {}) {
    this.options = options;
  }

  private async ensureConnected(): Promise<BashClient> {
    if (!this.client) {
      this.client = await BashClient.connect(this.options);
    }
    return this.client;
  }

  async eval(command: string): Promise<EvalResult> {
    try {
      const client = await this.ensureConnected();
      return await client.command.eval(command);
    } catch (e) {
      if (e instanceof TransportError) {
        // Connection lost - try reconnecting once
        this.client = null;
        const client = await this.ensureConnected();
        return await client.command.eval(command);
      }
      throw e;
    }
  }

  async close(): Promise<void> {
    if (this.client) {
      await this.client.close().catch(() => {});
      this.client = null;
    }
  }
}
```

### Validate Command Input

Never pass unsanitized user input directly to eval:

```typescript
// DANGEROUS: Shell injection
const filename = userInput;
await client.command.eval(`cat ${filename}`);  // DON'T DO THIS

// SAFER: Use quoting
function shellQuote(s: string): string {
  return "'" + s.replace(/'/g, "'\\''") + "'";
}
const result = await client.command.eval(`cat ${shellQuote(filename)}`);

// SAFEST: Use state channel for variable setting
await client.state.setVar("_FILENAME", filename);
const result2 = await client.command.eval('cat "$_FILENAME"');
```

### Use State Channel for Structured Data

Prefer the state channel over parsing command output:

```typescript
// Fragile: parsing output
const result = await client.command.eval("echo $HOME");
const home = result.stdout.trim();

// Robust: state channel
const homeInfo = await client.state.getVar("HOME");
const home2 = homeInfo.value;
```

### Timeout Everything

Always set timeouts on operations that talk to the server:

```typescript
// Connection timeout
const client = await BashClient.connect({ timeout: 5000 });

// Operation timeout
const result = await client.command.eval("some-command", {
  timeout: 30000,
});
```

### Log Errors with Context

```typescript
try {
  const result = await client.command.eval(command);
} catch (e) {
  console.error({
    error: e instanceof Error ? e.message : String(e),
    errorType: e?.constructor?.name,
    command: command,
    timestamp: new Date().toISOString(),
  });
  throw e;
}
```

---

## Recipes

### Recipe: Run a Script File

```typescript
import { BashClient } from "bashclient";
import * as fs from "fs";

async function runScript(scriptPath: string) {
  const client = await BashClient.connect();
  try {
    const script = fs.readFileSync(scriptPath, "utf-8");
    const result = await client.command.eval(script);

    console.log("Exit code:", result.exit_code);
    if (result.stdout) console.log("Output:", result.stdout);
    if (result.stderr) console.error("Errors:", result.stderr);

    return result;
  } finally {
    await client.close();
  }
}

runScript("/path/to/script.sh").catch(console.error);
```

### Recipe: Environment Setup and Teardown

```typescript
async function withEnvironment(
  client: BashClient,
  env: Record<string, string>,
  fn: () => Promise<void>
) {
  // Save current values
  const saved: Map<string, string | null> = new Map();
  for (const [name, value] of Object.entries(env)) {
    try {
      const current = await client.state.getVar(name);
      saved.set(name, current.value);
    } catch {
      saved.set(name, null);
    }
    await client.state.setVar(name, value);
  }

  try {
    await fn();
  } finally {
    // Restore original values
    for (const [name, original] of saved) {
      if (original === null) {
        await client.state.unsetVar(name);
      } else {
        await client.state.setVar(name, original);
      }
    }
  }
}

// Usage
await withEnvironment(
  client,
  { NODE_ENV: "test", DEBUG: "1" },
  async () => {
    const result = await client.command.eval("echo $NODE_ENV");
    console.log(result.stdout);  // "test\n"
  }
);
```

### Recipe: Parallel Command Execution

```typescript
async function parallelEval(
  client: BashClient,
  commands: string[]
): Promise<EvalResult[]> {
  // Note: Commands run serially on the server (single bash process),
  // but we can queue them all at once.
  return Promise.all(
    commands.map((cmd) => client.command.eval(cmd))
  );
}

const results = await parallelEval(client, [
  "uname -s",
  "whoami",
  "pwd",
  "date +%s",
]);
for (const r of results) {
  console.log(r.stdout.trim());
}
```

### Recipe: Command Output Streaming

Combine observe and command channels to see output as it happens:

```typescript
async function streamingEval(
  client: BashClient,
  command: string
): Promise<EvalResult> {
  await client.observe.setLevel(OBSERVE_LEVEL_COMMAND);

  client.observe.on("pre_command", (event: PreCommandEvent) => {
    console.log(`>>> ${event.command}`);
  });

  client.observe.on("post_command", (event: PostCommandEvent) => {
    console.log(`<<< exit ${event.exit_status} (${event.duration_ms}ms)`);
  });

  try {
    return await client.command.eval(command);
  } finally {
    await client.observe.setLevel(OBSERVE_LEVEL_OFF);
  }
}
```

### Recipe: Function Registry

Register and manage bash functions from TypeScript:

```typescript
class FunctionRegistry {
  private client: BashClient;
  private registered: Set<string> = new Set();

  constructor(client: BashClient) {
    this.client = client;
  }

  async register(name: string, body: string): Promise<void> {
    await this.client.state.setFunc(name, `${name}() { ${body}; }`);
    this.registered.add(name);
  }

  async call(name: string, ...args: string[]): Promise<EvalResult> {
    const quotedArgs = args.map(
      (a) => "'" + a.replace(/'/g, "'\\''") + "'"
    );
    return this.client.command.eval(`${name} ${quotedArgs.join(" ")}`);
  }

  async unregisterAll(): Promise<void> {
    for (const name of this.registered) {
      await this.client.state.unsetFunc(name);
    }
    this.registered.clear();
  }
}

// Usage
const registry = new FunctionRegistry(client);
await registry.register("greet", 'echo "Hello, $1!"');
await registry.register("add", "echo $(($1 + $2))");

const r1 = await registry.call("greet", "World");
console.log(r1.stdout);  // "Hello, World!\n"

const r2 = await registry.call("add", "3", "4");
console.log(r2.stdout);  // "7\n"

await registry.unregisterAll();
```

### Recipe: Conditional Debugging

Debug only certain commands based on runtime conditions:

```typescript
async function conditionalDebug(
  client: BashClient,
  script: string,
  shouldBreak: (command: string) => boolean
) {
  // Set a catch-all breakpoint with no condition
  await client.debug.addBreakpoint({
    kind: "command",
    pattern: "*",
  });

  const events: string[] = [];

  client.debug.on("break_hit", (event: BreakHitEvent) => {
    events.push(event.command);
    // Decision is logged; actual step/continue would need
    // to be coordinated outside the sync callback
  });

  const result = await client.command.eval(script);

  await client.debug.removeAllBreakpoints();

  return { result, events };
}
```

### Recipe: Health Check

```typescript
async function healthCheck(
  socketPath?: string,
  timeoutMs = 3000
): Promise<{ healthy: boolean; latency?: number; error?: string }> {
  try {
    const client = await BashClient.connect({
      socketPath,
      timeout: timeoutMs,
    });

    try {
      const start = Date.now();
      await client.control.ping();
      const latency = Date.now() - start;

      return { healthy: true, latency };
    } finally {
      await client.close();
    }
  } catch (e) {
    return {
      healthy: false,
      error: e instanceof Error ? e.message : String(e),
    };
  }
}

// Usage
const status = await healthCheck();
if (status.healthy) {
  console.log(`Server healthy (${status.latency}ms)`);
} else {
  console.error(`Server unhealthy: ${status.error}`);
}
```

### Recipe: Batch Variable Operations

```typescript
async function setVars(
  client: BashClient,
  vars: Record<string, string>
): Promise<void> {
  for (const [name, value] of Object.entries(vars)) {
    await client.state.setVar(name, value);
  }
}

async function getVars(
  client: BashClient,
  names: string[]
): Promise<Record<string, string>> {
  const result: Record<string, string> = {};
  for (const name of names) {
    try {
      const info = await client.state.getVar(name);
      result[name] = info.value;
    } catch {
      result[name] = "";
    }
  }
  return result;
}

// Usage
await setVars(client, {
  APP_NAME: "myapp",
  APP_VERSION: "1.0.0",
  APP_ENV: "production",
});

const vars = await getVars(client, ["APP_NAME", "APP_VERSION", "APP_ENV"]);
console.log(vars);
// { APP_NAME: "myapp", APP_VERSION: "1.0.0", APP_ENV: "production" }
```

### Recipe: stdio Transport for Testing

```typescript
import { BashClient } from "bashclient";

async function isolatedTest(
  testFn: (client: BashClient) => Promise<void>
) {
  // Each test gets its own bash-server process via stdio
  const client = await BashClient.connectStdio();

  try {
    await testFn(client);
  } finally {
    // Closing kills the server process - complete isolation
    await client.close();
  }
}

// In your test suite
describe("my bash operations", () => {
  it("should set and get variables", async () => {
    await isolatedTest(async (client) => {
      await client.state.setVar("TEST", "hello");
      const info = await client.state.getVar("TEST");
      expect(info.value).toBe("hello");
    });
  });

  it("should run commands", async () => {
    await isolatedTest(async (client) => {
      const result = await client.command.eval("echo hello");
      expect(result.stdout.trim()).toBe("hello");
      expect(result.exit_code).toBe(0);
    });
  });
});
```

---

## Next Steps

- [API Reference](API.md) &mdash; Complete API documentation with every method signature
- [Architecture](ARCHITECTURE.md) &mdash; Internal design and module structure
- [Troubleshooting](TROUBLESHOOTING.md) &mdash; Common issues and solutions
- [Examples](examples/README.md) &mdash; Runnable example scripts
