# Language Comparison and Selection Guide

This document compares the four bash-server client bindings and helps
you choose the right one for your project.

---

## Table of Contents

- [Feature Matrix](#feature-matrix)
- [API Style Comparison](#api-style-comparison)
  - [Connect + Auth + Eval](#connect--auth--eval)
  - [State Operations](#state-operations)
  - [Observe Events](#observe-events)
  - [Debug Breakpoints](#debug-breakpoints)
  - [PTY Spawn](#pty-spawn)
  - [Error Handling](#error-handling)
- [Concurrency Models](#concurrency-models)
  - [Python: asyncio](#python-asyncio)
  - [TypeScript: Promise / Event Loop](#typescript-promise--event-loop)
  - [C: Blocking + Poll](#c-blocking--poll)
  - [Java: Daemon Thread + BlockingQueue](#java-daemon-thread--blockingqueue)
- [Choosing a Binding](#choosing-a-binding)
- [Performance Considerations](#performance-considerations)
- [Platform Support Matrix](#platform-support-matrix)
- [Dependency Comparison](#dependency-comparison)

---

## Feature Matrix

| Feature               | Python               | TypeScript          | C                    | Java                 |
|-----------------------|----------------------|---------------------|----------------------|----------------------|
| **Async model**       | asyncio (async/await)| Promise (async/await)| Blocking (sync)     | Blocking (sync)      |
| **Server-push**       | Async callbacks      | EventEmitter-style  | Function pointers + poll | Functional interface + daemon thread |
| **Dependencies**      | None (stdlib only)   | None (Node built-ins)| None (POSIX only)   | junixsocket, Jackson |
| **Memory management** | Garbage collected    | Garbage collected   | Manual (malloc/free) | Garbage collected    |
| **Thread safety**     | Single-threaded (event loop) | Single-threaded (event loop) | Not thread-safe | Thread-safe (synchronized) |
| **Package manager**   | pip (PyPI)           | npm                 | Source / system pkg  | Maven Central        |
| **Min version**       | Python 3.8+          | Node.js 16+         | C99, POSIX           | Java 11+             |
| **Build system**      | setuptools / pip     | tsc + npm            | make                | Maven                |
| **Type system**       | Type hints (optional)| Static (TypeScript)  | Manual (structs)    | Static (generics)    |
| **Binary size**       | ~40 KB (source)      | ~35 KB (compiled)    | ~80 KB (shared lib) | ~120 KB (JAR)        |
| **Channels**          | All 6                | All 6               | All 6               | All 6                |
| **Transports**        | All 4                | All 4               | All 4               | All 4                |
| **Error types**       | All 5                | All 5               | All 5 (return codes)| All 5                |

---

## API Style Comparison

The following examples show the same operations implemented in all four
languages. Each example is complete and runnable.

### Connect + Auth + Eval

**Python** (async):

```python
import asyncio
from bashclient import BashClient

async def main():
    # Connect via Unix socket
    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        # Authenticate with 64-char hex token
        await client.auth("a1b2c3d4e5f6...64-hex-chars...")

        # Evaluate a command
        result = await client.eval("echo hello world")
        print(f"stdout: {result.stdout}")
        print(f"stderr: {result.stderr}")
        print(f"exit:   {result.exit_code}")

asyncio.run(main())
```

**TypeScript** (async):

```typescript
import { BashClient } from "bashclient";

async function main(): Promise<void> {
    // Connect via Unix socket
    const client = await BashClient.connect("/tmp/bash-server-1000/sock");
    try {
        // Authenticate with 64-char hex token
        await client.auth("a1b2c3d4e5f6...64-hex-chars...");

        // Evaluate a command
        const result = await client.eval("echo hello world");
        console.log(`stdout: ${result.stdout}`);
        console.log(`stderr: ${result.stderr}`);
        console.log(`exit:   ${result.exitCode}`);
    } finally {
        await client.close();
    }
}

main();
```

**C** (synchronous):

```c
#include "bashclient.h"
#include <stdio.h>

int main(void) {
    /* Connect via Unix socket */
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock");
    if (!c) {
        fprintf(stderr, "connect failed\n");
        return 1;
    }

    /* Authenticate with 64-char hex token */
    if (bc_auth(c, "a1b2c3d4e5f6...64-hex-chars...") != BC_OK) {
        fprintf(stderr, "auth failed: %s\n", bc_error(c));
        bc_close(c);
        return 1;
    }

    /* Evaluate a command */
    bc_eval_result_t result;
    if (bc_eval(c, "echo hello world", &result) != BC_OK) {
        fprintf(stderr, "eval failed: %s\n", bc_error(c));
        bc_close(c);
        return 1;
    }

    printf("stdout: %s\n", result.stdout_data);
    printf("stderr: %s\n", result.stderr_data);
    printf("exit:   %d\n", result.exit_code);

    bc_eval_result_free(&result);
    bc_close(c);
    return 0;
}
```

**Java** (blocking):

```java
import org.gnu.bash.client.BashClient;
import org.gnu.bash.client.types.EvalResult;

public class Example {
    public static void main(String[] args) throws Exception {
        // Connect via Unix socket
        try (BashClient client = BashClient.connect("/tmp/bash-server-1000/sock")) {
            // Authenticate with 64-char hex token
            client.auth("a1b2c3d4e5f6...64-hex-chars...");

            // Evaluate a command
            EvalResult result = client.eval("echo hello world");
            System.out.println("stdout: " + result.getStdout());
            System.out.println("stderr: " + result.getStderr());
            System.out.println("exit:   " + result.getExitCode());
        }
    }
}
```

### State Operations

**Python**:

```python
# Get a variable
var = await client.state.get_var("PATH")
print(f"PATH = {var.value}")
print(f"type = {var.type}")           # "string"
print(f"attrs = {var.attributes}")    # ["exported"]

# Set a variable with attributes
await client.state.set_var("MY_VAR", "hello", attributes=["exported"])

# Unset a variable
await client.state.unset_var("MY_VAR")

# Set and get an alias
await client.state.set_alias("ll", "ls -lah --color=auto")
alias = await client.state.get_alias("ll")
print(f"ll = {alias.value}")

# Set a trap
await client.state.set_trap("SIGINT", "echo 'caught interrupt'")

# Inspect a namespace (list all items)
items = await client.state.inspect("vars")
for item in items:
    print(f"  {item.name} = {item.value}")
```

**TypeScript**:

```typescript
// Get a variable
const v = await client.state.getVar("PATH");
console.log(`PATH = ${v.value}`);
console.log(`type = ${v.type}`);
console.log(`attrs = ${v.attributes}`);

// Set a variable with attributes
await client.state.setVar("MY_VAR", "hello", ["exported"]);

// Unset a variable
await client.state.unsetVar("MY_VAR");

// Set and get an alias
await client.state.setAlias("ll", "ls -lah --color=auto");
const alias = await client.state.getAlias("ll");
console.log(`ll = ${alias.value}`);

// Set a trap
await client.state.setTrap("SIGINT", "echo 'caught interrupt'");

// Inspect a namespace
const items = await client.state.inspect("vars");
for (const item of items) {
    console.log(`  ${item.name} = ${item.value}`);
}
```

**C**:

```c
/* Get a variable */
bc_var_info_t var;
bc_state_get_var(c, "PATH", &var);
printf("PATH = %s\n", var.value);
printf("type = %s\n", var.type);
bc_var_info_free(&var);

/* Set a variable with attributes */
const char *attrs[] = {"exported", NULL};
bc_state_set_var(c, "MY_VAR", "hello", attrs);

/* Unset a variable */
bc_state_unset_var(c, "MY_VAR");

/* Set and get an alias */
bc_state_set_alias(c, "ll", "ls -lah --color=auto");
bc_alias_info_t alias;
bc_state_get_alias(c, "ll", &alias);
printf("ll = %s\n", alias.value);
bc_alias_info_free(&alias);

/* Set a trap */
bc_state_set_trap(c, "SIGINT", "echo 'caught interrupt'");

/* Inspect a namespace */
char *json = NULL;
bc_state_inspect(c, "vars", &json);
printf("vars: %s\n", json);
bc_free(json);
```

**Java**:

```java
// Get a variable
VarInfo var = client.state.getVar("PATH");
System.out.println("PATH = " + var.getValue());
System.out.println("type = " + var.getType());
System.out.println("attrs = " + var.getAttributes());

// Set a variable with attributes
client.state.setVar("MY_VAR", "hello", List.of("exported"));

// Unset a variable
client.state.unsetVar("MY_VAR");

// Set and get an alias
client.state.setAlias("ll", "ls -lah --color=auto");
AliasInfo alias = client.state.getAlias("ll");
System.out.println("ll = " + alias.getValue());

// Set a trap
client.state.setTrap("SIGINT", "echo 'caught interrupt'");

// Inspect a namespace
JsonNode items = client.state.inspect("vars");
items.forEach(item ->
    System.out.println("  " + item.get("name").asText() + " = " + item.get("value").asText())
);
```

### Observe Events

**Python**:

```python
# Register callbacks for command observation
async def on_pre(event):
    print(f"[PRE]  command={event.command} cwd={event.cwd}")

async def on_post(event):
    print(f"[POST] command={event.command} exit={event.exit_status} "
          f"duration={event.duration_ms}ms")

client.observe.on("pre_command", on_pre)
client.observe.on("post_command", on_post)

# Start observing (level 1 = detailed, includes cwd + timing)
await client.observe.subscribe(level=1)

# Execute some commands -- events fire as they run
await client.eval("echo hello")
await client.eval("ls /nonexistent")

# Stop observing
await client.observe.unsubscribe()
```

**TypeScript**:

```typescript
// Register callbacks for command observation
client.observe.on("pre_command", (event) => {
    console.log(`[PRE]  command=${event.command} cwd=${event.cwd}`);
});

client.observe.on("post_command", (event) => {
    console.log(`[POST] command=${event.command} exit=${event.exitStatus} ` +
                `duration=${event.durationMs}ms`);
});

// Start observing
await client.observe.subscribe(1);

// Execute some commands
await client.eval("echo hello");
await client.eval("ls /nonexistent");

// Stop observing
await client.observe.unsubscribe();
```

**C**:

```c
/* Callback function for pre_command events */
static void on_pre(const char *json, void *userdata) {
    printf("[PRE]  %s\n", json);
}

/* Callback function for post_command events */
static void on_post(const char *json, void *userdata) {
    printf("[POST] %s\n", json);
}

/* Register callbacks */
bc_observe_on_pre_command(c, on_pre, NULL);
bc_observe_on_post_command(c, on_post, NULL);

/* Start observing */
bc_observe_subscribe(c, 1);

/* Execute some commands */
bc_eval_result_t r;
bc_eval(c, "echo hello", &r);
bc_eval_result_free(&r);

bc_eval(c, "ls /nonexistent", &r);
bc_eval_result_free(&r);

/*
 * Process any pending push messages.
 * In the C binding, server-push messages are buffered during
 * bc_eval() and dispatched when you call bc_poll().
 */
bc_poll(c, 0);  /* 0 = no wait, dispatch what's buffered */

/* Stop observing */
bc_observe_unsubscribe(c);
```

**Java**:

```java
// Register callbacks
client.observe.on("pre_command", msg -> {
    System.out.println("[PRE]  command=" + msg.get("command").asText()
                     + " cwd=" + msg.get("cwd").asText());
});

client.observe.on("post_command", msg -> {
    System.out.println("[POST] command=" + msg.get("command").asText()
                     + " exit=" + msg.get("exit_status").asInt()
                     + " duration=" + msg.get("duration_ms").asLong() + "ms");
});

// Start observing
client.observe.subscribe(1);

// Execute some commands
client.eval("echo hello");
client.eval("ls /nonexistent");

// Stop observing
client.observe.unsubscribe();
```

### Debug Breakpoints

**Python**:

```python
# Enable debug mode
await client.debug.enable()

# Set a breakpoint on "echo" commands
bp_id = await client.debug.add_breakpoint(kind="command", pattern="echo")

# Register handler for breakpoint hits
async def on_break(event):
    print(f"Hit breakpoint {event.breakpoint_id} at: {event.command}")

    # Inspect the AST at the break location
    ast = await client.debug.inspect_ast()
    print(f"AST: {ast}")

    # Continue execution
    await client.debug.continue_()

client.debug.on("break_hit", on_break)

# Execute a command that hits the breakpoint
await client.eval("echo this triggers the breakpoint")

# Clean up
await client.debug.remove_breakpoint(bp_id)
await client.debug.disable()
```

**TypeScript**:

```typescript
// Enable debug mode
await client.debug.enable();

// Set a breakpoint on "echo" commands
const bpId = await client.debug.addBreakpoint({
    kind: "command",
    pattern: "echo",
});

// Register handler
client.debug.on("break_hit", async (event) => {
    console.log(`Hit breakpoint ${event.breakpointId} at: ${event.command}`);

    const ast = await client.debug.inspectAst();
    console.log("AST:", ast);

    await client.debug.continue_();
});

// Trigger the breakpoint
await client.eval("echo this triggers the breakpoint");

// Clean up
await client.debug.removeBreakpoint(bpId);
await client.debug.disable();
```

**C**:

```c
/* Enable debug mode */
bc_debug_enable(c);

/* Set a breakpoint */
int bp_id;
bc_debug_add_breakpoint(c, "command", "echo", -1, NULL, &bp_id);

/* Register handler */
static void on_break(const char *json, void *userdata) {
    bc_client_t *c = (bc_client_t *)userdata;
    printf("Hit breakpoint: %s\n", json);

    /* Inspect AST */
    char *ast = NULL;
    bc_debug_inspect_ast(c, &ast);
    if (ast) {
        printf("AST: %s\n", ast);
        bc_free(ast);
    }

    /* Continue execution */
    bc_debug_continue(c);
}

bc_debug_on_break_hit(c, on_break, c);

/* Trigger the breakpoint */
bc_eval_result_t r;
bc_eval(c, "echo this triggers the breakpoint", &r);
bc_eval_result_free(&r);

/* Process pending events */
bc_poll(c, 0);

/* Clean up */
bc_debug_remove_breakpoint(c, bp_id);
bc_debug_disable(c);
```

**Java**:

```java
// Enable debug mode
client.debug.enable();

// Set a breakpoint
int bpId = client.debug.addBreakpoint("command", "echo", -1, null);

// Register handler
client.debug.on("break_hit", ev -> {
    System.out.println("Hit breakpoint " + ev.get("breakpoint_id").asInt()
                     + " at: " + ev.get("command").asText());

    try {
        JsonNode ast = client.debug.inspectAst();
        System.out.println("AST: " + ast);
        client.debug.continue_();
    } catch (Exception e) {
        e.printStackTrace();
    }
});

// Trigger the breakpoint
client.eval("echo this triggers the breakpoint");

// Clean up
client.debug.removeBreakpoint(bpId);
client.debug.disable();
```

### PTY Spawn

**Python**:

```python
# Register output and exit handlers
async def on_output(data: bytes):
    print(data.decode("utf-8"), end="", flush=True)

async def on_exit(exit_code: int):
    print(f"\n[PTY exited with code {exit_code}]")

client.pty.on("output", on_output)
client.pty.on("exit", on_exit)

# Spawn a PTY session
info = await client.pty.spawn(rows=24, cols=80, strip_ansi=True)
print(f"PTY spawned: pid={info.pid}")

# Send input
await client.pty.write_input("echo hello from PTY\n")
await asyncio.sleep(0.5)  # Give time for output

# Resize
await client.pty.resize(rows=48, cols=120)

# Send signal
await client.pty.signal("SIGINT")

# Close
await client.pty.close()
```

**TypeScript**:

```typescript
// Register output and exit handlers
client.pty.on("output", (data: Buffer) => {
    process.stdout.write(data);
});

client.pty.on("exit", (exitCode: number) => {
    console.log(`\n[PTY exited with code ${exitCode}]`);
});

// Spawn a PTY session
const info = await client.pty.spawn({ rows: 24, cols: 80, stripAnsi: true });
console.log(`PTY spawned: pid=${info.pid}`);

// Send input
await client.pty.writeInput("echo hello from PTY\n");
await new Promise((r) => setTimeout(r, 500));

// Resize
await client.pty.resize(48, 120);

// Send signal
await client.pty.signal("SIGINT");

// Close
await client.pty.close();
```

**C**:

```c
/* Callback for PTY output */
static void on_output(const char *data, size_t len, void *userdata) {
    fwrite(data, 1, len, stdout);
    fflush(stdout);
}

/* Callback for PTY exit */
static void on_exit(int exit_code, void *userdata) {
    printf("\n[PTY exited with code %d]\n", exit_code);
}

/* Register callbacks */
bc_pty_on_output(c, on_output, NULL);
bc_pty_on_exit(c, on_exit, NULL);

/* Spawn a PTY session */
bc_pty_info_t info;
bc_pty_spawn(c, 24, 80, NULL, 1 /* strip_ansi */, &info);
printf("PTY spawned: pid=%d\n", info.pid);

/* Send input */
bc_pty_write(c, "echo hello from PTY\n", 20);

/* Poll for output (blocking, 500ms timeout) */
bc_poll(c, 500);

/* Resize */
bc_pty_resize(c, 48, 120);

/* Send signal */
bc_pty_signal(c, "SIGINT");

/* Close */
bc_pty_close(c);
```

**Java**:

```java
// Register output and exit handlers
client.pty.on("output", data -> {
    System.out.print(new String((byte[]) data, StandardCharsets.UTF_8));
    System.out.flush();
});

client.pty.on("exit", code -> {
    System.out.println("\n[PTY exited with code " + code + "]");
});

// Spawn a PTY session
PtyInfo info = client.pty.spawn(24, 80, null, true);
System.out.println("PTY spawned: pid=" + info.getPid());

// Send input
client.pty.writeInput("echo hello from PTY\n");
Thread.sleep(500);

// Resize
client.pty.resize(48, 120);

// Send signal
client.pty.signal("SIGINT");

// Close
client.pty.close();
```

### Error Handling

**Python**:

```python
from bashclient import (
    AuthError, ProtocolError, TimeoutError,
    TransportError, ServerError, BashClientError,
)

try:
    async with await BashClient.connect(path) as client:
        await client.auth(token)
        result = await client.eval("some-command", timeout=10.0)
except AuthError as e:
    print(f"Authentication failed: {e}")
except TimeoutError as e:
    print(f"Operation timed out: {e}")
except TransportError as e:
    print(f"Connection error: {e}")
except ProtocolError as e:
    print(f"Protocol violation: {e}")
except ServerError as e:
    print(f"Server error: {e}")
except BashClientError as e:
    print(f"Client error: {e}")
```

**TypeScript**:

```typescript
import {
    AuthError, ProtocolError, TimeoutError,
    TransportError, ServerError, BashClientError,
} from "bashclient";

try {
    const client = await BashClient.connect(path);
    await client.auth(token);
    const result = await client.eval("some-command", { timeout: 10000 });
    await client.close();
} catch (e) {
    if (e instanceof AuthError) {
        console.error(`Authentication failed: ${e.message}`);
    } else if (e instanceof TimeoutError) {
        console.error(`Operation timed out: ${e.message}`);
    } else if (e instanceof TransportError) {
        console.error(`Connection error: ${e.message}`);
    } else if (e instanceof ProtocolError) {
        console.error(`Protocol violation: ${e.message}`);
    } else if (e instanceof ServerError) {
        console.error(`Server error: ${e.message}`);
    } else if (e instanceof BashClientError) {
        console.error(`Client error: ${e.message}`);
    }
}
```

**C**:

```c
bc_client_t *c = bc_connect(path);
if (!c) {
    fprintf(stderr, "Transport error: connect failed\n");
    return 1;
}

int rc = bc_auth(c, token);
if (rc == BC_ERR_AUTH) {
    fprintf(stderr, "Authentication failed: %s\n", bc_error(c));
} else if (rc == BC_ERR_TRANSPORT) {
    fprintf(stderr, "Connection error: %s\n", bc_error(c));
} else if (rc != BC_OK) {
    fprintf(stderr, "Error (%d): %s\n", rc, bc_error(c));
}

bc_eval_result_t result;
rc = bc_eval(c, "some-command", &result);
switch (rc) {
    case BC_OK:
        printf("%s", result.stdout_data);
        bc_eval_result_free(&result);
        break;
    case BC_ERR_TIMEOUT:
        fprintf(stderr, "Operation timed out: %s\n", bc_error(c));
        break;
    case BC_ERR_PROTOCOL:
        fprintf(stderr, "Protocol violation: %s\n", bc_error(c));
        break;
    case BC_ERR_SERVER:
        fprintf(stderr, "Server error: %s\n", bc_error(c));
        break;
    default:
        fprintf(stderr, "Error (%d): %s\n", rc, bc_error(c));
        break;
}

bc_close(c);
```

**Java**:

```java
try (BashClient client = BashClient.connect(path)) {
    client.auth(token);
    EvalResult result = client.eval("some-command");
    System.out.println(result.getStdout());
} catch (AuthException e) {
    System.err.println("Authentication failed: " + e.getMessage());
} catch (TimeoutException e) {
    System.err.println("Operation timed out: " + e.getMessage());
} catch (TransportException e) {
    System.err.println("Connection error: " + e.getMessage());
} catch (ProtocolException e) {
    System.err.println("Protocol violation: " + e.getMessage());
} catch (ServerException e) {
    System.err.println("Server error: " + e.getMessage());
} catch (BashClientException e) {
    System.err.println("Client error: " + e.getMessage());
}
```

---

## Concurrency Models

Each binding handles the multiplexed protocol differently based on the
language's concurrency primitives.

### Python: asyncio

The Python binding is fully asynchronous using `asyncio`. A background
`Task` runs the reader loop, which continuously reads NDJSON frames
from the transport and routes them.

```
                                    Reader loop (asyncio.Task)
                                    |
Event loop  ---- read_line() ----> decode_frame()
            |                       |
            |    +--[response]----> channel_queue[ch].put()
            |    |                       |
            |    +--[push event]--> callbacks[ch](event)
            |
            ---- await queue.get() --> channel method returns
```

**Key characteristics**:

- Single-threaded: Everything runs on one event loop
- Non-blocking: All I/O is async (no threads needed)
- Concurrent channels: Multiple `await` calls can be in-flight
  simultaneously via `asyncio.gather()`
- Server-push: Callbacks run on the event loop between awaits
- Timeout: `asyncio.wait_for()` wraps queue gets

**When to use asyncio**: If your application already uses asyncio
(web servers, async frameworks), or if you need concurrent channel
operations without threads.

### TypeScript: Promise / Event Loop

The TypeScript binding uses Node.js Promises and the built-in event
loop. Like Python, a background reader processes incoming frames.

```
                                    Reader (internal)
                                    |
Event loop  ---- readline() -----> JSON.parse()
            |                       |
            |    +--[response]----> resolve(pending Promise)
            |    |
            |    +--[push event]--> emit(eventName, data)
            |
            ---- await promise --> channel method returns
```

**Key characteristics**:

- Single-threaded (Node.js event loop)
- Promise-based: Every channel method returns a `Promise`
- EventEmitter: Server-push events use Node.js EventEmitter pattern
- Concurrent channels: Multiple `await` calls via `Promise.all()`
- Timeout: `setTimeout()` + `Promise.race()` for deadlines

**When to use TypeScript**: Node.js services, CLI tools, web tooling,
or any project already in the Node.js ecosystem.

### C: Blocking + Poll

The C binding uses blocking I/O for request-response operations and a
`poll()` function for server-push events.

```
bc_eval()       --> write request frame
                --> read response frames (blocking)
                --> buffer any push events encountered
                --> return result

bc_poll(ms)     --> check buffered push events
                --> if empty, poll(fd, ms) for new data
                --> read and dispatch push events via callbacks
```

**Key characteristics**:

- Synchronous: All channel methods block until completion
- No background threads: The caller's thread does all I/O
- Push event buffering: Events received during `bc_eval()` are
  buffered and dispatched on next `bc_poll()` call
- Timeout: `poll()` with millisecond timeout on the socket fd
- Not thread-safe: Only one thread may use a `bc_client_t` at a time

**When to use C**: Embedded systems, system-level integration,
performance-critical paths, or when you need minimal overhead and
no runtime dependencies.

### Java: Daemon Thread + BlockingQueue

The Java binding uses a daemon thread for the reader loop and
`BlockingQueue` for channel synchronization.

```
                                    Reader thread (daemon)
                                    |
Main thread                         readLine() --> parseJSON()
    |                               |
    |    +--[response]-----------> channelQueues[ch].put()
    |    |                               |
    |    +--[push event]----------> listeners[ch].forEach(cb)
    |                               (on reader thread)
    |
    +--- channelQueues[ch].poll(timeout) --> method returns
```

**Key characteristics**:

- Multi-threaded: Reader runs on a background daemon thread
- Blocking API: Channel methods block calling thread via `BlockingQueue.poll()`
- Thread-safe: Synchronized internal state, safe for multi-thread use
- Push events: Callbacks invoked on the reader thread (not the caller)
- Timeout: `BlockingQueue.poll(timeout, unit)` for deadlines
- Resource cleanup: `AutoCloseable` for try-with-resources

**When to use Java**: Enterprise applications, JVM ecosystem
integration, Android, or when you need thread-safe concurrent access
from multiple threads.

---

## Choosing a Binding

### Python

**Best for**: Scripting, async workflows, rapid prototyping,
data pipelines, automation.

**Strengths**:
- Fastest time-to-working-code
- Natural async/await for concurrent operations
- Zero dependencies (stdlib only)
- Rich ecosystem for scripting and automation
- Excellent for interactive use (Jupyter, REPL)

**Trade-offs**:
- Runtime performance limited by CPython
- asyncio can be complex for simple scripts
- Single-threaded limits CPU-bound workloads

**Ideal scenarios**:
- DevOps automation scripts
- CI/CD pipeline integration
- Interactive shell management tools
- Jupyter notebook-based system administration
- Rapid prototyping of bash-server integrations

### TypeScript

**Best for**: Node.js services, web tooling, CLI applications,
VS Code extensions.

**Strengths**:
- Static type checking catches errors at compile time
- Native Promise/async support in Node.js
- npm ecosystem for easy distribution
- Excellent for building CLI tools and web services
- First-class EventEmitter support for server-push events

**Trade-offs**:
- Requires Node.js runtime
- Build step (TypeScript compilation)
- Less suitable for system-level integration

**Ideal scenarios**:
- VS Code or editor extensions that manage shell sessions
- Node.js web services with shell backend
- CLI tools distributed via npm
- Build system plugins
- Real-time shell monitoring dashboards

### C

**Best for**: Embedded integration, system-level code, performance-
critical paths, minimal footprint.

**Strengths**:
- Smallest runtime footprint
- No runtime dependencies (pure POSIX)
- Direct integration with C/C++ applications
- Predictable performance (no GC pauses)
- Can be wrapped by FFI from other languages

**Trade-offs**:
- Manual memory management (must call `_free` functions)
- No built-in async -- must use `bc_poll()` for push events
- Not thread-safe (one client per thread)
- More verbose error handling (return codes)

**Ideal scenarios**:
- Embedding bash-server in C/C++ applications
- System daemons that manage shell sessions
- Performance-critical shell automation
- FFI bridge for languages without a native binding
- Resource-constrained environments

### Java

**Best for**: Enterprise applications, JVM ecosystem, Android,
multi-threaded concurrent access.

**Strengths**:
- Thread-safe by design
- Rich type system with generics
- Mature ecosystem (Maven, JUnit, CI integration)
- AutoCloseable for reliable resource cleanup
- Background reader thread handles push events automatically

**Trade-offs**:
- External dependencies (junixsocket for Unix sockets, Jackson for JSON)
- Heavier runtime footprint (JVM)
- More boilerplate than scripting languages

**Ideal scenarios**:
- Enterprise systems integration
- Android applications managing remote shells
- Multi-threaded server applications
- Jenkins/Gradle plugin development
- Large-scale automation platforms on JVM

---

## Performance Considerations

### Latency

For a simple `eval("echo hello")` round-trip (auth already complete):

| Binding      | Typical Latency | Notes                              |
|--------------|-----------------|------------------------------------|
| C            | ~0.3 ms         | Direct syscalls, no overhead       |
| Java         | ~0.5 ms         | Thread synchronization overhead    |
| TypeScript   | ~0.8 ms         | Event loop scheduling              |
| Python       | ~1.0 ms         | asyncio scheduling + CPython       |

These are approximate. Actual latency depends on the command being
evaluated, not the client binding. For commands taking >10ms, the
client overhead is negligible.

### Throughput

For sequential eval operations (back-to-back, no parallelism):

| Binding      | ~Ops/sec | Notes                              |
|--------------|----------|------------------------------------|
| C            | ~3000    | Minimal per-call overhead          |
| Java         | ~2000    | Queue synchronization              |
| TypeScript   | ~1500    | Event loop tick overhead           |
| Python       | ~1000    | asyncio coroutine overhead         |

For parallel eval operations (concurrent channels):

| Binding      | ~Ops/sec | Notes                              |
|--------------|----------|------------------------------------|
| Python       | ~2500    | asyncio.gather excels here         |
| TypeScript   | ~2200    | Promise.all                        |
| Java         | ~2000    | Multiple threads                   |
| C            | ~3000    | Must manually multiplex with poll  |

### Memory Usage

Idle client (connected, authenticated, no pending operations):

| Binding      | ~Memory   | Notes                               |
|--------------|-----------|---------------------------------------|
| C            | ~64 KB    | Client struct + buffers              |
| TypeScript   | ~8 MB     | Node.js baseline                     |
| Python       | ~12 MB    | CPython baseline + asyncio           |
| Java         | ~30 MB    | JVM baseline + reader thread         |

---

## Platform Support Matrix

| Platform          | Python | TypeScript | C      | Java   |
|-------------------|--------|------------|--------|--------|
| Linux (x86_64)    | Full   | Full       | Full   | Full   |
| Linux (aarch64)   | Full   | Full       | Full   | Full   |
| macOS (x86_64)    | Full   | Full       | Full   | Full   |
| macOS (Apple M)   | Full   | Full       | Full   | Full   |
| Cygwin            | Full   | Full       | Full   | Full   |
| FreeBSD           | Full   | Full       | Full   | Full   |
| Windows (native)  | Pipe only | Pipe only | Pipe only | Pipe only |
| WASI              | N/A    | N/A        | N/A    | N/A    |

**Transport support by platform**:

| Transport     | Linux | macOS | Cygwin | Windows | FreeBSD |
|---------------|-------|-------|--------|---------|---------|
| Unix socket   | Yes   | Yes   | Yes    | No      | Yes     |
| stdio         | Yes   | Yes   | Yes    | Yes     | Yes     |
| fd            | Yes   | Yes   | Yes    | No      | Yes     |
| Named Pipe    | No    | No    | Yes    | Yes     | No      |

---

## Dependency Comparison

| Binding      | Runtime Dependencies           | Dev/Test Dependencies             |
|--------------|--------------------------------|-----------------------------------|
| **Python**   | None (stdlib only)             | pytest, pytest-asyncio            |
| **TypeScript** | None (Node.js built-ins)     | jest, ts-jest, @types/node        |
| **C**        | None (POSIX libc only)         | None (test framework is inline)   |
| **Java**     | junixsocket, Jackson           | JUnit 5, Maven Surefire          |

### Why Java Has Dependencies

The Java binding requires external dependencies because:

- **junixsocket**: Java's standard library does not include Unix domain
  socket support until Java 16 (`UnixDomainSocketAddress`). junixsocket
  provides cross-version support from Java 8+.
- **Jackson**: Java's standard library has no JSON parser. Jackson is
  the de facto standard, widely used and well-tested.

If your project already uses these libraries (most JVM projects do),
the dependency cost is zero.

### Reducing Java Dependencies

- If targeting Java 16+, Unix domain sockets can use the JDK directly
  and junixsocket can be excluded.
- If you only use stdio/fd/Named Pipe transports, junixsocket is not
  needed at all.
- Jackson can be replaced with any JSON library by implementing the
  `JsonCodec` interface.
