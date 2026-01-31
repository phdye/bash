# BASH-SERVER-CLIENT-API(7) -- Client binding overview and unified API

# DESCRIPTION

The bash-server project provides six official client bindings for
interacting with the bash-server v2 NDJSON protocol.  Each binding
implements the same logical API over the same wire protocol, differing
only in language idioms, concurrency model, and memory management
conventions.

This page describes the unified architecture shared by all six
bindings, the common API lifecycle, and the feature matrix that
distinguishes them.  Per-language details are covered in dedicated
man pages.

# CONCEPTS

# Available bindings

Six client bindings are available, each in its own subdirectory under
`clients/`:

| Binding    | Package              | Version | Language   | Directory          |
|------------|----------------------|---------|------------|--------------------|
| Python     | `bashclient`         | 0.1.0   | Python 3.8+| `clients/python`   |
| TypeScript | `bashclient`         | 0.1.0   | Node.js 16+| `clients/typescript`|
| C          | `libbashclient`      | 0.1.0   | C99/POSIX  | `clients/c`        |
| Java       | `org.gnu.bash:bashclient` | 0.1.0 | Java 11+ | `clients/java`     |
| Go         | `bashclient`         | 0.1.0   | Go 1.21+   | `clients/go`       |
| C#         | `BashServer.Client`  | 0.1.0   | .NET 6.0+  | `clients/csharp`   |

All bindings target the v2 NDJSON wire format.  None depend on the
binary v2 frame format -- NDJSON is the standard client-side protocol.

# Dependencies

Each binding is designed with minimal external dependencies:

| Binding    | Runtime deps                    | Test deps                       |
|------------|---------------------------------|---------------------------------|
| Python     | stdlib only (asyncio)           | pytest, pytest-asyncio          |
| TypeScript | stdlib only (net, child_process)| jest, ts-jest                   |
| C          | POSIX libc only                 | none (built-in test harness)    |
| Java       | junixsocket, Jackson            | JUnit 5                         |
| Go         | stdlib only (net, encoding/json)| stdlib testing package           |
| C#         | stdlib only (System.Text.Json)  | xUnit                           |

# Unified architecture

All six bindings follow the same layered architecture:

```
  errors  -->  types  -->  protocol  -->  transport  -->  channels  -->  client
```

**errors** -- Exception/error type hierarchy.  Five error categories
map to the same failure modes across all languages: authentication
failure, protocol violation, operation timeout, transport I/O error,
and server-returned error.

**types** -- Data structures for channel messages.  Each language uses
its native type system (dataclasses, interfaces, structs, POJOs) to
represent `EvalResult`, `VarInfo`, `Breakpoint`, `PreCommandEvent`,
and other protocol objects.

**protocol** -- NDJSON frame encoding and decoding.  Serializes
language-native objects to JSON-line strings and deserializes incoming
lines back to native objects.  Handles base64 encoding for binary
payloads.

**transport** -- Connection management.  Four transport modes are
supported: Unix domain socket, stdio (subprocess), file descriptor,
and Windows Named Pipe.  Each transport provides a line-oriented
read/write interface.

**channels** -- Channel-specific logic.  Six channel classes (one per
v2 channel) encapsulate the request/response patterns and server-push
dispatch for their respective channel.

**client** -- Top-level entry point.  The `BashClient` class ties
everything together: it owns the transport, starts a background reader,
routes incoming messages to channel queues, and exposes convenience
methods for common operations.

# Common API lifecycle

Every client binding follows the same lifecycle:

```
connect  -->  auth  -->  use channels  -->  close
```

1. **Connect** -- Establish a transport connection.  The client provides
   factory methods for each transport mode: `connect()` for Unix socket,
   `connect_stdio()` for subprocess, `connect_fd()` for inherited file
   descriptor, and `connect_named_pipe()` for Windows Named Pipes.

2. **Authenticate** -- Send an `auth` message on CHAN_CONTROL with the
   server token.  The server responds with `auth_ok` or `auth_fail`.
   All channels except CHAN_CONTROL require authentication.

3. **Use channels** -- Interact with the six v2 channels through the
   channel objects exposed on the client (`control`, `command`, `state`,
   `observe`, `debug`, `pty`).  Each channel provides methods for its
   message types.

4. **Close** -- Send a `disconnect` message, cancel the background
   reader, and close the transport.

# Quick comparison

**Python** (async/await):

```python
async with BashClient.connect("/tmp/bash-server-1000/sock") as client:
    await client.auth(token)
    result = await client.eval("echo hello")
    print(result.stdout)
```

**TypeScript** (Promise-based):

```typescript
const client = await BashClient.connect("/tmp/bash-server-1000/sock");
try {
    await client.auth(token);
    const result = await client.eval("echo hello");
    console.log(result.stdout);
} finally {
    await client.close();
}
```

**C** (synchronous blocking):

