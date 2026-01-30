# bash-server Client Libraries

Native client libraries for the bash-server v2 NDJSON protocol.

## Bindings

| Language | Directory | Min Version | Dependencies | Build |
|----------|-----------|-------------|--------------|-------|
| **Python** | `python/` | Python 3.8+ | stdlib only | `pip install -e .` |
| **TypeScript** | `typescript/` | Node.js 16+ | stdlib only | `npm install && npm run build` |
| **C** | `c/` | C99, POSIX | none | `make` |
| **Java** | `java/` | Java 11+ | junixsocket, Jackson | `mvn package` |

## Protocol

All bindings implement the bash-server **v2 NDJSON** wire protocol:

- **Format**: One JSON object per `\n`-terminated line
- **Channel routing**: `"ch"` field (0=CONTROL, 1=COMMAND, 2=STATE, 3=OBSERVE, 4=DEBUG, 5=PTY)
- **Binary data**: base64-encoded in JSON string fields
- **Auth**: `{"ch":0,"type":"auth","token":"<64-hex>"}` → `{"ch":0,"type":"auth_ok",...}`
- **Max payload**: 1MB per frame

## Transports

All 4 transport modes are supported across all bindings:

| Transport | Description | Use case |
|-----------|-------------|----------|
| **Unix socket** | `connect(path)` | Default, production use |
| **stdio** | `connect_stdio(cmd...)` | Subprocess, testing |
| **fd** | `connect_fd(fd)` | Inherited file descriptor |
| **Named Pipe** | `connect_named_pipe(name)` | Windows/Cygwin |

## Channels

| Channel | ID | Operations |
|---------|----|------------|
| **CONTROL** | 0 | auth, ping, configure, disconnect |
| **COMMAND** | 1 | eval → stdout/stderr/exit_code |
| **STATE** | 2 | get/set/unset vars, funcs, aliases, traps; inspect |
| **OBSERVE** | 3 | subscribe → pre/post command events |
| **DEBUG** | 4 | breakpoints, step/next/finish, AST inspect |
| **PTY** | 5 | spawn, I/O relay, resize, signal |

## Quick Comparison

### Connect + Auth + Eval

**Python** (async):
```python
async with await BashClient.connect(path) as client:
    await client.auth(token)
    result = await client.eval("echo hello")
```

**TypeScript** (async):
```typescript
const client = await BashClient.connect(path);
await client.auth(token);
const result = await client.eval("echo hello");
await client.close();
```

**C** (synchronous):
```c
bc_client_t *c = bc_connect(path);
bc_auth(c, token);
bc_eval_result_t result;
bc_eval(c, "echo hello", &result);
bc_eval_result_free(&result);
bc_close(c);
```

**Java** (blocking):
```java
try (BashClient client = BashClient.connect(path)) {
    client.auth(token);
    EvalResult result = client.eval("echo hello");
}
```

## Error Handling

All bindings provide the same error hierarchy:

| Error | Meaning |
|-------|---------|
| `AuthError` | Authentication failed |
| `ProtocolError` | Malformed frame or message |
| `TimeoutError` | Operation timed out |
| `TransportError` | Socket/IO error |
| `ServerError` | Server returned error response |

## Testing

Each binding includes:
- **Unit tests**: Protocol framing, JSON helpers, base64, channel message construction (no server needed)
- **Channel tests**: Mock transport, request/response validation
- **Integration tests**: Real `bash-server --stdio` (skipped if binary not available)

Run tests:
```bash
# Python
cd python && pip install -e ".[dev]" && pytest

# TypeScript
cd typescript && npm install && npm test

# C
cd c && make check

# Java
cd java && mvn test
```

## Architecture

Each binding follows the same internal structure:

```
errors     → Exception/error hierarchy
types      → Message type definitions
protocol   → NDJSON encode/decode, base64, JSON helpers
transport  → Unix socket, stdio, fd, Named Pipe implementations
channels   → Per-channel request/response logic + server-push dispatch
client     → Top-level API, message routing, connection lifecycle
```

The client reads messages in a background loop and routes them:
- **Request/response** messages (auth, eval, state ops) go to per-channel queues
- **Server-push** messages (observe events, break hits, PTY output) go to registered callbacks

## Documentation

### Cross-Binding Guides

| Document | Description |
|----------|-------------|
| [CONTRIBUTING.md](CONTRIBUTING.md) | Shared contributor guide, PR checklist, release process |
| [COMPARISON.md](COMPARISON.md) | Language differences, feature matrix, choosing a binding |
| [PROTOCOL.md](PROTOCOL.md) | v2 NDJSON wire protocol from client perspective |
| [TESTING.md](TESTING.md) | Testing philosophy, mock transport pattern, coverage |

### Per-Language Documentation

Each binding includes comprehensive docs:

| Document | Python | TypeScript | C | Java |
|----------|--------|------------|---|------|
| Installation | [INSTALL.md](python/INSTALL.md) | [INSTALL.md](typescript/INSTALL.md) | [INSTALL.md](c/INSTALL.md) | [INSTALL.md](java/INSTALL.md) |
| Usage Guide | [GUIDE.md](python/GUIDE.md) | [GUIDE.md](typescript/GUIDE.md) | [GUIDE.md](c/GUIDE.md) | [GUIDE.md](java/GUIDE.md) |
| API Reference | [API.md](python/API.md) | [API.md](typescript/API.md) | [API.md](c/API.md) | [API.md](java/API.md) |
| Architecture | [ARCHITECTURE.md](python/ARCHITECTURE.md) | [ARCHITECTURE.md](typescript/ARCHITECTURE.md) | [ARCHITECTURE.md](c/ARCHITECTURE.md) | [ARCHITECTURE.md](java/ARCHITECTURE.md) |
| Contributing | [CONTRIBUTING.md](python/CONTRIBUTING.md) | [CONTRIBUTING.md](typescript/CONTRIBUTING.md) | [CONTRIBUTING.md](c/CONTRIBUTING.md) | [CONTRIBUTING.md](java/CONTRIBUTING.md) |
| Troubleshooting | [TROUBLESHOOTING.md](python/TROUBLESHOOTING.md) | [TROUBLESHOOTING.md](typescript/TROUBLESHOOTING.md) | [TROUBLESHOOTING.md](c/TROUBLESHOOTING.md) | [TROUBLESHOOTING.md](java/TROUBLESHOOTING.md) |
| Changelog | [CHANGELOG.md](python/CHANGELOG.md) | [CHANGELOG.md](typescript/CHANGELOG.md) | [CHANGELOG.md](c/CHANGELOG.md) | [CHANGELOG.md](java/CHANGELOG.md) |
| AI Metadata | [api-metadata.json](python/api-metadata.json) | [api-metadata.json](typescript/api-metadata.json) | [api-metadata.json](c/api-metadata.json) | [api-metadata.json](java/api-metadata.json) |
| Examples | [examples/](python/examples/README.md) | [examples/](typescript/examples/README.md) | [examples/](c/examples/README.md) | [examples/](java/examples/README.md) |

### Man Pages

C API function reference and conceptual overviews are in `../doc/server/man/`:

- **man3/** — 54 C function pages (`bc_connect`, `bc_eval`, `bc_state_*`, `bc_debug_*`, `bc_pty_*`, etc.)
- **man5/** — File format specs (`bashclient-api-metadata.5`, `bash-server-client-errors.5`)
- **man7/** — Conceptual overviews (`bash-server-client-api.7`, per-language overviews, transports, channels)

### Server Documentation

See [`../doc/server/`](../doc/server/README.md) for bash-server internals, protocol spec, and server-side man pages.
