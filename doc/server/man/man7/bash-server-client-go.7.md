# BASH-SERVER-CLIENT-GO(7) -- Go client binding for bash-server

# DESCRIPTION

The Go client binding provides a goroutine-based, concurrent interface
to the bash-server v2 NDJSON protocol.  It is implemented as the
`bashclient` Go package, requires Go 1.21 or later, and depends only
on the Go standard library (`net`, `os/exec`, `encoding/json`,
`bufio`).

The binding runs a background goroutine that reads NDJSON lines from
the transport and dispatches them to per-channel response channels and
registered callback functions.  All request/response operations accept
a `context.Context` for cancellation and timeout control.

# CONCEPTS

# Installation

The package is located at `clients/go/` and uses Go modules:

```sh
cd clients/go
go build ./...
```

To run tests:

```sh
go test ./...
```

To use the package in another module:

```go
import "github.com/cygwin/bash-server/clients/go/bashclient"
```

# Package structure

```
bashclient/
    client.go        -- BashClient type, connect, close, auth
    channels.go      -- Six channel types
    control.go       -- ControlChannel (channel 0)
    command.go       -- CommandChannel (channel 1)
    state.go         -- StateChannel (channel 2)
    observe.go       -- ObserveChannel (channel 3)
    debug.go         -- DebugChannel (channel 4)
    pty.go           -- PtyChannel (channel 5)
    errors.go        -- Error type hierarchy
    protocol.go      -- NDJSON encode/decode
    transport.go     -- Four transport implementations
    types.go         -- Exported type definitions
```

# Architecture

The central type is `BashClient`.  It owns a `Transport` interface
value and starts a background reader goroutine at construction time.

The reader goroutine continuously reads NDJSON lines from the
transport and routes them:

- **Request/response messages** are sent on per-channel `chan Message`
  instances.  When a channel method sends a request, it selects on the
  response channel and the context's `Done()` channel, blocking until
  a response arrives or the context expires.

- **Server-push messages** (observe events, debug break_hit, PTY
  output/exit) are dispatched directly to registered callback
  functions via the channel's `dispatch()` method.

Each channel is exposed as a public field:

```go
client.Control   // *ControlChannel  (channel 0)
client.Command   // *CommandChannel  (channel 1)
client.State     // *StateChannel    (channel 2)
client.Observe   // *ObserveChannel  (channel 3)
client.Debug     // *DebugChannel    (channel 4)
client.Pty       // *PtyChannel      (channel 5)
```

# Connection

`bashclient` provides factory functions that return `(*BashClient, error)`:

```go
// Unix domain socket (default)
client, err := bashclient.Connect(ctx, "/tmp/bash-server-1000/sock")

// Subprocess (--stdio mode)
client, err := bashclient.ConnectStdio(ctx, "bash-server", "--stdio")

// Inherited file descriptor
client, err := bashclient.ConnectFd(ctx, 3)

// Windows Named Pipe (Cygwin)
client, err := bashclient.ConnectNamedPipe(ctx, `\\.\pipe\bash-server-myname`)

// From existing transport
client := bashclient.FromTransport(myTransport)
```

All factory functions accept a `context.Context` as the first
parameter.  The context controls the connection timeout but is not
stored -- subsequent operations use their own contexts.

`FromTransport()` takes an already-connected transport and immediately
starts the reader goroutine.  It does not return an error.

# Lifecycle management

Go's `defer` statement provides deterministic cleanup:

```go
client, err := bashclient.Connect(ctx, socketPath)
if err != nil {
    log.Fatal(err)
}
defer client.Close()

err = client.Auth(ctx, token)
if err != nil {
    log.Fatal(err)
}
result, err := client.Eval(ctx, "echo hello")
if err != nil {
    log.Fatal(err)
}
fmt.Println(result.Stdout)
```

The `Close()` method:

1. Signals the reader goroutine to stop via a `done` channel.
2. Sends a `disconnect` message (ignoring errors).
3. Closes the transport.
4. Waits for the reader goroutine to exit.

`Close()` is safe to call multiple times.

# Authentication

```go
err := client.Auth(ctx, token)
```

The token is a 64-character hex string.  Returns `*AuthError` on
failure.  Check authentication status:

```go
if client.IsAuthenticated() {
    // safe to use all channels
}
```

# Type system

Protocol objects are defined as Go structs in `types.go`:

