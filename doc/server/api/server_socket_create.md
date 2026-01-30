# SERVER_SOCKET_CREATE(3) — Create and Bind a Unix Domain Socket

## NAME

server_socket_create — create, configure, bind, and listen on a Unix domain socket

## SYNOPSIS

```c
#include "server.h"

int server_socket_create(const char *path);
```

## DESCRIPTION

Creates a Unix domain stream socket (`AF_UNIX`, `SOCK_STREAM`), binds it
to the filesystem path **path**, sets file permissions to `0600`, and
begins listening for connections.

If a socket file already exists at **path**, it is unlinked before binding.
This handles stale socket files from previous server runs.

## PARAMETERS

**path**
:   Filesystem path for the Unix socket.  Must be shorter than
    `sizeof(struct sockaddr_un.sun_path)` (typically 108 bytes).

## RETURN VALUE

On success, returns the listening socket file descriptor (non-negative).

On error, returns **-1** with `errno` set:
- `ENAMETOOLONG` — path exceeds `sun_path` size
- `EACCES` — permission denied on bind
- Other `socket()`, `bind()`, or `listen()` errors

## OPERATIONS PERFORMED

1. Validate path length against `sun_path`.
2. `socket(AF_UNIX, SOCK_STREAM, 0)`.
3. `setsockopt(SOL_SOCKET, SO_REUSEADDR, 1)`.
4. `unlink(path)` — remove stale socket file.
5. `bind()` to the path.
6. `chmod(path, 0600)`.
7. `listen(fd, SOMAXCONN)`.

## NOTES

- On bind or listen failure, the socket fd is closed and errno is preserved.
- The listen backlog is set to `SOMAXCONN` (system maximum).
- The `SO_REUSEADDR` option is set but has limited effect on Unix domain
  sockets (primarily relevant for the stale file case).

## SOURCE

`server_socket.c:29`

## SEE ALSO

[server_socket_close.md](server_socket_close.md), [server_accept_client.md](server_accept_client.md),
[security.md](../security.md)