```c
bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock");
bc_auth(c, token);
bc_eval_result_t result;
bc_eval(c, "echo hello", &result);
printf("%s", result.stdout_data);
bc_eval_result_free(&result);
bc_close(c);
```

**Java** (blocking with try-with-resources):

```java
try (BashClient client = BashClient.connect("/tmp/bash-server-1000/sock")) {
    client.auth(token);
    EvalResult result = client.eval("echo hello");
    System.out.println(result.stdout);
}
```

**Go** (goroutine-based with context):

```go
client, err := bashclient.Connect(ctx, "/tmp/bash-server-1000/sock")
if err != nil { log.Fatal(err) }
defer client.Close()
client.Auth(ctx, token)
result, _ := client.Eval(ctx, "echo hello")
fmt.Print(result.Stdout)
```

**C#** (Task-based async/await):

```csharp
await using var client = await BashClient.ConnectAsync("/tmp/bash-server-1000/sock");
await client.AuthAsync(token);
var result = await client.EvalAsync("echo hello");
Console.Write(result.Stdout);
```

# Feature matrix

| Feature              | Python      | TypeScript  | C             | Java         | Go              | C#              |
|----------------------|-------------|-------------|---------------|--------------|-----------------|-----------------|
| Async model          | asyncio     | Promise     | synchronous   | blocking     | goroutines      | Task/async-await|
| Concurrency          | single-thread + event loop | single-thread + event loop | single-thread | daemon thread | goroutines + channels | thread pool + tasks |
| Threading            | not needed  | not needed  | NOT thread-safe | one reader thread | goroutine-safe | task-safe       |
| Memory management    | GC          | GC          | manual (bc_free) | GC          | GC              | GC              |
| Context manager      | async with  | N/A (manual close) | N/A        | try-with-resources | defer Close() | await using     |
| Callback model       | on/off (sync or async) | EventEmitter-style on/off | function pointers + userdata | Consumer/BiConsumer | function values | C# events       |
| Server-push polling  | automatic (reader task) | automatic (reader loop) | manual bc_poll() | automatic (reader thread) | automatic (reader goroutine) | automatic (reader task) |
| Type system          | dataclasses | interfaces  | structs       | POJOs        | structs + tags  | records/classes |
| Error model          | exceptions  | exceptions  | error codes   | checked exceptions | error interface | exceptions      |
| Timeout control      | asyncio.wait_for | per-call ms | N/A          | hardcoded 30s | context.Context | CancellationToken |
| Platform support     | Linux, macOS, Cygwin, Windows | Linux, macOS, Cygwin | Linux, macOS, Cygwin | Linux, macOS | Linux, macOS, Cygwin, Windows | Linux, macOS, Windows, Cygwin |
| Named Pipe support   | yes         | yes         | yes           | yes          | yes             | yes             |

# Transport modes

All six bindings support the same four transport modes:

**Unix domain socket** -- The default and recommended transport for
production use.  Connects to `AF_UNIX` socket at a path discovered
by the standard resolution order: CLI argument, `$BASH_SERVER_SOCKET`
environment variable, `~/.bash-serverrc` configuration, `$XDG_RUNTIME_DIR`,
or `/tmp/bash-server-<uid>/sock`.

**stdio** -- Launches `bash-server --stdio` as a subprocess and
communicates over stdin/stdout.  Ideal for testing, CI pipelines, and
environments where socket creation is restricted.

**File descriptor** -- Connects via an inherited file descriptor.  Used
in container and systemd integration scenarios where a parent process
pre-opens the connection.

**Named Pipe** -- Windows Named Pipe transport (`\\.\pipe\bash-server-*`).
Available on Cygwin and Windows.  Uses DACL security for access control.

# Channel overview

All bindings expose the same six v2 channels as properties on the
client object:

| Channel      | ID | Property   | Purpose                        |
|--------------|----|------------|--------------------------------|
| CHAN_CONTROL  | 0  | `control`  | Auth, ping, configure, disconnect |
| CHAN_COMMAND  | 1  | `command`  | Command evaluation             |
| CHAN_STATE    | 2  | `state`    | Variable/function/alias/trap ops |
| CHAN_OBSERVE  | 3  | `observe`  | Pre/post command events        |
| CHAN_DEBUG    | 4  | `debug`    | Breakpoints, stepping, AST     |
| CHAN_PTY      | 5  | `pty`      | Pseudo-terminal I/O            |

Channels 0-2 use request/response patterns.  Channels 3-5 also support
server-push messages dispatched to registered callbacks.

# Error hierarchy

All bindings implement the same five error categories, mapped to
language-appropriate constructs:

