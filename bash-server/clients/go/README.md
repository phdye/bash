# bashclient - Go client for bash-server

Go client library for the bash-server v2 NDJSON protocol.

## Requirements

- Go 1.21+
- No external dependencies (stdlib only)

## Installation

```go
import "github.com/cygwin/bash-server/clients/go/bashclient"
```

## Quick Start

```go
ctx := context.Background()
client, err := bashclient.Connect(ctx, "/tmp/bash-server-1000/sock")
if err != nil {
    log.Fatal(err)
}
defer client.Close()

if err := client.Auth(ctx, token); err != nil {
    log.Fatal(err)
}

result, err := client.Eval(ctx, "echo hello")
if err != nil {
    log.Fatal(err)
}
fmt.Print(result.Stdout)
```

## Build & Test

```sh
go build ./...
go test ./bashclient/...
go test -tags integration ./bashclient/...  # requires bash-server in PATH
```

## Transport Modes

- **Unix socket**: `bashclient.Connect(ctx, path)`
- **Stdio**: `bashclient.ConnectStdio(ctx, "bash-server", "--stdio")`
- **File descriptor**: `bashclient.ConnectFd(ctx, fd)`
- **Named Pipe**: `bashclient.ConnectNamedPipe(ctx, pipeName)`

## Channels

Access via `client.Control`, `client.Command`, `client.State`,
`client.Observe`, `client.Debug`, `client.Pty`.

## Documentation

See `bash-server-client-go(7)` man page for complete API reference.
