# SERVER_SOCKET_CLOSE(3) — Close Socket and Clean Up

## NAME

server_socket_close — close a server socket file descriptor and remove the socket file

## SYNOPSIS

```c
#include "server.h"

void server_socket_close(int fd, const char *path);
```

## DESCRIPTION

Closes the socket file descriptor **fd** and unlinks the socket file at
**path**.  Either parameter may be invalid (fd < 0, path NULL) and will
be skipped.

## PARAMETERS

**fd**
:   Socket file descriptor to close.  If negative, `close()` is skipped.

**path**
:   Filesystem path of the socket file to unlink.  If NULL, `unlink()`
    is skipped.

## RETURN VALUE

None (void).

## NOTES

- Called during `server_shutdown()` as part of clean exit.
- Also used in test cleanup.
- Does not check return values of `close()` or `unlink()` — cleanup
  is best-effort.

## SOURCE

`server_socket.c:101`

## SEE ALSO

[server_socket_create.md](server_socket_create.md)