| Error category | Python         | TypeScript     | C               | Java                    | Go                | C#                  |
|----------------|----------------|----------------|-----------------|-------------------------|-------------------|---------------------|
| Base           | BashClientError| BashClientError| (return codes)  | BashClientException     | BashClientError   | BashClientException |
| Authentication | AuthError      | AuthError      | BC_ERR_AUTH     | AuthException           | *AuthError        | AuthException       |
| Protocol       | ProtocolError  | ProtocolError  | BC_ERR_PROTOCOL | ProtocolException       | *ProtocolError    | ProtocolException   |
| Timeout        | TimeoutError   | TimeoutError   | BC_ERR_TIMEOUT  | TimeoutException        | *TimeoutError     | TimeoutException    |
| Transport      | TransportError | TransportError | BC_ERR_TRANSPORT| TransportException      | *TransportError   | TransportException  |
| Server         | ServerError    | ServerError    | BC_ERR_SERVER   | ServerException         | *ServerError      | ServerException     |

The Python and TypeScript bindings use exception inheritance trees
rooted at `BashClientError`.  The C binding uses integer return codes
(`BC_OK`, `BC_ERR_*`).  The Java binding uses checked exceptions as
static inner classes of `BashClientException`.  The Go binding uses
concrete error types implementing the `error` interface, checked via
`errors.As()`.  The C# binding uses an exception hierarchy rooted at
`BashClientException`.

# Installation

**Python:**

```sh
cd clients/python
pip install -e .
# or: pip install -e ".[dev]"  (with test dependencies)
```

**TypeScript:**

```sh
cd clients/typescript
npm install
npm run build
```

**C:**

```sh
cd clients/c
make
# produces libbashclient.a and libbashclient.so
```

**Java:**

```sh
cd clients/java
mvn package
```

**Go:**

```sh
cd clients/go
go build ./...
```

**C#:**

```sh
cd clients/csharp
dotnet build
```

# Wire protocol

All bindings use the v2 NDJSON wire format exclusively.  Each message
is a single JSON object on one line, terminated by `\n`.  Messages
include a `channel` field (integer 0-5) and a `type` field (string).
Binary payloads (stdout, stderr, PTY I/O) use base64 encoding.

The server auto-detects the protocol version from the first byte of
data received.  NDJSON clients send a `{` as the first byte (the
opening brace of the first JSON message), which the server recognizes
as NDJSON framing.

# Background reader

Five of the six bindings (Python, TypeScript, Java, Go, C#) run a
background reader that continuously reads lines from the transport
and dispatches them:

- **Request/response messages** are placed on per-channel queues.
  The calling method awaits/polls the queue for its response.

- **Server-push messages** (observe events, debug break_hit, PTY
  output/exit) are dispatched directly to registered callbacks.

The C binding does not run a background thread.  Instead, the caller
must periodically invoke `bc_poll()` to process incoming messages,
including both queued responses and callback dispatch.

# Naming conventions

Each binding follows the naming conventions of its host language:

| Concept          | Python           | TypeScript       | C                   | Java             | Go                  | C#                    |
|------------------|------------------|------------------|---------------------|------------------|---------------------|-----------------------|
| Connect          | `connect()`      | `connect()`      | `bc_connect()`      | `connect()`      | `Connect()`         | `ConnectAsync()`      |
| Authenticate     | `auth()`         | `auth()`         | `bc_auth()`         | `auth()`         | `Auth()`            | `AuthAsync()`         |
| Evaluate         | `eval()`         | `eval()`         | `bc_eval()`         | `eval()`         | `Eval()`            | `EvalAsync()`         |
| Get variable     | `state.get_var()`| `state.getVar()` | `bc_state_get_var()`| `state.getVar()` | `State.GetVar()`    | `State.GetVarAsync()` |
| Set breakpoint   | `debug.add_breakpoint()` | `debug.addBreakpoint()` | `bc_debug_add_breakpoint()` | `debug.addBreakpoint()` | `Debug.AddBreakpoint()` | `Debug.AddBreakpointAsync()` |
| Spawn PTY        | `pty.spawn()`    | `pty.spawn()`    | `bc_pty_spawn()`    | `pty.spawn()`    | `Pty.Spawn()`       | `Pty.SpawnAsync()`    |

Python uses `snake_case`, TypeScript and Java use `camelCase`, the
C binding prefixes all functions with `bc_` and uses `snake_case`,
Go uses exported `PascalCase`, and C# uses `PascalCase` with an
`Async` suffix on async methods.

# SEE ALSO

**bash-server-client-python**(7),
**bash-server-client-typescript**(7),
**bash-server-client-c**(7),
**bash-server-client-java**(7),
**bash-server-client-go**(7),
**bash-server-client-csharp**(7),
**bash-server-client-transports**(7),
**bash-server-client-channels**(7),
**bash-server**(1),
**bash-server-channels**(7),
**bash-server-transports**(7)

# AUTHORS

GNU Bash is Copyright (C) Free Software Foundation, Inc.
The bash-server extension was developed as part of the Cygwin Bash project.

# COPYRIGHT

This is free software; see the GNU General Public License v3 or later
for copying conditions.  There is NO warranty.
