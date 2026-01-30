# PROTOCOL_WRITE_LINE(3) — Write a Formatted Line to Socket

## NAME

protocol_write_line — write a printf-formatted, newline-terminated line to a file descriptor

## SYNOPSIS

```c
#include "server.h"

int protocol_write_line(int fd, const char *fmt, ...);
```

## DESCRIPTION

Formats a string using `vsnprintf()`, appends a newline character (`\n`),
and writes the complete line to file descriptor **fd**.

The write is performed in a loop to handle partial writes, ensuring all
bytes are delivered.

## PARAMETERS

**fd**
:   File descriptor to write to (typically a connected socket).

**fmt**
:   printf-style format string.

**...**
:   Format arguments.

## RETURN VALUE

On success, returns the total number of bytes written (including the
newline).

On error, returns **-1**.

## ERRORS

Returns -1 when:
- `vsnprintf()` returns a negative value.
- `write()` returns -1 with `errno != EINTR`.

`EINTR` is handled internally (retries the write).

`SIGPIPE` is ignored at the process level, so writing to a disconnected
client returns -1 with `errno == EPIPE` rather than killing the server.

## NOTES

- The internal buffer is `SERVER_MAX_LINE` (8,192 bytes).  Messages
  longer than 8,189 bytes (8192 - 2 for newline - 1 for NUL) are truncated.
- The newline is always appended, even for an empty format string.
- This function is used for all server-to-client communication.

## EXAMPLES

```c
protocol_write_line(fd, "%s", RSP_OK);           // "OK\n"
protocol_write_line(fd, "%s %s", RSP_ERR, msg);  // "ERR reason\n"
protocol_write_line(fd, "%s %d", RSP_EXIT, 0);   // "EXIT 0\n"
protocol_write_line(fd, "%s %s", RSP_STDOUT, b64); // "STDOUT <base64>\n"
```

## SOURCE

`server_protocol.c:88`

## SEE ALSO

[protocol_read_line.md](protocol_read_line.md)
