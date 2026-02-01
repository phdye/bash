# Changelog

All notable changes to the bashclient Java package will be documented in this
file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.1.0] - 2026-01-30

### Added

- **BashClient** main class with `AutoCloseable` support for try-with-resources
- **Static factory methods** for three transport modes:
  - `BashClient.connect(socketPath)` -- Unix domain socket via junixsocket
  - `BashClient.connectStdio(args...)` -- subprocess stdin/stdout transport
  - `BashClient.connectNamedPipe(pipeName)` -- Windows Named Pipe transport
- **Convenience methods** on BashClient: `auth()`, `eval()`, `ping()`,
  `isConnected()`, `isAuthenticated()`, `close()`
- **Six channel classes** in `org.gnu.bash.client.channels`:
  - `ControlChannel` (channel 0) -- auth, ping, disconnect, configure
  - `CommandChannel` (channel 1) -- eval, evalParsed, streaming output callbacks
  - `StateChannel` (channel 2) -- get/set/unset/list for variables, functions,
    aliases, and traps; bulk inspect
  - `ObserveChannel` (channel 3) -- pre/post command event subscription with
    `Consumer<ObjectNode>` callbacks
  - `DebugChannel` (channel 4) -- breakpoints (add, remove, enable, disable,
    list), stepping (step, next, finish, continue, skip), AST inspection
  - `PtyChannel` (channel 5) -- spawn, writeInput, readOutput, resize, signal,
    close, ANSI stripping, output callbacks
- **Transport interface** (`org.gnu.bash.client.Transport`) with three
  implementations:
  - `UnixSocketTransport` -- junixsocket `AFUNIXSocket` wrapper
  - `StdioTransport` -- `ProcessBuilder`/`Process` wrapper
  - `NamedPipeTransport` -- `RandomAccessFile` on `\\.\pipe\` paths
- **NDJSON protocol** (`NdjsonProtocol`) for wire serialization/deserialization
  using Jackson `ObjectNode`
- **Daemon reader thread** with `BlockingQueue<ObjectNode>` per-channel message
  routing (30-second default timeout)
- **Type classes** in `org.gnu.bash.client.types`:
  - `EvalResult` -- stdout, stderr, exit code
  - `VarInfo` -- value, type, exported, readonly
  - `BreakHitEvent` -- breakpoint ID, command, file, line
  - `PtyInfo` -- PID, cols, rows
- **Exception hierarchy** (`BashClientException` base with inner classes):
  - `AuthException` -- authentication failures
  - `ProtocolException` -- wire protocol errors with raw data
  - `TimeoutException` -- response timeout with duration, channel, action context
  - `TransportException` -- connection/IO errors with cause chain
  - `ServerException` -- server-side errors with error code
- **Thread safety**: synchronized protocol writes, `CopyOnWriteArrayList` for
  listeners, volatile connection state flags
- **Documentation suite**: INSTALL.md, GUIDE.md, ARCHITECTURE.md,
  CONTRIBUTING.md, API.md, TROUBLESHOOTING.md, CHANGELOG.md, api-metadata.json
- **Example programs**: Eval.java, Observe.java, Debugger.java, PtySession.java

### Dependencies

- junixsocket 2.9.1 (Unix domain sockets)
- Jackson 2.17.0 (JSON processing)
- JUnit 5.10.2 (testing, test scope only)

### Platform Support

- Linux x86_64 and aarch64
- macOS x86_64 and aarch64 (Apple Silicon)
- Cygwin x86_64 (Unix socket and Named Pipe transports)
- Windows native (Named Pipe transport only)

[Unreleased]: https://github.com/user/repo/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/user/repo/releases/tag/v0.1.0
