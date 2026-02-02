# bashclient (Java)

Java client for the bash-server v2 NDJSON protocol.

## Build

```bash
mvn package
```

## Quick Start

```java
import org.gnu.bash.client.BashClient;
import org.gnu.bash.client.types.EvalResult;

try (BashClient client = BashClient.connect("/tmp/bash-server-1000/sock")) {
    client.auth("your-64-char-hex-token");
    EvalResult result = client.eval("echo hello");
    System.out.println(result.getStdout()); // "hello\n"
}
```

## Transports

```java
// Unix socket
BashClient client = BashClient.connect("/path/to/sock");

// Subprocess (--stdio)
BashClient client = BashClient.connectStdio("bash-server", "--stdio");

// Named Pipe
BashClient client = BashClient.connectNamedPipe("bash-server-pipe");
```

## Channels

### Command
```java
EvalResult r = client.eval("ls -la");
```

### State
```java
VarInfo v = client.state.getVar("PATH");
client.state.setVar("X", "1", List.of("exported"));
client.state.unsetVar("X");
String func = client.state.getFunc("myfunc");
String alias = client.state.getAlias("ll");
client.state.setTrap("SIGINT", "echo caught");
JsonNode items = client.state.inspect("vars");
```

### Observe
```java
client.observe.on("pre_command", msg -> System.out.println(msg));
client.observe.subscribe(1);
```

### Debug
```java
client.debug.enable();
int bpId = client.debug.addBreakpoint("command", "echo", -1, null);
client.debug.on("break_hit", ev -> { /* handle */ });
client.debug.continue_();
client.debug.disable();
```

### PTY
```java
client.pty.on("output", data -> System.out.print(data));
PtyInfo info = client.pty.spawn(24, 80, null, true);
client.pty.writeInput("echo hello\n");
client.pty.close();
```

## Dependencies

- junixsocket (Unix domain sockets)
- Jackson (JSON)
- JUnit 5 (tests)

## Requirements

- Java 11+
- Maven 3.6+

## Documentation

| Document | Description |
|----------|-------------|
| [INSTALL.md](INSTALL.md) | Detailed installation, platform notes, verification |
| [GUIDE.md](GUIDE.md) | Comprehensive usage guide — all transports, channels, patterns |
| [API.md](API.md) | Full API reference — every class, method, type, exception |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Internals for contributors — module structure, threading model |
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
- [bash-server-client-java(7)](../../doc/server/man/man7/bash-server-client-java.7.md) — man page overview
- [Server documentation](../../doc/server/README.md) — bash-server internals
