# bashclient (TypeScript)

TypeScript/Node.js client for the bash-server v2 NDJSON protocol.

## Installation

```bash
npm install bashclient
```

## Quick Start

```typescript
import { BashClient } from "bashclient";

const client = await BashClient.connect("/tmp/bash-server-1000/sock");
await client.auth("your-64-char-hex-token");
const result = await client.eval("echo hello");
console.log(result.stdout); // "hello\n"
await client.close();
```

## Transports

```typescript
// Unix socket
const client = await BashClient.connect("/path/to/sock");

// Subprocess (--stdio mode)
const client = await BashClient.connectStdio("bash-server", "--stdio");

// Inherited fd
const client = await BashClient.connectFd(3);

// Windows Named Pipe
const client = await BashClient.connectNamedPipe("bash-server-pipe");
```

## Channels

### COMMAND

```typescript
const result = await client.eval("ls -la");
// { stdout: "...", stderr: "", exit_code: 0 }
```

### STATE

```typescript
const v = await client.state.getVar("PATH");
await client.state.setVar("MY_VAR", "value", ["exported"]);
await client.state.unsetVar("MY_VAR");

const f = await client.state.getFunc("my_function");
const a = await client.state.getAlias("ll");
await client.state.setAlias("ll", "ls -lah");

await client.state.setTrap("SIGINT", "echo caught");
const items = await client.state.inspect("vars");
```

### OBSERVE

```typescript
client.observe.on("pre_command", (e) => console.log(e.command));
client.observe.on("post_command", (e) => console.log(e.exit_status));
await client.observe.subscribe(1);
```

### DEBUG

```typescript
await client.debug.enable();
const bpId = await client.debug.addBreakpoint({ kind: "command", pattern: "echo" });
client.debug.on("break_hit", async (e) => {
  const ast = await client.debug.inspectAst();
  await client.debug.continue_();
});
```

### PTY

```typescript
client.pty.on("output", (data) => process.stdout.write(data));
client.pty.on("exit", (code) => console.log(`Exit: ${code}`));
const info = await client.pty.spawn({ rows: 24, cols: 80 });
await client.pty.writeInput("echo hello\n");
await client.pty.close();
```

## Error Handling

```typescript
import { AuthError, ProtocolError, TimeoutError, TransportError, ServerError } from "bashclient";
```

## Requirements

- Node.js 16+
- No external dependencies (Node.js built-ins only)

## Development

```bash
npm install
npm test
npm run build
```

## Documentation

| Document | Description |
|----------|-------------|
| [INSTALL.md](INSTALL.md) | Detailed installation, platform notes, verification |
| [GUIDE.md](GUIDE.md) | Comprehensive usage guide — all transports, channels, patterns |
| [API.md](API.md) | Full API reference — every class, method, interface, constant |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Internals for contributors — module structure, message routing |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Dev setup, code style, testing, PR process |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | Common issues, connection failures, debugging tips |
| [CHANGELOG.md](CHANGELOG.md) | Version history |
| [api-metadata.json](api-metadata.json) | Machine-readable API index for AI tools |
| [examples/](examples/README.md) | Annotated examples with expected output |

### See Also

- [Client bindings overview](../README.md) — all 4 language bindings
- [Cross-binding comparison](../COMPARISON.md) — choosing a language
- [Protocol reference](../PROTOCOL.md) — v2 NDJSON wire format
- [Testing guide](../TESTING.md) — testing philosophy and patterns
- [bash-server-client-typescript(7)](../../doc/server/man/man7/bash-server-client-typescript.7.md) — man page overview
- [Server documentation](../../doc/server/README.md) — bash-server internals