| Type               | Fields                                          |
|--------------------|-------------------------------------------------|
| `Message`          | `Ch`, `Type`, plus arbitrary `map[string]any`   |
| `EvalResult`       | `Stdout`, `Stderr`, `ExitCode`                  |
| `VarInfo`          | `Name`, `Value`, `Attributes`                   |
| `FuncInfo`         | `Name`, `Definition`                            |
| `AliasInfo`        | `Name`, `Value`                                 |
| `TrapInfo`         | `Signal`, `Command`                             |
| `PreCommandEvent`  | `Seq`, `Timestamp`, `Command`, `Cwd`, `LineNumber`, `IsSubshell`, `IsAsync` |
| `PostCommandEvent` | `Seq`, `Timestamp`, `Command`, `ExitStatus`, `SignalNumber`, `DurationMs` |
| `Breakpoint`       | `ID`, `Kind`, `Enabled`, `HitCount`, `Pattern`, `Line`, `Condition` |
| `BreakHitEvent`    | `Line`, `Command`, `Depth`                      |
| `DebugStatus`      | `Active`, `Mode`, `Breakpoints`, `Depth`        |
| `PtyInfo`          | `Rows`, `Cols`, `Pid`, `StripAnsi`              |
| `InspectItem`      | `Name`, `Value`, `Definition`, `Attributes`, `Signal`, `Command` |

Struct fields are PascalCase (exported).  JSON struct tags map to
the `snake_case` wire-format names:

```go
type EvalResult struct {
    Stdout   string `json:"stdout"`
    Stderr   string `json:"stderr"`
    ExitCode int    `json:"exit_code"`
}
```

# Error handling

All errors implement the `error` interface.  A base `BashClientError`
type is embedded in all specific error types:

```
BashClientError (implements error)
    *AuthError          -- Authentication failed
    *ProtocolError      -- Malformed frame or message
    *TimeoutError       -- Context deadline exceeded
    *TransportError     -- Socket or I/O error
    *ServerError        -- Server returned error response
        .Channel        -- Channel that produced the error
```

Standard error handling patterns:

```go
import "errors"

err := client.Auth(ctx, wrongToken)
var authErr *bashclient.AuthError
if errors.As(err, &authErr) {
    log.Printf("bad token: %v", authErr)
}

ctx, cancel := context.WithTimeout(ctx, 5*time.Second)
defer cancel()
result, err := client.Eval(ctx, "sleep 60")
if errors.Is(err, context.DeadlineExceeded) {
    log.Println("command timed out")
}

var srvErr *bashclient.ServerError
if errors.As(err, &srvErr) {
    log.Printf("server error on channel %d: %v", srvErr.Channel, srvErr)
}
```

Timeout errors arise when the provided `context.Context` expires.
The binding does not impose a default timeout -- callers must provide
a context with a deadline when timeout behavior is desired.

# Naming conventions

The Go binding uses exported PascalCase for all public identifiers,
following standard Go conventions:

| Python equivalent    | Go method                      |
|----------------------|--------------------------------|
| `state.get_var()`    | `State.GetVar()`               |
| `state.set_var()`    | `State.SetVar()`               |
| `state.unset_var()`  | `State.UnsetVar()`             |
| `state.get_func()`   | `State.GetFunc()`              |
| `state.get_alias()`  | `State.GetAlias()`             |
| `debug.add_breakpoint()` | `Debug.AddBreakpoint()`   |
| `debug.remove_breakpoint()` | `Debug.RemoveBreakpoint()` |
| `debug.list_breakpoints()` | `Debug.ListBreakpoints()` |
| `debug.inspect_ast()`| `Debug.InspectAST()`           |
| `pty.write_input()`  | `Pty.WriteInput()`             |

Wire-format field names in JSON struct tags retain `snake_case` to
match the protocol (e.g., `json:"exit_code"`, `json:"hit_count"`).

# CONTROL channel (channel 0)

```go
// Authenticate
err := client.Auth(ctx, token)

// Ping
err := client.Ping(ctx)

// Configure
err := client.Control.Configure(ctx, map[string]any{"observe_level": 1})

// Disconnect
err := client.Control.Disconnect(ctx)
```

# COMMAND channel (channel 1)

```go
// Simple eval (convenience on client)
result, err := client.Eval(ctx, "echo hello")
fmt.Println(result.Stdout)    // "hello\n"
fmt.Println(result.Stderr)    // ""
fmt.Println(result.ExitCode)  // 0

// Via channel object
result, err := client.Command.Eval(ctx, "ls -la")
```

The `Eval()` method blocks until all three response messages
(`stdout`, `stderr`, `complete`) have been received, or the context
expires.  stdout and stderr payloads are automatically base64-decoded.

# STATE channel (channel 2)

