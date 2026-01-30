# protocol_read_line(3) -- Read a single LF-terminated line from a file descriptor

# SYNOPSIS

    #include "server.h"

    int protocol_read_line(int fd, char *buf, size_t bufsize);

# DESCRIPTION

Reads a single line from the file descriptor `fd`, terminated by a newline
character (`\n`).  The trailing newline is stripped and not stored in `buf`.
Carriage return characters (`\r`) are silently discarded, allowing the
function to handle both LF and CRLF line endings transparently.

Reading proceeds one byte at a time via `read(2)`.  If `EINTR` is received,
the read is retried automatically.  Reading stops when a newline is
encountered, EOF is reached (with at least one byte already buffered), or
the buffer is full (`bufsize - 1` bytes stored).

The result is always NUL-terminated.

This function is used for the v1 text protocol.  For v2 framed I/O, see
`json_frame_read(3)`.

# PARAMETERS

| Parameter | Type     | Description                                      |
|-----------|----------|--------------------------------------------------|
| `fd`      | `int`    | File descriptor to read from (socket or pipe).   |
| `buf`     | `char *` | Destination buffer for the line content.          |
| `bufsize` | `size_t` | Size of `buf` in bytes.  Must be at least 2.     |

The recommended buffer size is `SERVER_MAX_LINE` (8192 bytes), which is the
maximum line length defined by the v1 protocol.

# RETURN VALUE

On success, returns the number of bytes stored in `buf` (excluding the
NUL terminator).  A return value of 0 indicates an empty line (bare `\n`).

Returns **-1** on error or if EOF is encountered with no bytes buffered.

# ERRORS

Returns -1 when:

- `read(2)` fails with an error other than `EINTR`.
- EOF is reached before any data has been read into `buf`.

The function does **not** set `errno` itself; the value left by `read(2)`
is preserved on error.

# EXAMPLES

```c
char line[SERVER_MAX_LINE];
int n;

n = protocol_read_line(client_fd, line, sizeof(line));
if (n < 0) {
    fprintf(stderr, "read error or client disconnected\n");
    return -1;
}

printf("received %d bytes: %s\n", n, line);
```

Typical v1 session read loop:

```c
char line[SERVER_MAX_LINE];
char cmd[64], arg[SERVER_MAX_LINE];

while (protocol_read_line(fd, line, sizeof(line)) >= 0) {
    if (protocol_parse_command(line, cmd, arg, sizeof(arg)) < 0)
        continue;
    /* dispatch on cmd */
}
```

# SEE ALSO

`protocol_write_line(3)`, `protocol_parse_command(3)`,
`json_frame_read(3)`, `read(2)`

# NOTES

- The function reads one byte at a time, which is simple but not optimal
  for high-throughput scenarios.  This is acceptable for the v1 protocol
  where commands are infrequent and short.

- The maximum useful line length is `bufsize - 1` bytes.  Lines exceeding
  this are silently truncated with the remainder left in the kernel buffer
  for the next read.

- `EINTR` is handled internally; callers do not need to retry.

- EOF mid-line (after at least one byte) is treated as a complete line.
  EOF with zero bytes buffered returns -1.
