# json_frame_write(3) -- Write one v2 protocol frame

# SYNOPSIS

    #include "server.h"

    int json_frame_write(int fd, int channel, int flags,
                         const char *payload, size_t payload_len);

# DESCRIPTION

Writes a single v2 protocol frame to the file descriptor `fd`.  The wire
format is determined by the process-global setting from
`json_set_wire_format(3)`.

# Binary wire format (`WIRE_BINARY`)

Writes a 6-byte header followed by the payload:

| Offset | Size   | Field    | Description                          |
|--------|--------|----------|--------------------------------------|
| 0      | 1 byte | channel  | Logical channel ID (0--5)            |
| 1      | 1 byte | flags    | Frame flags                          |
| 2      | 4 bytes| length   | Payload length in network byte order |
| 6      | N bytes| payload  | JSON string                          |

# NDJSON wire format (`WIRE_NDJSON`)

If the payload starts with `{` (JSON object), the channel is injected as
`"ch":N` at the start of the object and a newline is appended.  Uses
`writev(2)` for zero-copy assembly of the output.

If the payload does not start with `{`, it is wrapped as:
`{"ch":N,"data":"..."}\n`.

The `flags` parameter is ignored in NDJSON mode.

# PARAMETERS

| Parameter      | Type           | Description                           |
|----------------|----------------|---------------------------------------|
| `fd`           | `int`          | File descriptor to write to.          |
| `channel`      | `int`          | Channel ID (0--5).                    |
| `flags`        | `int`          | Frame flags (binary only; ignored for NDJSON). |
| `payload`      | `const char *` | Payload data (typically JSON).        |
| `payload_len`  | `size_t`       | Length of payload in bytes.           |

# RETURN VALUE

Returns **0** on success.

Returns **-1** on error.

# ERRORS

Returns -1 when:

- `payload_len` exceeds `FRAME_MAX_PAYLOAD` (1,048,576 bytes) in binary mode.
- `write(2)` or `writev(2)` fails with an error other than `EINTR`.
- (NDJSON) `snprintf` fails for the wrapped output.

# EXAMPLES

```c
/* Write a JSON response on the CONTROL channel */
const char *resp = "{\"type\":\"pong\"}";
json_frame_write(fd, CHAN_CONTROL, 0, resp, strlen(resp));

/* Write with flags (binary mode only) */
json_frame_write(fd, CHAN_COMMAND, FRAME_FLAG_FINAL,
                 payload, payload_len);

/* Write empty payload */
json_frame_write(fd, CHAN_CONTROL, 0, "", 0);
```

# SEE ALSO

`json_frame_read(3)`, `json_frame_write_fmt(3)`,
`json_set_wire_format(3)`, `json_get_wire_format(3)`

# NOTES

- Partial writes are retried internally until all bytes are sent.

- `EINTR` during `write(2)` / `writev(2)` is handled internally.

- In NDJSON mode, the function uses `writev(2)` with 3 iovec entries
  (prefix with channel, payload body, newline) for zero-copy output
  when the payload is a JSON object.

- In NDJSON mode, `writev(2)` partial writes are handled by advancing
  through the iovec array.

- NULL payload with `payload_len == 0` is safe in binary mode (writes
  header only).
