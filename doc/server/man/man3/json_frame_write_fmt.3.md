# json_frame_write_fmt(3) -- Write a printf-formatted v2 protocol frame

# SYNOPSIS

    #include "server.h"

    int json_frame_write_fmt(int fd, int channel, const char *fmt, ...);

# DESCRIPTION

Convenience wrapper around `json_frame_write(3)` that formats the payload
using `printf`-style format specifiers.  The formatted string is passed as
the frame payload with `flags` set to 0.

The function first attempts formatting into a stack-allocated buffer of
`SERVER_MAX_LINE` (8192) bytes.  If the formatted output exceeds this size,
a heap buffer is dynamically allocated.

This is the primary function used throughout the v2 session handler for
sending JSON responses.

# PARAMETERS

| Parameter | Type           | Description                                   |
|-----------|----------------|-----------------------------------------------|
| `fd`      | `int`          | File descriptor to write to.                  |
| `channel` | `int`          | Channel ID (0--5).                            |
| `fmt`     | `const char *` | `printf`-style format string.                 |
| `...`     | variadic       | Arguments for the format string.              |

# RETURN VALUE

Returns **0** on success.

Returns **-1** on error (format failure, allocation failure, or write error).

# ERRORS

Returns -1 when:

- `vsnprintf(3)` returns a negative value.
- `malloc(3)` fails for oversized output.
- The underlying `json_frame_write(3)` call fails.

# EXAMPLES

```c
/* Authentication success response */
json_frame_write_fmt(wfd, CHAN_CONTROL,
    "{\"type\":\"auth_ok\",\"capabilities\":[\"state\",\"command\"]}");

/* Command completion with exit code */
json_frame_write_fmt(wfd, CHAN_COMMAND,
    "{\"type\":\"complete\",\"id\":\"%s\",\"exit_code\":%d}",
    request_id, exit_code);

/* Error response */
json_frame_write_fmt(wfd, CHAN_STATE,
    "{\"type\":\"error\",\"message\":\"%s\"}", escaped_message);
```

# SEE ALSO

`json_frame_write(3)`, `json_frame_read(3)`,
`protocol_write_line(3)`, `vsnprintf(3)`

# NOTES

- The `flags` field is always 0.  To write frames with non-zero flags,
  use `json_frame_write(3)` directly.

- If the formatted output exceeds `SERVER_MAX_LINE` bytes, a heap buffer
  is allocated, used, and freed automatically.

- The format string should produce valid JSON.  No JSON validation is
  performed by this function.

- The actual wire format (binary or NDJSON) is determined by the
  process-global wire format setting, inherited from
  `json_set_wire_format(3)`.
