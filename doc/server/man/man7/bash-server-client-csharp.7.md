# BASH-SERVER-CLIENT-CSHARP(7) -- C# client binding for bash-server

# DESCRIPTION

The C# client binding provides a `Task`-based async/await interface to
the bash-server v2 NDJSON protocol.  It is implemented as the
`BashServer.Client` NuGet package, targets .NET 6.0 or later, and
depends only on the .NET standard library
(`System.Net.Sockets`, `System.IO.Pipes`, `System.Text.Json`).

The binding uses a background `Task` that reads NDJSON lines from
the transport and dispatches them to per-channel
`TaskCompletionSource` instances and registered event handlers.
All request/response operations are async and accept a
`CancellationToken` for cancellation and timeout control.

# CONCEPTS

# Installation

The package is located at `clients/csharp/` and uses the .NET CLI:

```sh
cd clients/csharp
dotnet build
```

To run tests:

```sh
dotnet test
```

To create a NuGet package:

```sh
dotnet pack
```

# NuGet package

```xml
<PackageReference Include="BashServer.Client" Version="0.1.0" />
```

Or via the CLI:

```sh
dotnet add package BashServer.Client
```

# Package structure

```
BashServer.Client/
    BashClient.cs        -- Main client class (IAsyncDisposable)
    Channels/
        ControlChannel.cs    -- Channel 0
        CommandChannel.cs    -- Channel 1
        StateChannel.cs      -- Channel 2
        ObserveChannel.cs    -- Channel 3
        DebugChannel.cs      -- Channel 4
        PtyChannel.cs        -- Channel 5
    Errors/
        BashClientException.cs   -- Exception hierarchy
    Protocol/
        NdjsonProtocol.cs    -- NDJSON encode/decode
    Transport/
        ITransport.cs        -- Transport interface
        UnixSocketTransport.cs
        StdioTransport.cs
        FdTransport.cs
        NamedPipeTransport.cs
    Types/
        EvalResult.cs        -- Command result
        VarInfo.cs           -- Variable info
        BreakHitEvent.cs     -- Debugger break event
        PtyInfo.cs           -- PTY session info
        ...
```

# Architecture

The central class is `BashClient`, which implements
`IAsyncDisposable`.  It owns an `ITransport` instance and starts a
background reader task at construction time.

The reader task continuously reads NDJSON lines from the transport
and routes them:

- **Request/response messages** complete per-channel
  `TaskCompletionSource<JsonElement>` instances.  When a channel
  method sends a request, it creates a new `TaskCompletionSource`,
  registers it, and returns its `Task`.  The reader completes the
  source when the response arrives.

- **Server-push messages** (observe events, debug break_hit, PTY
  output/exit) are dispatched directly to registered event handlers
  via C# events.

Each channel is exposed as a public readonly property:

```csharp
client.Control   // ControlChannel  (channel 0)
client.Command   // CommandChannel  (channel 1)
client.State     // StateChannel    (channel 2)
client.Observe   // ObserveChannel  (channel 3)
client.Debug     // DebugChannel    (channel 4)
client.Pty       // PtyChannel      (channel 5)
```

# Connection

`BashClient` provides static async factory methods:

```csharp
// Unix domain socket (default)
var client = await BashClient.ConnectAsync("/tmp/bash-server-1000/sock");

// Subprocess (--stdio mode)
var client = await BashClient.ConnectStdioAsync("bash-server", "--stdio");

// Inherited file descriptor
var client = await BashClient.ConnectFdAsync(3);

// Windows Named Pipe (Cygwin)
var client = await BashClient.ConnectNamedPipeAsync(@"\\.\pipe\bash-server-myname");

// From existing transport
var client = BashClient.FromTransport(myTransport);
```

All async factory methods accept an optional `CancellationToken`
parameter.  `FromTransport()` is synchronous -- it takes an
already-connected transport and immediately starts the reader task.

# Lifecycle management

C# provides `IAsyncDisposable` with `await using` for deterministic
async cleanup:

```csharp
await using var client = await BashClient.ConnectAsync(socketPath);
await client.AuthAsync(token);
var result = await client.EvalAsync("echo hello");
Console.WriteLine(result.Stdout);
// DisposeAsync() called automatically at end of scope
```

For environments where `await using` is not available, use
`try`/`finally`:

```csharp
var client = await BashClient.ConnectAsync(socketPath);
try
{
    await client.AuthAsync(token);
    var result = await client.EvalAsync("echo hello");
    Console.WriteLine(result.Stdout);
}
finally
{
    await client.DisposeAsync();
}
```

The `DisposeAsync()` method:

