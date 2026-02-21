# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is **GNU Bash 5.1 (patch level 16)** modified for compilation to **WebAssembly (WASM/WASIX)**. The repository contains the complete Bash shell implementation with extensive modifications for cross-platform WASM compilation.

- **Version**: Bash 5.1, Patch Level 16
- **License**: GNU GPL v3+
- **Primary Target**: `wasm32-wasmer-wasi`
- **Output**: `bash.wasm` (deployed to `/prog/packages/bash/bash.wasm`)

## Build Commands

### WASM Build (Primary)

The Makefile is pre-configured for WASM. Build with:

```bash
make -j12
```

This compiles bash to `shell.wasm`, optimizes with `wasm-opt --asyncify`, and copies to `/prog/packages/bash/bash.wasm`.

**Build Dependencies:**
- Clang with WebAssembly backend
- WASIX libc sysroot at `../wasix-libc/sysroot32`
- `wasm-opt` for asyncify transformation
- LLD linker (at `/prog/rust/build/x86_64-unknown-linux-gnu/lld/bin`)

**Clean build:**
```bash
make clean && make -j12
```

### Cygwin Native Build (with bash-server)

The `feature/bash-server-cygwin` branch builds a native Cygwin bash as a shared
library (`cygbash-5.1.dll`) plus several executables that link against it:

```bash
./configure --enable-bash-server
make -j12
make bash-server
make bashclient
```

**Build outputs:**

| Artifact | Description | Links against |
|----------|-------------|---------------|
| `ss-bash.exe` | Bash shell (68 KB stub) | `cygbash-5.1.dll`, `cygwin1.dll` |
| `cygbash-5.1.dll` | Shared Bash library (4.6 MB) | readline, history, ncursesw, intl |
| `bash-server/bash-server.exe` | Persistent evaluation daemon | `cygbash-5.1.dll`, `cygwin1.dll` |
| `bashclient/bashclient.exe` | CLI client | `cygwin1.dll` only (standalone) |

**Install to alternate Cygwin:**
```bash
make install-bash-server  # installs bash-server and bashclient
```

### Standard Native Build (Not WASM)

For a standard native bash build (not the default configuration):
```bash
./configure
make -j12
make tests
```

## Test Suite

79 test files with 353 sub-tests in `tests/` directory.

**Run all tests** (requires working bash binary):
```bash
cd tests && sh run-all
```

Tests compare output against `.right` files. Any output indicates a test failure unless otherwise noted.

**Test file structure:**
- `<name>.tests` - Main test file
- `<name>N.sub` - Individual test subscripts
- `<name>.right` - Expected output

### bash-server Tests

```bash
cd bash-server/tests && make check
```

| Test binary | Tests | Covers |
|-------------|-------|--------|
| `test_protocol` | 30 | base64, secure_compare, parse_command, read/write line, detect_version |
| `test_socket` | 8 | socket create/close, permissions, accept, nonblocking |
| `test_config` | 19 | directory creation, config parsing, path resolution, token, CLI |
| `test_session` | — | v2 session lifecycle, channel dispatch, auth flow |
| `test_serialize` | — | COMMAND tree ↔ JSON round-trip for all 10 command types |
| `test_state` | — | get/set/unset vars, funcs, aliases, traps; inspect |
| `test_observe` | — | observe_init, set_level, pre/post command events, cleanup |
| `test_transport` | — | stdio, fd, named-pipe transport modes |
| `test_winpipe` | — | Windows Named Pipe create, accept, token path (Cygwin only) |

All tests use `alarm()` timeouts (10–30 s) and fork-based isolation for
blocking operations. See `bash-server/tests/Makefile` for build details.

## Architecture

### Core Source Files

| File | Purpose |
|------|---------|
| `shell.c` | Main entry point, initialization |
| `parse.y` | Yacc grammar → generates `y.tab.c` |
| `subst.c` | Parameter/variable/command substitution (largest file) |
| `execute_cmd.c` | Command execution engine |
| `variables.c` | Variable management, arrays |
| `bashline.c` | Readline integration, completion |
| `nojobs.c` | Job control stub (WASM uses this instead of `jobs.c`) |
| `trap.c` / `sig.c` | Signal handling |

### Directory Structure