```go
// Variables
info, err := client.State.GetVar(ctx, "HOME")
fmt.Println(info.Value)        // "/home/user"
fmt.Println(info.Attributes)   // []

err = client.State.SetVar(ctx, "MY_VAR", "hello", []string{"-x"})
err = client.State.UnsetVar(ctx, "MY_VAR")

// Functions
funcInfo, err := client.State.GetFunc(ctx, "my_function")
fmt.Println(funcInfo.Definition)

err = client.State.UnsetFunc(ctx, "my_function")

// Aliases
aliasInfo, err := client.State.GetAlias(ctx, "ll")
err = client.State.SetAlias(ctx, "ll", "ls -la --color")
err = client.State.UnsetAlias(ctx, "ll")

// Traps
err = client.State.SetTrap(ctx, "SIGINT", "echo interrupted")
err = client.State.UnsetTrap(ctx, "SIGINT")

// Inspect
items, err := client.State.Inspect(ctx, "variables")
```

# OBSERVE channel (channel 3)

```go
// Subscribe
err := client.Observe.Subscribe(ctx, 1)

// Register callbacks (function values)
client.Observe.OnPreCommand(func(event bashclient.PreCommandEvent) {
    fmt.Printf("[%d] running: %s in %s\n", event.Seq, event.Command, event.Cwd)
})

client.Observe.OnPostCommand(func(event bashclient.PostCommandEvent) {
    fmt.Printf("[%d] done: exit=%d duration=%dms\n",
        event.Seq, event.ExitStatus, event.DurationMs)
})

// Execute commands -- callbacks fire on reader goroutine
result, _ := client.Eval(ctx, "echo hello")

// Unregister (pass nil)
client.Observe.OnPreCommand(nil)
client.Observe.OnPostCommand(nil)

// Unsubscribe
err = client.Observe.Unsubscribe(ctx)
```

# DEBUG channel (channel 4)

```go
// Enable
err := client.Debug.Enable(ctx)

// Status
status, err := client.Debug.Status(ctx)
fmt.Printf("active=%t mode=%s\n", status.Active, status.Mode)

// Add breakpoints
bpID, err := client.Debug.AddBreakpoint(ctx, "command", &bashclient.BreakpointOpts{
    Pattern: "echo*",
})
bpID2, err := client.Debug.AddBreakpoint(ctx, "line", &bashclient.BreakpointOpts{
    Line: 10,
})

// List breakpoints
bps, err := client.Debug.ListBreakpoints(ctx)
for _, bp := range bps {
    fmt.Printf("#%d %s hits=%d\n", bp.ID, bp.Kind, bp.HitCount)
}

// Break hit callback
client.Debug.OnBreakHit(func(event bashclient.BreakHitEvent) {
    fmt.Printf("break at line %d: %s\n", event.Line, event.Command)
})

// Execution control
err = client.Debug.Continue(ctx)
err = client.Debug.Step(ctx)
err = client.Debug.Next(ctx)
err = client.Debug.Finish(ctx)
err = client.Debug.Skip(ctx)

// Inspect AST
ast, err := client.Debug.InspectAST(ctx)

// Remove breakpoint
err = client.Debug.RemoveBreakpoint(ctx, bpID)

// Enable/disable individual breakpoints
err = client.Debug.DisableBreakpoint(ctx, bpID2)
err = client.Debug.EnableBreakpoint(ctx, bpID2)

// Disable debugger
err = client.Debug.Disable(ctx)
```

