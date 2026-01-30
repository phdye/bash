# session_handle(3) -- Main v1 session command loop

# SYNOPSIS

    #include "server.h"

    int session_handle(client_session_t *session, server_config_t *config);

# DESCRIPTION

Main session handler that processes client commands in a loop.  This is the
entry point for all client interaction after connection setup.

On entry, the function performs automatic protocol version detection by
calling `protocol_detect_version()` to peek at the first byte on the wire:

- If the first byte is a v2 binary frame channel ID (0--5), delegates
  immediately to `json_session_handle()` with `PROTOCOL_V2`.
- If the first byte is `{` (0x7B), sets `WIRE_NDJSON` wire format and
  delegates to `json_session_handle()` with `PROTOCOL_V2_NDJSON`.
- Otherwise, falls through to the v1 text protocol handler.

In v1 mode, the function reads LF-terminated lines via
`protocol_read_line()`, parses each into a command and argument via
`protocol_parse_command()`, and dispatches to the appropriate handler:

| Command       | Authentication | Handler                              |
|---------------|----------------|--------------------------------------|
| `AUTH`        | Not required   | Validates token, initializes bash     |
| `EVAL`        | Required       | Executes command, captures output     |
| `PING`        | Not required   | Responds with `PONG`                  |
| `QUIT`        | Not required   | Responds with `BYE`, exits loop       |
| `GET-VAR`     | Required       | `state_handle_get_var()`              |
| `SET-VAR`     | Required       | `state_handle_set_var()`              |
| `UNSET-VAR`   | Required       | `state_handle_unset_var()`            |
| `GET-FUNC`    | Required       | `state_handle_get_func()`             |
| `UNSET-FUNC`  | Required       | `state_handle_unset_func()`           |
| `GET-ALIAS`   | Required       | `state_handle_get_alias()`            |
| `SET-ALIAS`   | Required       | `state_handle_set_alias()`            |
| `UNSET-ALIAS` | Required       | `state_handle_unset_alias()`          |
| `SET-TRAP`    | Required       | `state_handle_set_trap()`             |
| `UNSET-TRAP`  | Required       | `state_handle_unset_trap()`           |
| `INSPECT`     | Required       | `state_handle_inspect()`              |

Commands requiring authentication return `ERR not authenticated` if the
session has not yet completed a successful `AUTH` handshake.  Unknown
commands return `ERR unknown command: <cmd>`.

The loop exits when `QUIT` is received or `protocol_read_line()` returns
an error (connection closed or I/O error).

# PARAMETERS

- **session** -- Pointer to an initialized `client_session_t`.  Must have
  been set up by `session_init()`.

- **config** -- Pointer to the server configuration.  Used to access the
  auth token for `AUTH` validation and shell initialization flags for
  `init_bash_for_session()`.

# RETURN VALUE

Returns `0` on clean session exit (QUIT received or connection closed).
Returns `-1` on protocol or I/O error.

# ERRORS

I/O errors on the client socket cause the loop to exit.  The specific
errno is logged to stderr before returning.

# SEE ALSO

`session_init`(3), `session_cleanup`(3), `session_execute_command`(3),
`init_bash_for_session`(3), `json_session_handle`(3),
`protocol_detect_version`(3)

# NOTES

The function uses `session_wfd()` (an internal inline helper) to
determine the correct file descriptor for writing responses.  In socket
mode, read and write use the same fd.  In stdio mode, `fd` is stdin and
`write_fd` is stdout.

Protocol version detection is performed only once, on the first byte.
Once v1 is selected, the session remains in v1 mode for its lifetime.

Shell state (variables, functions, aliases, working directory, traps)
persists across EVAL commands within a single session because command
execution happens in-process via `parse_and_execute()`.
