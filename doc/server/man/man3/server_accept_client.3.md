# server_accept_client(3) -- Accept a new client connection

# SYNOPSIS

    #include "server.h"

    int server_accept_client(int server_fd);

# DESCRIPTION

Accepts a new client connection on the listening Unix domain socket.
Calls `accept()` on `server_fd` and sets the `FD_CLOEXEC` flag on the
returned client file descriptor to prevent leaking it to child processes
spawned by bash command execution.

# PARAMETERS

- **server_fd** -- The listening socket file descriptor, as returned by
  `server_socket_create()`.

# RETURN VALUE

Returns the client file descriptor on success (a non-negative integer).

Returns `-1` on error, with `errno` set appropriately.

# ERRORS

- **EINTR** -- The call was interrupted by a signal (typically `SIGINT` or
  `SIGTERM`).  The main accept loop checks `server_running` and retries.
- **ECONNABORTED** -- Connection aborted (on Cygwin, this can occur due
  to the `SO_PEERCRED` handshake race; see `server_socket_create`(3)).
- **EMFILE** / **ENFILE** -- Too many open file descriptors.
- Standard `accept()` errors.

# SEE ALSO

`server_socket_create`(3), `session_init`(3), `accept`(2), `fcntl`(2)

# NOTES

The `FD_CLOEXEC` flag is set via `fcntl(client_fd, F_SETFD, FD_CLOEXEC)`
immediately after accept.  This ensures the client socket is not inherited
by child processes created during `parse_and_execute()`.

The caller is responsible for closing the returned file descriptor when
the session ends, typically via `session_cleanup()`.

In the main server loop, `EINTR` is treated as a non-fatal condition.
When a signal sets `server_running = 0`, the loop exits naturally after
the interrupted `accept()` returns.

The function uses `struct sockaddr_storage` for the client address,
although for `AF_UNIX` sockets the address is not used after accept.
