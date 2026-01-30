# json_frame_read(3) -- Read one v2 protocol frame

# SYNOPSIS

    #include "server.h"

    int json_frame_read(int fd, int *channel, int *flags,
                        char **payload, size_t *payload_len);

# DESCRIPTION

Reads a single v2 protocol frame from the file descriptor `fd`.  The wire
format is determined by the process-global setting from
`json_set_wire_format(3)`.

# Binary wire format (`WIRE_BINARY`)

Reads a 6-byte header followed by the payload:

| Offset | Size   | Field    | Description                          |
|--------|--------|----------|--------------------------------------|
| 0      | 1 byte | channel  | Logical channel ID (0--5)            |
| 1      | 1 byte | flags    | Frame flags (COMPRESSED, BINARY, etc.)|
| 2      | 4 bytes| length   | Payload length in network byte order |
| 6      | N bytes| payload  | JSON string                          |

# NDJSON wire format (`WIRE_NDJSON`)

Reads bytes until `\n` (newline-delimited JSON).  The `"ch"` field is
extracted from the JSON object to determine the channel.  Flags are always
set to 0.  If no `"ch"` field is present, defaults to `CHAN_CONTROL` (0).

In both formats, the payload is `malloc`'d and NUL-terminated.  The caller
is responsible for freeing `*payload`.

# PARAMETERS

| Parameter      | Type      | Description                                  |
|----------------|-----------|----------------------------------------------|
| `fd`           | `int`     | File descriptor to read from.                |
| `channel`      | `int *`   | Receives the channel ID (0--5).              |
| `flags`        | `int *`   | Receives frame flags (binary) or 0 (NDJSON). |
| `payload`      | `char **` | Receives `malloc`'d payload (caller frees).  |
| `payload_len`  | `size_t *`| Receives payload length in bytes.            |

# RETURN VALUE

Returns **0** on success.

Returns **-1** on error, EOF, unknown channel, or payload exceeding
`FRAME_MAX_PAYLOAD` (1 MB).  On error, `*payload` is set to NULL and
`*payload_len` to 0.

# ERRORS

Returns -1 when:

- `read(2)` fails or returns EOF.
- The channel ID exceeds `CHAN_MAX` (5).
- The payload length exceeds `FRAME_MAX_PAYLOAD` (1,048,576 bytes).
- `malloc(3)` fails.
- (NDJSON) The line exceeds `FRAME_MAX_PAYLOAD + 64` bytes.
- (NDJSON) The `"ch"` field is out of range.

# EXAMPLES

```c
int channel, flags;
char *payload;
size_t payload_len;

while (json_frame_read(fd, &channel, &flags, &payload, &payload_len) == 0) {
    switch (channel) {
    case CHAN_CONTROL:
        handle_control(payload);
        break;
    case CHAN_COMMAND:
        handle_command(payload);
        break;
    case CHAN_STATE:
        handle_state(payload);
        break;
    }
    free(payload);
}
```

# SEE ALSO

`json_frame_write(3)`, `json_frame_write_fmt(3)`,
`json_set_wire_format(3)`, `json_get_wire_format(3)`,
`json_session_handle(3)`, `protocol_detect_version(3)`

# NOTES

- The payload is always NUL-terminated for convenience, but `*payload_len`
  gives the actual byte count.

- For binary framing, unknown channels (> `CHAN_MAX`) cause the payload
  to be read and discarded before returning -1.

- For NDJSON framing, the buffer grows dynamically (starting at 1024 bytes,
  doubling as needed) up to `FRAME_MAX_PAYLOAD + 64`.

- `EINTR` during `read(2)` is handled internally in both formats.

- The function reads exactly the number of bytes specified by the header
  (binary) or until newline (NDJSON).  It does not over-read.
