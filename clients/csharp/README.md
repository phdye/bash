# BashServer.Client — C# client for bash-server

Async C# client library for the bash-server v2 NDJSON protocol.
Targets .NET 6.0+. Zero external dependencies (stdlib only).

## Installation

```sh
cd clients/csharp
dotnet build
```

## Quick Start

```csharp
await using var client = await BashClient.ConnectAsync("/tmp/bash-server-1000/sock");
await client.AuthAsync(token);
var result = await client.EvalAsync("echo hello");
Console.Write(result.Stdout);
```

## API

### Connect

```csharp
// Unix domain socket
var client = await BashClient.ConnectAsync("/tmp/bash-server-1000/sock");

// Subprocess (--stdio mode)
var client = await BashClient.ConnectStdioAsync("bash-server", "--stdio");

// File descriptor
var client = await BashClient.ConnectFdAsync(3);

// Windows Named Pipe
var client = await BashClient.ConnectNamedPipeAsync("bash-server-1000");

// Existing transport
var client = BashClient.FromTransport(myTransport);
```

### Channels

| Channel | Property | Operations |
|---------|----------|------------|
| Control | `client.Control` | `AuthAsync`, `PingAsync`, `ConfigureAsync`, `DisconnectAsync` |
| Command | `client.Command` | `EvalAsync` |
| State | `client.State` | `GetVarAsync`, `SetVarAsync`, `UnsetVarAsync`, `GetFuncAsync`, `UnsetFuncAsync`, `GetAliasAsync`, `SetAliasAsync`, `UnsetAliasAsync`, `SetTrapAsync`, `UnsetTrapAsync`, `InspectAsync` |
| Observe | `client.Observe` | `SubscribeAsync`, `UnsubscribeAsync` + `PreCommand`/`PostCommand` events |
| Debug | `client.Debug` | `EnableAsync`, `DisableAsync`, `StatusAsync`, `AddBreakpointAsync`, `RemoveBreakpointAsync`, `StepAsync`, `NextAsync`, `FinishAsync`, `SkipAsync`, `ContinueAsync`, `InspectAstAsync` + `BreakHit` event |
| PTY | `client.Pty` | `SpawnAsync`, `WriteInputAsync`, `ResizeAsync`, `SignalAsync`, `CloseAsync` + `Output`/`Exit` events |

### Events

```csharp
client.Observe.PreCommand += (sender, e) =>
    Console.WriteLine($"cmd={e.Command} cwd={e.Cwd}");

client.Debug.BreakHit += (sender, e) =>
    Console.WriteLine($"break at line {e.Line}");

client.Pty.Output += (sender, data) =>
    Console.Write(data);
```

### Error Handling

All errors derive from `BashClientException`:

| Exception | Meaning |
|-----------|---------|
| `AuthException` | Authentication failed |
| `ProtocolException` | Malformed frame or message |
| `TimeoutException` | Operation timed out |
| `TransportException` | Socket or I/O error |
| `ServerException` | Server returned an error response |

### Cancellation

All async methods accept an optional `CancellationToken`:

```csharp
using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
var result = await client.EvalAsync("sleep 10", cts.Token);
```

## Testing

```sh
dotnet test
```

Integration tests require `bash-server` in PATH and are skipped otherwise.

## Examples

```sh
cd examples/Eval && dotnet run -- /tmp/bash-server-1000/sock <token> "echo hi"
cd examples/Observe && dotnet run -- /tmp/bash-server-1000/sock <token>
cd examples/Debugger && dotnet run -- /tmp/bash-server-1000/sock <token>
cd examples/Pty && dotnet run -- /tmp/bash-server-1000/sock <token>
```

## Architecture

```
errors → types → protocol → transport → channels → client
```

See `bash-server-client-csharp(7)` for full API documentation.