1. Cancels the reader task via a `CancellationTokenSource`.
2. Sends a `disconnect` message (ignoring errors).
3. Closes the transport.
4. Awaits the reader task to ensure clean shutdown.

`DisposeAsync()` is safe to call multiple times.

# Authentication

```csharp
await client.AuthAsync(token);
```

The token is a 64-character hex string.  Throws `AuthException` on
failure.  Check authentication status:

```csharp
if (client.IsAuthenticated)
{
    // safe to use all channels
}
```

# Type system

Protocol objects are defined as C# record types (or classes with
init-only properties) in the `BashServer.Client.Types` namespace:

| Type               | Fields                                          |
|--------------------|-------------------------------------------------|
| `Message`          | `Ch`, `Type`, plus `JsonElement` extras          |
| `EvalResult`       | `Stdout`, `Stderr`, `ExitCode`                  |
| `VarInfo`          | `Name`, `Value`, `Attributes`                   |
| `FuncInfo`         | `Name`, `Definition`                            |
| `AliasInfo`        | `Name`, `Value`                                 |
| `TrapInfo`         | `Signal`, `Command`                             |
| `PreCommandEvent`  | `Seq`, `Timestamp`, `Command`, `Cwd`, `LineNumber`, `IsSubshell`, `IsAsync` |
| `PostCommandEvent` | `Seq`, `Timestamp`, `Command`, `ExitStatus`, `SignalNumber`, `DurationMs` |
| `Breakpoint`       | `Id`, `Kind`, `Enabled`, `HitCount`, `Pattern`, `Line`, `Condition` |
| `BreakHitEvent`    | `Line`, `Command`, `Depth`                      |
| `DebugStatus`      | `Active`, `Mode`, `Breakpoints`, `Depth`        |
| `PtyInfo`          | `Rows`, `Cols`, `Pid`, `StripAnsi`              |
| `InspectItem`      | `Name`, `Value`, `Definition`, `Attributes`, `Signal`, `Command` |

