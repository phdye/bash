# Changelog

All notable changes to libbashclient will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

Nothing yet.

## [0.1.0] - 2025-01-30

Initial release of the libbashclient C library.

### Added

#### Connection
- `bc_connect()` - Connect via Unix domain socket with auto-discovery
- `bc_connect_opts()` - Connect with full transport and option control
- `bc_close()` - Close connection and free resources
- `bc_ping()` - Test server responsiveness
- `bc_configure()` - Set session configuration parameters
- `bc_disconnect()` - Graceful server disconnect
- `bc_get_fd()` - Get underlying file descriptor for event loop integration
- `bc_last_error()` - Get last error code
- `bc_last_error_msg()` - Get last error message

#### Command (Channel 1)
- `bc_eval()` - Synchronous command evaluation with captured output
- `bc_eval_stream()` - Streaming evaluation with incremental callbacks

#### State (Channel 2)
- `bc_var_get()` / `bc_var_set()` / `bc_var_set_attr()` / `bc_var_unset()` - Variable management
- `bc_func_get()` / `bc_func_set()` / `bc_func_unset()` - Function management
- `bc_alias_get()` / `bc_alias_set()` / `bc_alias_unset()` - Alias management
- `bc_trap_get()` / `bc_trap_set()` / `bc_trap_unset()` - Trap management
- `bc_inspect()` - JSON dump of entire namespace

#### Observe (Channel 3)
- `bc_observe_set_level()` - Set observation level (0/1/2)
- `bc_on_pre_command()` - Register pre-command event callback
- `bc_on_post_command()` - Register post-command event callback
- `bc_poll()` - Poll for and dispatch server-push events
- `bc_observe_cleanup()` - Unregister all observe callbacks

#### Debug (Channel 4)
- `bc_debug_enable()` / `bc_debug_disable()` - Debugger lifecycle
- `bc_debug_status()` - Query debugger state
- `bc_debug_break_command()` - Set breakpoint on command pattern
- `bc_debug_break_line()` - Set breakpoint on line number
- `bc_debug_break_function()` - Set breakpoint on function entry
- `bc_debug_set_condition()` - Set conditional breakpoint
- `bc_debug_enable_breakpoint()` - Enable/disable breakpoint
- `bc_debug_remove_breakpoint()` - Remove breakpoint
- `bc_debug_list_breakpoints()` - List all breakpoints
- `bc_debug_step()` - Step over
- `bc_debug_step_into()` - Step into function
- `bc_debug_step_out()` - Step out of function
- `bc_debug_continue()` - Continue execution
- `bc_debug_skip()` - Skip current command
- `bc_debug_inspect_ast()` - Get current command AST as JSON
- `bc_on_break_hit()` - Register breakpoint hit callback

#### PTY (Channel 5)
- `bc_pty_spawn()` - Spawn pseudo-terminal session
- `bc_pty_read()` - Read PTY output
- `bc_pty_write()` - Write PTY input
- `bc_pty_resize()` - Resize terminal dimensions
- `bc_pty_set_strip_ansi()` - Enable/disable ANSI stripping
- `bc_pty_close()` - Close PTY session
- `bc_pty_info_free()` - Free PTY info struct

#### Utility
- `bc_free()` - Free library-allocated memory
- `bc_version()` - Get library version string
- `bc_eval_result_free()` - Free eval result struct
- `bc_debug_status_free()` - Free debug status struct
- `bc_breakpoints_free()` - Free breakpoint array

#### Transports
- Unix domain socket (default)
- Stdio (fork+exec bash-server --stdio)
- File descriptor (pre-opened fds)
- Windows Named Pipe (Cygwin only)

#### Wire Formats
- NDJSON (newline-delimited JSON) - default
- Binary v2 (6-byte length-prefixed frames)
- Automatic protocol version detection

#### Platform Support
- Linux (glibc and musl)
- macOS
- Cygwin
- FreeBSD

[Unreleased]: https://github.com/example/libbashclient/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/example/libbashclient/releases/tag/v0.1.0
