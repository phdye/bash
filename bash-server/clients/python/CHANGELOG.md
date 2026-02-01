# Changelog

All notable changes to bashclient (Python) will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Nothing yet.

## [0.1.0] - 2025-01-30

### Added

- Initial release of the bashclient Python package.
- `BashClient` async class as the primary entry point for all operations.
- Four transport modes:
  - `connect(path)` -- Unix domain socket (Linux, macOS, Cygwin).
  - `connect_stdio(cmd)` -- Subprocess stdin/stdout (all platforms).
  - `connect_fd(read_fd, write_fd)` -- Pre-opened file descriptors (POSIX).
  - `connect_named_pipe(name)` -- Windows Named Pipe (Cygwin, Windows).
- All six v2 protocol channels:
  - **CONTROL** (channel 0): `auth()`, `ping()`, `close()`, `configure()`.
  - **COMMAND** (channel 1): `eval()`, `eval_parsed()`.
  - **STATE** (channel 2): `get_var()`, `set_var()`, `unset_var()`,
    `get_func()`, `set_func()`, `unset_func()`, `get_alias()`,
    `set_alias()`, `unset_alias()`, `get_trap()`, `set_trap()`,
    `inspect()`.
  - **OBSERVE** (channel 3): `observe_start()`, `observe_stop()`,
    `on_observe()`.
  - **DEBUG** (channel 4): `breakpoint_set()`, `breakpoint_clear()`,
    `debug_step()`, `debug_next()`, `debug_finish()`, `debug_skip()`,
    `debug_continue()`, `inspect_ast()`, `on_debug()`.
  - **PTY** (channel 5): `pty_spawn()`, `pty_write()`, `pty_resize()`,
    `pty_signal()`, `pty_close()`, `on_pty()`.
- NDJSON (Newline-Delimited JSON) wire protocol implementation.
- Full type annotations throughout the codebase (Python 3.8+ compatible).
- Async context manager support (`async with BashClient() as client:`).
- Callback-based server-push event dispatch for OBSERVE, DEBUG, and PTY
  channels, supporting both sync and async callbacks.
- 12 dataclass types for structured results and events:
  - `EvalResult`, `PingResult`, `AuthResult`
  - `VarInfo`, `FuncInfo`, `AliasInfo`, `TrapInfo`, `InspectResult`
  - `ObserveEvent`, `DebugEvent`, `BreakpointInfo`, `PtyEvent`
- 5 exception types under `BashClientError`:
  - `ConnectionError`, `AuthenticationError`, `ProtocolError`,
    `TimeoutError`, `ChannelError`
- Per-channel `asyncio.Queue` message routing with background reader loop.
- Configurable timeouts at constructor and per-call level.
- Base64 encoding/decoding helpers for binary data in JSON frames.
- Unit test suite for protocol encoding/decoding.
- Channel test suite using mock transport.
- Integration test suite using bash-server `--stdio` mode.
- Example scripts: `eval.py`, `observe.py`, `debugger.py`, `pty.py`.
- Documentation suite: README, API reference, user guide, installation
  guide, architecture overview, contributing guide, troubleshooting,
  changelog, machine-readable API metadata, examples README.
- Zero external runtime dependencies (stdlib only).
- GPLv3+ license.

### Dependencies

- **Runtime**: None (Python standard library only).
- **Development**: `pytest>=7.0`, `pytest-asyncio>=0.21`.

### Compatibility

- Python 3.8, 3.9, 3.10, 3.11, 3.12, 3.13.
- Linux, macOS, Cygwin, Windows (limited transport support).
- bash-server v2 with NDJSON wire format.