Properties are PascalCase (C# convention).  JSON serialization uses
`System.Text.Json` with `JsonPropertyName` attributes to map to the
`snake_case` wire-format names:

```csharp
public record EvalResult(
    [property: JsonPropertyName("stdout")] string Stdout,
    [property: JsonPropertyName("stderr")] string Stderr,
    [property: JsonPropertyName("exit_code")] int ExitCode
);
```

# Error handling

All errors derive from `BashClientException`, which extends
`Exception`:

```
BashClientException
    AuthException          -- Authentication failed
    ProtocolException      -- Malformed frame or message
    TimeoutException       -- Operation timed out / cancelled
    TransportException     -- Socket or I/O error
    ServerException        -- Server returned error response
        .Channel           -- Channel that produced the error
```

Standard `try`/`catch` patterns:

```csharp
try
{
    await client.AuthAsync(wrongToken);
}
catch (AuthException)
{
    Console.Error.WriteLine("bad token");
}

using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
try
{
    var result = await client.EvalAsync("sleep 60", cts.Token);
}
catch (TimeoutException)
{
    Console.Error.WriteLine("command timed out");
}
catch (OperationCanceledException)
{
    Console.Error.WriteLine("operation was cancelled");
}

try
{
    await client.State.GetVarAsync("NONEXISTENT");
}
catch (ServerException ex)
{
    Console.Error.WriteLine($"server error on channel {ex.Channel}");
}
```

`TimeoutException` is thrown when a `CancellationToken` expires.
`OperationCanceledException` is thrown when the caller explicitly
cancels the token.  Both can be caught together if the distinction
is not needed.

# Naming conventions

The C# binding uses PascalCase for all public members and appends
`Async` to async method names, following standard .NET conventions:

| Python equivalent    | C# method                        |
|----------------------|----------------------------------|
| `state.get_var()`    | `State.GetVarAsync()`            |
| `state.set_var()`    | `State.SetVarAsync()`            |
| `state.unset_var()`  | `State.UnsetVarAsync()`          |
| `state.get_func()`   | `State.GetFuncAsync()`           |
| `state.get_alias()`  | `State.GetAliasAsync()`          |
| `debug.add_breakpoint()` | `Debug.AddBreakpointAsync()` |
| `debug.remove_breakpoint()` | `Debug.RemoveBreakpointAsync()` |
| `debug.list_breakpoints()` | `Debug.ListBreakpointsAsync()` |
| `debug.inspect_ast()`| `Debug.InspectAstAsync()`        |
| `pty.write_input()`  | `Pty.WriteInputAsync()`          |

Wire-format field names in `JsonPropertyName` attributes retain
`snake_case` to match the JSON protocol.

# CONTROL channel (channel 0)

```csharp
// Authenticate
await client.AuthAsync(token);

// Ping
await client.PingAsync();

// Configure
await client.Control.ConfigureAsync(new { observe_level = 1 });

// Disconnect
await client.Control.DisconnectAsync();
```

# COMMAND channel (channel 1)

```csharp
// Simple eval (convenience on client)
var result = await client.EvalAsync("echo hello");
Console.WriteLine(result.Stdout);    // "hello\n"
Console.WriteLine(result.Stderr);    // ""
Console.WriteLine(result.ExitCode);  // 0

// With cancellation token
using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(10));
var result2 = await client.EvalAsync("sleep 2 && echo done", cts.Token);

// Via channel object
var result3 = await client.Command.EvalAsync("ls -la");
```

The `EvalAsync()` method completes when all three response messages
(`stdout`, `stderr`, `complete`) have been received.  stdout and
stderr payloads are automatically base64-decoded.

# STATE channel (channel 2)

```csharp
// Variables
var info = await client.State.GetVarAsync("HOME");
Console.WriteLine(info.Value);        // "/home/user"
Console.WriteLine(info.Attributes);   // []

await client.State.SetVarAsync("MY_VAR", "hello", new[] { "-x" });
await client.State.UnsetVarAsync("MY_VAR");

// Functions
var func = await client.State.GetFuncAsync("my_function");
Console.WriteLine(func.Definition);

await client.State.UnsetFuncAsync("my_function");

// Aliases
var alias = await client.State.GetAliasAsync("ll");
await client.State.SetAliasAsync("ll", "ls -la --color");
await client.State.UnsetAliasAsync("ll");

// Traps
await client.State.SetTrapAsync("SIGINT", "echo interrupted");
await client.State.UnsetTrapAsync("SIGINT");

// Inspect
var items = await client.State.InspectAsync("variables");
```

# OBSERVE channel (channel 3)

```csharp
// Subscribe
await client.Observe.SubscribeAsync(1);

// Register event handlers (C# events)
client.Observe.PreCommand += (sender, e) =>
{
    Console.WriteLine($"[{e.Seq}] running: {e.Command} in {e.Cwd}");
};

client.Observe.PostCommand += (sender, e) =>
{
    Console.WriteLine($"[{e.Seq}] done: exit={e.ExitStatus} duration={e.DurationMs}ms");
};

// Execute commands -- handlers fire automatically
await client.EvalAsync("echo hello");

// Unsubscribe
await client.Observe.UnsubscribeAsync();
```

Event handlers follow the standard .NET `EventHandler<T>` pattern.
They are invoked on the reader task's thread pool context.

# DEBUG channel (channel 4)

```csharp
// Enable
await client.Debug.EnableAsync();

// Status
var status = await client.Debug.StatusAsync();
Console.WriteLine($"active={status.Active} mode={status.Mode}");

// Add breakpoints
int bpId = await client.Debug.AddBreakpointAsync("command", pattern: "echo*");
int bpId2 = await client.Debug.AddBreakpointAsync("line", line: 10);

// List breakpoints
var bps = await client.Debug.ListBreakpointsAsync();
foreach (var bp in bps)
{
    Console.WriteLine($"#{bp.Id} {bp.Kind} hits={bp.HitCount}");
}

// Break hit event
client.Debug.BreakHit += (sender, e) =>
{
    Console.WriteLine($"break at line {e.Line}: {e.Command}");
};

// Execution control
await client.Debug.ContinueAsync();
await client.Debug.StepAsync();
await client.Debug.NextAsync();
await client.Debug.FinishAsync();
await client.Debug.SkipAsync();

// Inspect AST
var ast = await client.Debug.InspectAstAsync();

// Remove / toggle breakpoints
await client.Debug.RemoveBreakpointAsync(bpId);
await client.Debug.DisableBreakpointAsync(bpId2);
await client.Debug.EnableBreakpointAsync(bpId2);

// Disable debugger
await client.Debug.DisableAsync();
```

Note that `ContinueAsync()` is used instead of `continue()` because
`continue` is a reserved keyword in C#.

# PTY channel (channel 5)

```csharp
// Spawn
var info = await client.Pty.SpawnAsync(new PtyOpts
{
    Rows = 24,
    Cols = 80,
    StripAnsi = true
});
Console.WriteLine($"pid={info.Pid}");

// Output events
client.Pty.Output += (sender, data) =>
{
    Console.Write(data);
};

client.Pty.Exit += (sender, exitCode) =>
{
    Console.WriteLine($"\nPTY exited: {exitCode}");
};

// Write input
await client.Pty.WriteInputAsync("ls -la\n");

// Resize
await client.Pty.ResizeAsync(48, 120);

// Signal
await client.Pty.SignalAsync("SIGINT");

// Close
await client.Pty.CloseAsync();
```

# Callback model

The C# binding uses standard .NET events and delegates for
server-push notifications:

```csharp
// Observe events
client.Observe.PreCommand += handler;    // EventHandler<PreCommandEvent>
client.Observe.PostCommand += handler;   // EventHandler<PostCommandEvent>

// Debug events
client.Debug.BreakHit += handler;        // EventHandler<BreakHitEvent>

// PTY events
client.Pty.Output += handler;            // EventHandler<string>
client.Pty.Exit += handler;              // EventHandler<int>

// Unregister
client.Observe.PreCommand -= handler;
```

Events follow standard .NET conventions:

- Multiple handlers per event are supported.
- Handlers are invoked in registration order.
- Handlers run on a thread pool thread (from the reader task).
- Keep handlers fast and non-blocking to avoid stalling dispatch.
- For heavy processing, use `Task.Run()` inside the handler.

# Timeout handling

The C# binding does not impose a default timeout.  All async
operations accept an optional `CancellationToken` parameter.  Use
`CancellationTokenSource` with a timeout for deadline-based control:

```csharp
// 10-second timeout for eval
using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(10));
var result = await client.EvalAsync("slow-command", cts.Token);

// No timeout (completes when response arrives or client is disposed)
var result2 = await client.EvalAsync("echo hello");
```

When the token is cancelled, the pending `TaskCompletionSource` is
completed with `OperationCanceledException`, which the channel
method wraps in `TimeoutException` if the token expired due to a
timeout.

# Concurrency safety

The `BashClient` is safe for concurrent use from multiple `Task`
contexts with the following caveats:

- Different channels can be used concurrently (each has its own
  response queue).
- Two tasks awaiting the same channel concurrently may receive
  each other's responses -- avoid this pattern.
- Event handlers run on a thread pool thread, so they must be
  thread-safe if accessing shared state.
- `DisposeAsync()` is safe to call from any context and will
  cancel all pending operations.

# Properties

```csharp
client.IsConnected      // true if transport is open
client.IsAuthenticated  // true if AuthAsync() succeeded
```

# Constants

```csharp
public static class Channels
{
    public const int Control     = 0;
    public const int Command     = 1;
    public const int State       = 2;
    public const int Observe     = 3;
    public const int Debug       = 4;
    public const int Pty         = 5;
    public const int Max         = 5;
}

public const int FrameMaxPayload     = 1_048_576;
public const int TokenHexLen         = 64;
public const int ObserveLevelOff     = 0;
public const int ObserveLevelCommand = 1;
```

# Complete example

```csharp
using BashServer.Client;
using BashServer.Client.Types;

await using var client = await BashClient.ConnectAsync(
    "/tmp/bash-server-1000/sock");

// Authenticate
await client.AuthAsync("abcdef0123456789...");

// Simple command
var result = await client.EvalAsync("echo hello world");
Console.Write(result.Stdout);

// Set and read a variable
await client.State.SetVarAsync("GREETING", "hello");
var info = await client.State.GetVarAsync("GREETING");
Console.WriteLine(info.Value);  // "hello"

// Observe
var events = new List<PostCommandEvent>();
client.Observe.PostCommand += (s, e) => events.Add(e);
await client.Observe.SubscribeAsync(1);
await client.EvalAsync("true");

// Brief pause for push events
await Task.Delay(100);
Console.WriteLine($"Captured {events.Count} events");
```

# Comparison with other bindings

The C# binding is most similar to the TypeScript binding in its
async model (both use a promise/task-based approach with a background
reader), but uses .NET events instead of an EventEmitter:

| Aspect           | C#                          | TypeScript                |
|------------------|-----------------------------|---------------------------|
| Async model      | Task + async/await          | Promise + async/await     |
| Cancellation     | CancellationToken           | manual timeout per-call   |
| Cleanup          | await using (IAsyncDisposable) | try/finally + close()  |
| Errors           | exception hierarchy         | exception hierarchy       |
| Dependencies     | stdlib only                 | stdlib only               |
| Push events      | C# events (EventHandler)   | EventEmitter on/off       |
| Named Pipes      | NamedPipeClientStream       | Node.js net module        |

# Platform support

The C# binding supports:

- **Linux** -- Unix domain sockets via `Socket` +
  `UnixDomainSocketEndPoint` (.NET 6+).
- **macOS** -- Same as Linux.
- **Windows** -- Named Pipes via `NamedPipeClientStream`.  Unix
  sockets also work on Windows 10+ with AF_UNIX support.
- **Cygwin** -- Named Pipe transport recommended for interop with
  Cygwin bash-server.

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
