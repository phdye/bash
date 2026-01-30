# SERVER_ACCEPT_CLIENT(3) — Accept a Client Connection

## NAME

server_accept_client — accept an incoming client connection on the server socket

## SYNOPSIS

```c
#include "server.h"

int server_accept_client(int server_fd);
```

## DESCRIPTION

Accepts a pending connection on the listening socket **server_fd**.
Sets the `FD_CLOEXEC` flag on the accepted file descriptor to prevent
it from being inherited by child processes created via `fork()`/`exec()`.

## PARAMETERS

**server_fd**
:   Listening socket file descriptor (as returned by `server_socket_create()`).

## RETURN VALUE

On success, returns the connected client file descriptor (non-negative).

On error, returns **-1** with `errno` set (e.g., `EINTR` if interrupted
by a signal).

## NOTES

- The `FD_CLOEXEC` flag ensures that forked child processes (from EVAL)
  do not inherit the client socket.
- The caller (main accept loop) checks for `EINTR` to handle signal
  interruption gracefully.
- The client address is accepted but not inspected (Unix domain sockets
  do not provide meaningful address information via `accept()`).

## SOURCE

`server_socket.c:113`

## SEE ALSO

[server_socket_create.md](server_socket_create.md), [session_init.md](session_init.md)
