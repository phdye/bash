# json_session_handle(3) -- Main v2 JSON session handler

# SYNOPSIS

    #include "server.h"

    int json_session_handle(client_session_t *session, server_config_t *config);

# DESCRIPTION

Main entry point for handling a v2 protocol client session.  Reads v2 frames
in a loop using `json_frame_read(3)` and dispatches them to channel-specific
handlers based on the channel ID.

The session loop continues until a disconnect is requested, the client closes
the connection, or an error occurs.

# Channel dispatch

| Channel         | ID | Handler                  | Description                      |
|-----------------|----|--------------------------|----------------------------------|
| `CHAN_CONTROL`  | 0  | `v2_handle_control()`    | Auth, ping, disconnect, configure|
| `CHAN_COMMAND`  | 1  | `v2_handle_command()`    | EVAL command execution           |
| `CHAN_STATE`    | 2  | `v2_handle_state()`      | Variable/function/alias get/set  |
| `CHAN_OBSERVE`  | 3  | inline subscribe/unsub   | Observability event subscription |
| `CHAN_DEBUG`    | 4  | `debug_handle_message()` | Breakpoints and stepping         |
| `CHAN_PTY`      | 5  | `pty_handle_spawn()`     | Terminal I/O (spawn takes over loop) |

# Authentication

All channels except `CHAN_CONTROL` require prior authentication.
Unauthenticated requests on other channels receive an error response.
Authentication is performed by sending an `auth` message on `CHAN_CONTROL`
with a valid token.

# Session lifecycle

1. Protocol version is set to `PROTOCOL_V2`.
2. Frame read loop begins.
3. Each frame is dispatched to the appropriate channel handler.
4. On disconnect or error, `debug_cleanup()` and `observe_cleanup()` are
   called.

# PARAMETERS

| Parameter | Type                | Description                           |
|-----------|---------------------|---------------------------------------|
| `session` | `client_session_t *` | Client session state (fd, auth, etc.).|
| `config`  | `server_config_t *`  | Server configuration (token, flags).  |

# RETURN VALUE

Returns **0** on clean disconnect or connection close.

Returns **-1** on error (currently not used; errors break the read loop
and return 0).

# ERRORS

The function handles errors internally by breaking the frame read loop.
Frame read errors (connection closed, malformed frames) cause the session
to end gracefully.

# EXAMPLES

```c
client_session_t session;
session_init(&session, client_fd);

char first_byte;
int version = protocol_detect_version(client_fd, &first_byte);

if (version == PROTOCOL_V2 || version == PROTOCOL_V2_NDJSON) {
    if (version == PROTOCOL_V2_NDJSON)
        json_set_wire_format(WIRE_NDJSON);
    json_session_handle(&session, &server_config);
} else {
    session_handle(&session, &server_config);
}

session_cleanup(&session);
```

# SEE ALSO

`json_frame_read(3)`, `json_frame_write(3)`,
`json_frame_write_fmt(3)`, `json_set_wire_format(3)`,
`protocol_detect_version(3)`, `session_handle(3)`,
`protocol_secure_compare(3)`

# NOTES

- The function uses a helper `v2_wfd()` to determine the write file
  descriptor, which may differ from the read fd in pipe-based transports
  (where `session->write_fd` is set separately).

- The `CHAN_CONTROL` handler processes `auth`, `ping`, `disconnect`, and
  `configure` message types.  On successful auth, it calls
  `init_bash_for_session()`, `observe_init()`, and `debug_init()`.

- The `CHAN_COMMAND` handler captures stdout/stderr via temp files and
  dup2, sends base64-encoded output as `stdout`/`stderr` frames, and
  a `complete` frame with the exit code.

- The `CHAN_STATE` handler bridges v2 JSON messages to v1 state handlers
  by using a pipe to capture v1 text responses and converting them to
  JSON.

- The `CHAN_PTY` handler (`pty_handle_spawn`) takes over the session loop
  for the duration of the PTY session, then returns control to the main
  frame loop.

- The `CHAN_OBSERVE` handler manages event subscription levels via
  `observe_set_level()`.

- Each payload is freed after dispatch.  Channel handlers must not store
  or return references to the payload buffer.

- Cleanup functions (`debug_cleanup`, `observe_cleanup`) are always called
  on exit, regardless of the reason for session termination.