```
├── bash-server/     # Persistent evaluation daemon
│   ├── server.h             # Common header, constants, typedefs, all declarations
│   ├── server_main.c        # Entry point, CLI (16 options), daemonization, accept loop
│   ├── server_protocol.c    # v1 wire protocol, base64, secure compare, version detect
│   ├── server_session.c     # Session lifecycle, auth, v1 EVAL, v2 channel dispatch
│   ├── server_socket.c      # Unix socket management, SO_PEERCRED
│   ├── server_json.c        # v2 JSON frame read/write, NDJSON, channel routing
│   ├── server_state.c       # CHAN_STATE: get/set/unset vars, funcs, aliases, traps
│   ├── server_observe.c     # CHAN_OBSERVE: pre/post command hooks, event streaming
│   ├── server_debug.c       # CHAN_DEBUG: breakpoints, stepping, AST inspection
│   ├── server_pty.c         # CHAN_PTY: forkpty, relay, resize, ANSI stripping
│   ├── cmd_serialize.c      # COMMAND tree ↔ JSON (10 command types, bidirectional)
│   ├── server_winpipe.c     # Windows Named Pipe transport (Cygwin only)
│   └── tests/               # Unit tests (9 test binaries)
├── bashclient/      # Standalone CLI client
│   └── bashclient.c         # Connect, auth, eval/file/interactive modes
├── builtins/        # 44 .def files defining built-in commands
├── lib/
│   ├── sh/          # Shell utility library
│   ├── readline/    # Command-line editing
│   ├── glob/        # Pathname expansion
│   ├── termcap/     # Terminal capabilities
│   └── malloc/      # Memory allocator (not used in WASM build)
├── tests/           # Test suite
├── doc/
│   ├── server/      # bash-server documentation suite
│   │   ├── README.md, protocol.md, architecture.md, configuration.md, security.md
│   │   ├── channels.md, developer.md, transports.md
│   │   └── man/     # Man-page style reference (man1/, man3/, man5/, man7/)
│   └── ...          # TeXinfo, man, PDF
└── support/         # Build utilities
```

### WASM-Specific Modifications

Key differences from standard bash:

1. **No job control**: Uses `nojobs.c` instead of `jobs.c` (`JOB_CONTROL` undefined in `config.h`)
2. **WASI emulation**: `_WASI_EMULATED_MMAN`, `_WASI_EMULATED_SIGNAL`, `_WASI_EMULATED_PROCESS_CLOCKS`
3. **Named pipes missing**: `NAMED_PIPES_MISSING` defined
4. **Threading**: POSIX thread model with local-exec TLS
5. **Asyncify**: Output is post-processed with `wasm-opt --asyncify` for async operations

The `WASM` preprocessor macro gates WASM-specific code paths (check `config.h`).

### Build Artifacts