Note that Go uses `Continue()` (not a reserved keyword in Go, unlike
Java's `continue`).

# PTY channel (channel 5)

```go
// Spawn
info, err := client.Pty.Spawn(ctx, &bashclient.PtyOpts{
    Rows:      24,
    Cols:      80,
    StripAnsi: true,
})
fmt.Printf("pid=%d\n", info.Pid)

// Output callback
client.Pty.OnOutput(func(data string) {
    fmt.Print(data)
})

client.Pty.OnExit(func(exitCode int) {
    fmt.Printf("\nPTY exited: %d\n", exitCode)
})

// Write input
err = client.Pty.WriteInput(ctx, "ls -la\n")

// Resize
err = client.Pty.Resize(ctx, 48, 120)

// Signal
err = client.Pty.Signal(ctx, "SIGINT")

// Close
err = client.Pty.Close(ctx)
```

# Callback model

The observe, debug, and PTY channels use typed callback functions:

```go
// Register
client.Observe.OnPreCommand(func(e bashclient.PreCommandEvent) { ... })
client.Observe.OnPostCommand(func(e bashclient.PostCommandEvent) { ... })
client.Debug.OnBreakHit(func(e bashclient.BreakHitEvent) { ... })
client.Pty.OnOutput(func(data string) { ... })
client.Pty.OnExit(func(exitCode int) { ... })

// Unregister (pass nil)
client.Observe.OnPreCommand(nil)
```

Callbacks are invoked on the reader goroutine.  Only one callback per
event type is active at a time.  Setting a new callback replaces the
previous one.  Pass `nil` to unregister.

Callbacks should be non-blocking.  If heavy processing is needed,
send the event to a separate goroutine via a channel:

```go
events := make(chan bashclient.PostCommandEvent, 100)
client.Observe.OnPostCommand(func(e bashclient.PostCommandEvent) {
    select {
    case events <- e:
    default: // drop if full
    }
})
go func() {
    for e := range events {
        // heavy processing
    }
}()
```

# Timeout handling

The Go binding does not impose a default timeout.  All
request/response operations accept a `context.Context` parameter.
Use `context.WithTimeout` or `context.WithDeadline` to control
timeouts:

```go
// 10-second timeout for eval
ctx, cancel := context.WithTimeout(ctx, 10*time.Second)
defer cancel()
result, err := client.Eval(ctx, "slow-command")
if err != nil {
    // may be context.DeadlineExceeded
}

// No timeout (blocks until response or client close)
result, err := client.Eval(context.Background(), "echo hello")
```

When the context expires, the pending select unblocks via the
context's `Done()` channel, and a `*TimeoutError` wrapping
`context.DeadlineExceeded` is returned.

# Concurrency safety

The `BashClient` is safe for concurrent use from multiple goroutines
with the following caveats:

- Different channels can be used concurrently (each has its own
  response channel).
- Two goroutines calling the same channel concurrently may receive
  each other's responses -- avoid this pattern.
- Callbacks run on the reader goroutine, so blocking in a callback
  stalls all message dispatch.
- `Close()` is safe to call from any goroutine and will unblock
  any pending operations.

# Properties

```go
client.IsConnected()      // true if transport is open
client.IsAuthenticated()  // true if Auth() succeeded
```

# Constants

```go
const (
    ChanControl      = 0
    ChanCommand      = 1
    ChanState        = 2
    ChanObserve      = 3
    ChanDebug        = 4
    ChanPty          = 5
    ChanMax          = 5
    FrameMaxPayload  = 1048576
    TokenHexLen      = 64
    ObserveLevelOff     = 0
    ObserveLevelCommand = 1
)
```

# Complete example

```go
package main

import (
    "context"
    "fmt"
    "log"
    "time"

    "github.com/cygwin/bash-server/clients/go/bashclient"
)

func main() {
    ctx := context.Background()

    client, err := bashclient.Connect(ctx, "/tmp/bash-server-1000/sock")
    if err != nil {
        log.Fatal(err)
    }
    defer client.Close()

    // Authenticate
    if err := client.Auth(ctx, "abcdef0123456789..."); err != nil {
        log.Fatal(err)
    }

    // Simple command
    result, err := client.Eval(ctx, "echo hello world")
    if err != nil {
        log.Fatal(err)
    }
    fmt.Print(result.Stdout)

    // Set and read a variable
    if err := client.State.SetVar(ctx, "GREETING", "hello", nil); err != nil {
        log.Fatal(err)
    }
    info, err := client.State.GetVar(ctx, "GREETING")
    if err != nil {
        log.Fatal(err)
    }
    fmt.Println(info.Value) // "hello"

    // Observe
    var events []bashclient.PostCommandEvent
    client.Observe.OnPostCommand(func(e bashclient.PostCommandEvent) {
        events = append(events, e)
    })
    if err := client.Observe.Subscribe(ctx, 1); err != nil {
        log.Fatal(err)
    }
    client.Eval(ctx, "true")

    // Brief pause for push events
    time.Sleep(100 * time.Millisecond)
    fmt.Printf("Captured %d events\n", len(events))
}
```

# Comparison with other bindings

The Go binding is most similar to the Python binding in its
concurrency model (background reader, non-blocking dispatch), but
uses goroutines and channels instead of an event loop:

| Aspect           | Go                        | Python                  |
|------------------|---------------------------|-------------------------|
| Concurrency      | goroutines + channels     | asyncio event loop      |
| Timeout control  | context.Context           | asyncio.wait_for()      |
| Cleanup          | defer client.Close()      | async with              |
| Errors           | error interface + As()    | exception hierarchy     |
| Dependencies     | stdlib only               | stdlib only             |
| Push events      | automatic (reader goroutine) | automatic (reader task) |

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
