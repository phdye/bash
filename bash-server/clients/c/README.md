# bashclient (C)

C client library for the bash-server v2 NDJSON protocol.

## Build

```bash
make          # builds libbashclient.so + libbashclient.a
make check    # runs unit tests
make examples # builds example programs
```

## Quick Start

```c
#include "bashclient.h"

int main(void) {
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock");
    bc_auth(c, "your-64-char-hex-token");

    bc_eval_result_t result;
    bc_eval(c, "echo hello", &result);
    printf("stdout: %s\n", result.stdout_data);
    printf("exit_code: %d\n", result.exit_code);
    bc_eval_result_free(&result);

    bc_close(c);
    return 0;
}
```

Compile: `gcc -o myapp myapp.c -lbashclient`

## API Overview

### Connection
- `bc_connect(path)` — Unix socket
- `bc_connect_stdio(argv)` — subprocess
- `bc_connect_fd(fd)` — inherited fd
- `bc_connect_named_pipe(name)` — Windows Named Pipe
- `bc_auth(c, token)` — authenticate
- `bc_close(c)` — disconnect and free
- `bc_error(c)` — last error message

### Command
- `bc_eval(c, cmd, &result)` — evaluate, get stdout/stderr/exit_code

### State
- `bc_state_get_var`, `bc_state_set_var`, `bc_state_unset_var`
- `bc_state_get_func`, `bc_state_unset_func`
- `bc_state_get_alias`, `bc_state_set_alias`, `bc_state_unset_alias`
- `bc_state_set_trap`, `bc_state_unset_trap`
- `bc_state_inspect(c, query, &json)` — bulk query

### Observe
- `bc_observe_subscribe(c, level)`, `bc_observe_unsubscribe(c)`
- `bc_observe_on_pre_command(c, callback, userdata)`
- `bc_observe_on_post_command(c, callback, userdata)`
- `bc_poll(c, timeout_ms)` — process server-push messages

### Debug
- `bc_debug_enable/disable`
- `bc_debug_add_breakpoint`, `bc_debug_remove_breakpoint`, `bc_debug_list_breakpoints`
- `bc_debug_continue/step/next/finish/skip`
- `bc_debug_inspect_ast(c, &json)`
- `bc_debug_on_break_hit(c, callback, userdata)`

### PTY
- `bc_pty_spawn(c, rows, cols, shell, strip_ansi, &info)`
- `bc_pty_write(c, data, len)`
- `bc_pty_resize(c, rows, cols)`
- `bc_pty_signal(c, name)`, `bc_pty_close(c)`
- `bc_pty_on_output/on_exit(c, callback, userdata)`

## Memory Management

All returned strings and structs must be freed by the caller using the corresponding `_free` functions or `bc_free()`. The library never retains references to caller-provided strings.

## Requirements

- POSIX (Unix sockets, fork, socketpair)
- No external dependencies
- C99 or later

## Documentation

| Document | Description |
|----------|-------------|
| [INSTALL.md](INSTALL.md) | Detailed installation, platform notes, verification |
| [GUIDE.md](GUIDE.md) | Comprehensive usage guide — all transports, channels, patterns |
| [API.md](API.md) | Full API reference — every function, type, constant, error code |
| [ARCHITECTURE.md](ARCHITECTURE.md) | Internals for contributors — module structure, memory model |
| [CONTRIBUTING.md](CONTRIBUTING.md) | Dev setup, code style, testing, PR process |
| [TROUBLESHOOTING.md](TROUBLESHOOTING.md) | Common issues, connection failures, debugging tips |
| [CHANGELOG.md](CHANGELOG.md) | Version history |
| [api-metadata.json](api-metadata.json) | Machine-readable API index for AI tools |
| [examples/](examples/README.md) | Annotated examples with expected output |

### Man Pages

54 function-level man pages in [`../../doc/server/man/man3/`](../../doc/server/man/man3/):
`bc_connect(3)`, `bc_auth(3)`, `bc_eval(3)`, `bc_state_get_var(3)`, `bc_debug_enable(3)`, `bc_pty_spawn(3)`, and more.

### See Also

- [Client bindings overview](../README.md) — all 4 language bindings
- [Cross-binding comparison](../COMPARISON.md) — choosing a language
- [Protocol reference](../PROTOCOL.md) — v2 NDJSON wire format
- [Testing guide](../TESTING.md) — testing philosophy and patterns
- [bash-server-client-c(7)](../../doc/server/man/man7/bash-server-client-c.7.md) — man page overview
- [Server documentation](../../doc/server/README.md) — bash-server internals