Each subdirectory has its own Makefile:
- `make builtins` - Compile builtins/*.o
- `make sh` - Compile lib/sh/*.o
- `make readline` - Compile lib/readline/*.o
- `make glob` - Compile lib/glob/*.o
- `make termcap` - Compile lib/termcap/*.o

### bash-server Architecture

`bash-server` is a persistent Bash evaluation daemon that embeds the full Bash
interpreter via `cygbash-5.1.dll`. It supports two protocol versions and four
transport modes.

**Key design points:**

- **Transports**: Unix socket (default), stdio (`--stdio`), fd (`--fd N`), Windows Named Pipes (`--named-pipe`, Cygwin only)
- **Protocol v1**: Line-oriented text (LF-terminated), base64 payloads. Commands: AUTH, EVAL, PING, QUIT
- **Protocol v2**: Length-prefixed JSON frames (6-byte header) or NDJSON. 6 multiplexed channels
- **Auto-detection**: First byte determines protocol (`{`/`\n` → NDJSON, 0–5 → binary v2, else → v1)
- **Authentication**: 256-bit random token from `/dev/urandom`, constant-time comparison
- **Execution model**: Fork-per-session, fork-per-command with pipe-captured stdout/stderr
- **Cygwin quirk**: `--no-peercred` disables `SO_PEERCRED` handshake for Python client compatibility

**v2 Channel architecture:**

| Channel | ID | Purpose |
|---------|----|---------|
| CHAN_CONTROL | 0 | Auth, ping, disconnect, configure (observe level, wire format) |
| CHAN_COMMAND | 1 | eval, eval_parsed (pre-parsed COMMAND JSON), stdout/stderr/complete |
| CHAN_STATE | 2 | get/set/unset/inspect for variables, functions, aliases, traps |
| CHAN_OBSERVE | 3 | Subscribe to pre/post command events with timing and cwd |
| CHAN_DEBUG | 4 | Breakpoints (command/line/function), stepping, AST inspection |
| CHAN_PTY | 5 | Spawn PTY, I/O relay, resize, signal, close, ANSI stripping |

**Source modules (11 .c files + server.h):**

| Module | Purpose |
|--------|---------|
| `server_main.c` | CLI (16 options), config, daemonization, signal setup, accept loop |
| `server_protocol.c` | v1 wire protocol I/O, base64, constant-time compare, version detect |
| `server_session.c` | Session state machine, auth, v1 EVAL, v2 channel dispatch |
| `server_socket.c` | Socket create/bind/listen, close, accept, nonblocking |
| `server_json.c` | v2 JSON frame read/write, NDJSON framing, json_get helpers |
| `server_state.c` | CHAN_STATE handler: 11 operations across 4 namespaces |
| `server_observe.c` | CHAN_OBSERVE: register pre/post command hooks, JSON events |
| `server_debug.c` | CHAN_DEBUG: breakpoints, step/next/finish/skip, AST inspect |
| `server_pty.c` | CHAN_PTY: forkpty + select relay, ANSI strip state machine |
| `cmd_serialize.c` | COMMAND tree ↔ JSON (10 command types, bidirectional) |
| `server_winpipe.c` | Windows Named Pipe transport with DACL security (Cygwin only) |

**CLI options** (16 total): `--socket`, `--name`, `--token`, `--daemonize`, `--no-peercred`,
`--max-clients`, `--stdio`, `--fd`, `--auth-fd`, `--named-pipe`, `--login`, `--norc`,
`--noprofile`, `--init`, `--help`, `--version`

**Socket path resolution:** CLI → `$BASH_SERVER_SOCKET` → `~/.bash-serverrc` → `$XDG_RUNTIME_DIR` → `/tmp/bash-server-<uid>/sock`

See `doc/server/` for comprehensive documentation (8 top-level guides + 62 man pages).

## Key Configuration

`config.h` contains all feature flags. Important WASM-related settings:
- `#define WASM 1`
- `/* #undef JOB_CONTROL */` (job control disabled)
- `#define NAMED_PIPES_MISSING 1`
- `#define READLINE 1`
- `#define HISTORY 1`

Cygwin native build (`feature/bash-server-cygwin` branch) has full feature set
enabled: `JOB_CONTROL`, `ALIAS`, `READLINE`, `HISTORY`, `PROCESS_SUBSTITUTION`,
`ARRAY_VARS`, etc. See `config.h` for the complete set.

## Documentation

- `doc/bashref.texi` - Complete reference manual (TeXinfo source)
- `doc/bash.1` - Man page
- `doc/FAQ` - Frequently asked questions
- `doc/server/` - bash-server documentation suite:
  - `README.md` - Overview, quick start, v1+v2 examples
  - `protocol.md` - Wire protocol specification (v1 text, v2 binary, NDJSON, auto-detect)
  - `architecture.md` - Internal design, all 11 modules, data flow diagrams
  - `configuration.md` - All 16 CLI options, 4 transport modes, shell init
  - `security.md` - Threat model, auth, DACL, multi-transport security
  - `channels.md` - v2 channel architecture, all 6 channels with JSON schemas
  - `developer.md` - Build/test guide, how to add channels/messages, code conventions
  - `transports.md` - Unix socket, stdio, fd, Named Pipes, auth-fd delivery
  - `man/man1/` - Command reference (bash-server.1, bashclient.1)
  - `man/man3/` - C API reference (~46 function pages)
  - `man/man5/` - File/wire format specs (7 pages)
  - `man/man7/` - Conceptual overviews (7 pages)
- `COMPAT` - Version incompatibilities
- `NEWS` - New features in bash-5.1
- `POSIX` - POSIX compliance notes
