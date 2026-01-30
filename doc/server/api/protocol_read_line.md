# PROTOCOL_READ_LINE(3) — Read a Line from Socket

## NAME

protocol_read_line — read a newline-terminated line from a file descriptor

## SYNOPSIS

```c
#include "server.h"

int protocol_read_line(int fd, char *buf, size_t bufsize);
```

## DESCRIPTION

Reads bytes one at a time from file descriptor **fd** until a newline (`\n`)
is encountered, the buffer is full, or EOF/error occurs.

Carriage return characters (`\r`) are silently discarded, allowing both
LF and CRLF line endings.

The result is stored in **buf** as a NUL-terminated string without the
trailing newline.

## PARAMETERS

**fd**
:   File descriptor to read from (typically a connected socket).

**buf**
:   Output buffer.  Will be NUL-terminated on success.

**bufsize**
:   Size of the output buffer in bytes.  At most `bufsize - 1` data
    characters will be stored.

## RETURN VALUE

On success, returns the number of characters stored in **buf** (not
counting the NUL terminator).  May be 0 for an empty line.

On EOF with no data read, or on read error, returns **-1**.

On EOF after partial data, returns the number of characters read
(partial line without newline).

## ERRORS

Returns -1 when:
- `read()` returns 0 (EOF) with no data accumulated.
- `read()` returns -1 with `errno != EINTR`.

`EINTR` is handled internally (retries the read).

## NOTES

- Reads one byte at a time.  This is intentional for correctness in
  a line-oriented protocol over stream sockets, where message boundaries
  are defined by newlines.  Performance is acceptable for the expected
  message sizes (< 8 KB).
- The buffer is always NUL-terminated, even on partial reads.
- Maximum line length is bounded by `SERVER_MAX_LINE` (8,192 bytes).

## SOURCE

`server_protocol.c:52`

## SEE ALSO

[protocol_write_line.md](protocol_write_line.md), [protocol_parse_command.md](protocol_parse_command.md)
