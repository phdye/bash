# Changelog

All notable changes to the `bashclient` TypeScript/Node.js package will be
documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [Unreleased]

Nothing yet.

---

## [0.1.0] - 2026-01-30

### Added

- Initial release of the `bashclient` TypeScript/Node.js package.
- `BashClient` class with static factory methods:
  - `BashClient.connect()` for Unix socket transport.
  - `BashClient.connectStdio()` for stdio transport (spawns bash-server).
  - `BashClient.connectFd()` for file descriptor transport.
  - `BashClient.connectNamedPipe()` for Windows Named Pipe transport (Cygwin).
- Six channel interfaces matching bash-server v2 protocol:
  - `ControlChannel` (ch=0): ping, disconnect, configure, info.
  - `CommandChannel` (ch=1): eval, evalParsed.
  - `StateChannel` (ch=2): get/set/unset for variables, functions, aliases, traps; inspect.
  - `ObserveChannel` (ch=3): setLevel, pre_command/post_command events.
  - `DebugChannel` (ch=4): breakpoints (add/remove/enable/list), stepping
    (continue/step/next/finish/skip), inspectAst, status, break_hit events.
  - `PtyChannel` (ch=5): spawn, writeInput, resize, signal, close,
    output/close events.
- NDJSON wire protocol implementation (`protocol.ts`).
- Four transport implementations (`transport.ts`):
  - `SocketTransport` (Unix domain socket via `net` module).
  - `StdioTransport` (child process via `child_process` module).
  - `FdTransport` (file descriptors via `fs` module).
  - `NamedPipeTransport` (Windows Named Pipes via `net` module).
- Five typed error classes (`errors.ts`):
  - `AuthError` for authentication failures.
  - `ProtocolError` for malformed messages.
  - `TimeoutError` for operation timeouts.
  - `TransportError` for connection failures.
  - `ServerError` for server-returned errors.
- Full TypeScript type definitions (`types.ts`):
  - `Message`, `EvalResult`, `VarInfo`, `FuncInfo`, `AliasInfo`, `TrapInfo`.
  - `PreCommandEvent`, `PostCommandEvent`.
  - `Breakpoint`, `BreakHitEvent`, `DebugStatus`.
  - `PtyInfo`, `InspectItem`.
- Protocol constants: `CHAN_CONTROL` through `CHAN_PTY`, `FRAME_MAX_PAYLOAD`,
  `TOKEN_HEXLEN`, `OBSERVE_LEVEL_OFF`, `OBSERVE_LEVEL_COMMAND`.
- `FrameReader` class for incremental NDJSON parsing.
- Promise-based async API throughout.
- EventEmitter-based `on()`/`off()` for streaming channels (observe, debug, pty).
- Automatic socket path and token discovery.
- Zero external runtime dependencies (Node.js stdlib only).
- Comprehensive documentation suite:
  - INSTALL.md, GUIDE.md, API.md, ARCHITECTURE.md.
  - CONTRIBUTING.md, TROUBLESHOOTING.md, CHANGELOG.md.
  - api-metadata.json (machine-readable API metadata).
  - examples/ directory with documented example scripts.
- Jest test suite with ts-jest for TypeScript support.
- TypeScript strict mode with full type safety.
- Targets Node.js 16+ with ES2020 output.
- GPLv3+ license.

---

[Unreleased]: https://github.com/your-org/bash/compare/bashclient-ts-v0.1.0...HEAD
[0.1.0]: https://github.com/your-org/bash/releases/tag/bashclient-ts-v0.1.0
