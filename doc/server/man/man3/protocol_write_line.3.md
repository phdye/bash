# protocol_write_line(3) -- Write a printf-formatted line with LF to a file descriptor

# SYNOPSIS

    #include "server.h"

    int protocol_write_line(int fd, const char *fmt, ...);

# DESCRIPTION

Formats a string using `vsnprintf(3)` with `printf`-style format specifiers,
appends a newline character (`\n`), and writes the result to the file
descriptor `fd`.

The function first attempts formatting into a stack-allocated buffer of
`SERVER_MAX_LINE` (8192) bytes.  If the formatted output exceeds this size
(e.g., base64-encoded STDOUT/STDERR payloads up to ~1.4 MB), a heap buffer
is dynamically allocated to accommodate the full output.

Writing uses `write(2)` in a loop to handle partial writes and `EINTR`
interruptions.

This function is used for v1 text protocol responses.  For v2 framed I/O,
see `json_frame_write(3)` and `json_frame_write_fmt(3)`.

# PARAMETERS

| Parameter | Type           | Description                                   |
|-----------|----------------|-----------------------------------------------|
| `fd`      | `int`          | File descriptor to write to (socket or pipe). |
| `fmt`     | `const char *` | `printf`-style format string.                 |
| `...`     | variadic       | Arguments for the format string.              |

# RETURN VALUE

On success, returns the total number of bytes written (including the
appended newline).

Returns **-1** on error (format failure, allocation failure, or write error).

# ERRORS

Returns -1 when:

- `vsnprintf(3)` returns a negative value (format error).
- `malloc(3)` fails when the formatted output exceeds the stack buffer.
- `write(2)` fails with an error other than `EINTR`.

# EXAMPLES

```c
/* Simple status response */
protocol_write_line(client_fd, "OK authenticated");

/* Response with formatted data */
protocol_write_line(client_fd, "EXIT %d", exit_code);

/* Base64-encoded output (may exceed 8KB stack buffer) */
char *b64 = protocol_base64_encode(stdout_data, stdout_len);
protocol_write_line(client_fd, "STDOUT %s", b64);
free(b64);
```

# SEE ALSO

`protocol_read_line(3)`, `json_frame_write(3)`,
`json_frame_write_fmt(3)`, `vsnprintf(3)`, `write(2)`

# NOTES

- The newline is always appended regardless of whether `fmt` already ends
  with one.  Do not include a trailing `\n` in the format string.

- For payloads larger than `SERVER_MAX_LINE - 2` bytes after formatting,
  a dynamic buffer is allocated and freed automatically.  This allows
  writing base64-encoded command output that can reach ~1.4 MB.

- `EINTR` during `write(2)` is handled internally; callers do not need
  to retry.

- Partial writes are retried in a loop until all bytes are sent.
