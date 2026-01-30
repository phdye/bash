# json_set_wire_format(3) -- Set the process-global wire format for v2 frame I/O

# SYNOPSIS

    #include "server.h"

    void json_set_wire_format(int format);

# DESCRIPTION

Sets the wire format used by `json_frame_read(3)` and `json_frame_write(3)`
for the current process.  The format determines how v2 protocol frames are
serialized on the wire.

| Constant      | Value | Description                              |
|---------------|-------|------------------------------------------|
| `WIRE_BINARY` | 0     | 6-byte binary header + payload           |
| `WIRE_NDJSON` | 1     | Newline-delimited JSON with `"ch"` field |

This function should be called once per session, immediately after protocol
version detection (see `protocol_detect_version(3)`) and before any v2
frame I/O.

The setting is stored in a process-global static variable.  This is safe
because bash-server uses a fork-per-session model: each client session
runs in its own process.

# PARAMETERS

| Parameter | Type  | Description                                            |
|-----------|-------|--------------------------------------------------------|
| `format`  | `int` | Wire format: `WIRE_BINARY` (0) or `WIRE_NDJSON` (1).  |

# RETURN VALUE

None (`void`).

# ERRORS

No error checking is performed.  Passing an invalid format value leads to
undefined behavior in subsequent frame I/O calls.

# EXAMPLES

```c
char first_byte;
int version = protocol_detect_version(client_fd, &first_byte);

if (version == PROTOCOL_V2) {
    json_set_wire_format(WIRE_BINARY);
    json_session_handle(session, config);
} else if (version == PROTOCOL_V2_NDJSON) {
    json_set_wire_format(WIRE_NDJSON);
    json_session_handle(session, config);
}
```

# SEE ALSO

`json_get_wire_format(3)`, `json_frame_read(3)`,
`json_frame_write(3)`, `protocol_detect_version(3)`,
`json_session_handle(3)`

# NOTES

- The default wire format is `WIRE_BINARY` (0).

- This function is not thread-safe, but thread safety is not required
  because each session runs in a separate forked process.

- Changing the wire format mid-session is not supported and will corrupt
  the protocol stream.

- The wire format affects both reading and writing: `json_frame_read(3)`
  and `json_frame_write(3)` both check the global setting on every call.
