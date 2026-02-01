# bashclient (Python)

Async Python client for the bash-server v2 NDJSON protocol.

## Installation

```bash
pip install -e bash-server/clients/python
```

## Quick Start

```python
import asyncio
from bashclient import BashClient

async def main():
    async with await BashClient.connect("/tmp/bash-server-1000/sock") as client:
        await client.auth("your-64-char-hex-token")
        result = await client.eval("echo hello")
        print(result.stdout)  # "hello\n"

asyncio.run(main())
```

## Transports

```python
# Unix socket (default)
client = await BashClient.connect("/path/to/sock")

# Subprocess (--stdio mode)
client = await BashClient.connect_stdio("bash-server", "--stdio")

# Inherited file descriptor
client = await BashClient.connect_fd(3)

# Windows Named Pipe (Cygwin)
client = await BashClient.connect_named_pipe("/tmp/bash-server-pipe")
```

## Channels

### COMMAND (eval)

```python
result = await client.eval("ls -la")
print(result.stdout, result.stderr, result.exit_code)
```

### STATE (variables, functions, aliases, traps)

```python
var = await client.state.get_var("PATH")
await client.state.set_var("MY_VAR", "value", attributes=["exported"])
await client.state.unset_var("MY_VAR")

func = await client.state.get_func("my_function")
await client.state.unset_func("my_function")

alias = await client.state.get_alias("ll")
await client.state.set_alias("ll", "ls -lah")
await client.state.unset_alias("ll")

await client.state.set_trap("SIGINT", "echo caught")
await client.state.unset_trap("SIGINT")

items = await client.state.inspect("vars")  # "functions", "aliases", "traps"
```

### OBSERVE (command events)

```python
client.observe.on("pre_command", lambda e: print(f"Running: {e.command}"))
client.observe.on("post_command", lambda e: print(f"Done: exit={e.exit_status}"))
await client.observe.subscribe(level=1)
# ... events arrive as commands execute ...
await client.observe.unsubscribe()
```

### DEBUG (breakpoints, stepping)

```python
await client.debug.enable()
bp_id = await client.debug.add_breakpoint(kind="command", pattern="echo")

async def on_break(event):
    ast = await client.debug.inspect_ast()
    await client.debug.continue_()

client.debug.on("break_hit", on_break)
# ... execute commands that hit breakpoints ...
await client.debug.disable()
```

### PTY (terminal)

```python
client.pty.on("output", lambda data: print(data, end=""))
client.pty.on("exit", lambda code: print(f"Exit: {code}"))
info = await client.pty.spawn(rows=24, cols=80, strip_ansi=True)
await client.pty.write_input("echo hello\n")
await client.pty.resize(48, 120)
await client.pty.signal("SIGINT")
await client.pty.close()
```

## Error Handling

```python
from bashclient import AuthError, ProtocolError, TimeoutError, TransportError, ServerError

try:
    await client.auth(token)
except AuthError:
    print("Bad token")
except TransportError:
    print("Connection lost")
```

## Requirements

- Python 3.8+
- No external dependencies (stdlib only)

## Development

```bash
pip install -e ".[dev]"
pytest
```

## Documentation

| Document | Description |
|----------|-------------|
| [INSTALL.md](INSTALL.md) | Detailed installation, platform notes, verification |
| [GUIDE.md](GUIDE.md) | Comprehensive usage guide — all transports, channels, patterns |
| [API.md](API.md) | Full API reference — every class, method, type, constant |
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
- [bash-server-client-python(7)](../../doc/server/man/man7/bash-server-client-python.7.md) — man page overview
- [Server documentation](../../doc/server/README.md) — bash-server internals
