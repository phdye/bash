# protocol_detect_version(3) -- Auto-detect protocol version from first byte

# SYNOPSIS

    #include "server.h"

    int protocol_detect_version(int fd, char *first_byte);

# DESCRIPTION

Detects the protocol version of an incoming client connection by peeking at
the first byte using `recv(2)` with `MSG_PEEK`.  The byte is stored in
`*first_byte` but is not consumed from the socket buffer, allowing the
subsequent protocol handler to read it normally.

The detection logic is a three-way classification:

| First byte value       | Detected protocol      | Return value          |
|------------------------|------------------------|-----------------------|
| 0x00 -- 0x05 (channel ID) | Binary v2 framing  | `PROTOCOL_V2` (2)    |
| 0x7B (`{`)             | NDJSON v2              | `PROTOCOL_V2_NDJSON` (3) |
| >= 0x20 (other printable) | Text v1 protocol   | `PROTOCOL_V1` (1)    |

This classification is unambiguous because:
- No v1 command starts with `{`.
- No v1 command starts with a byte in the 0x00--0x05 range.
- v2 binary frames always begin with a channel byte (0--5).
- NDJSON lines always begin with `{`.

# PARAMETERS

| Parameter    | Type     | Description                                      |
|--------------|----------|--------------------------------------------------|
| `fd`         | `int`    | Socket file descriptor to peek at.               |
| `first_byte` | `char *` | Receives the peeked byte value.                 |

# RETURN VALUE

Returns one of:

- `PROTOCOL_V1` (1) -- Text-based line protocol.
- `PROTOCOL_V2` (2) -- Binary length-prefixed JSON frames.
- `PROTOCOL_V2_NDJSON` (3) -- Newline-delimited JSON.

Returns **-1** on error or if the socket is closed before any data arrives.

# ERRORS

Returns -1 when:

- `recv(2)` returns 0 (peer closed connection before sending data).
- `recv(2)` returns -1 (socket error).

# EXAMPLES

```c
char first_byte;
int version = protocol_detect_version(client_fd, &first_byte);

switch (version) {
case PROTOCOL_V1:
    session_handle(session, config);  /* text protocol */
    break;
case PROTOCOL_V2:
    json_set_wire_format(WIRE_BINARY);
    json_session_handle(session, config);
    break;
case PROTOCOL_V2_NDJSON:
    json_set_wire_format(WIRE_NDJSON);
    json_session_handle(session, config);
    break;
default:
    fprintf(stderr, "protocol detection failed\n");
    break;
}
```

# SEE ALSO

`json_session_handle(3)`, `session_handle(3)`,
`json_set_wire_format(3)`, `json_get_wire_format(3)`,
`recv(2)`

# NOTES

- Uses `MSG_PEEK` so the byte remains in the socket buffer for the
  protocol handler to read normally.

- The function only works on socket file descriptors (not pipes or regular
  files) because it uses `recv(2)` with `MSG_PEEK`.  For `--stdio` mode,
  a different detection path may be needed.

- The detection is deterministic after a single byte, making it zero-cost
  for protocol negotiation.

- The `first_byte` output is primarily useful for debugging; the return
  value is the authoritative protocol indicator.
