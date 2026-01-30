# server_socket_create(3) -- Create and bind a Unix domain listening socket

# SYNOPSIS

    #include "server.h"

    int server_socket_create(const char *path, int no_peercred);

# DESCRIPTION

Creates an `AF_UNIX` stream socket, binds it to the filesystem `path`,
and begins listening for connections.

The function performs the following steps:

1. Validates that `path` fits within `sun_path` (typically 108 bytes).
2. Creates a `SOCK_STREAM` socket in the `AF_UNIX` domain.
3. Sets `SO_REUSEADDR` on the socket.
4. On Cygwin, if `no_peercred` is set, calls
   `setsockopt(fd, SOL_SOCKET, SO_PEERCRED, NULL, 0)` to disable the
   credential handshake (see NOTES).
5. Removes any existing socket file at `path` via `unlink()`.
6. Binds the socket to `path`.
7. Sets the socket file permissions to `0600` (owner read/write only).
8. Calls `listen()` with `SOMAXCONN` backlog.

# PARAMETERS

- **path** -- Filesystem path for the Unix domain socket.  Must be shorter
  than `sizeof(struct sockaddr_un.sun_path)` (typically 108 bytes).  The
  parent directory must already exist; use the server's
  `resolve_socket_path()` to create it if needed.

- **no_peercred** -- If non-zero, disables the Cygwin `SO_PEERCRED`
  credential handshake.  This flag has no effect on non-Cygwin platforms.
  Set to `1` when Python or other non-C clients will connect (see NOTES).

# RETURN VALUE

Returns the listening socket file descriptor on success (a non-negative
integer).

Returns `-1` on error, with `errno` set appropriately.

# ERRORS

- **ENAMETOOLONG** -- `path` exceeds `sun_path` capacity.
- **EACCES** -- Insufficient permissions to create the socket file.
- **EADDRINUSE** -- Another process has the path bound (rare after unlink).
- **ENOMEM** -- Insufficient kernel memory.
- Standard `socket()`, `bind()`, and `listen()` errors.

On `bind()` or `listen()` failure, the socket fd is closed and (for
`listen()`) the socket file is unlinked before returning `-1`.

# SEE ALSO

`server_socket_close`(3), `server_accept_client`(3), `socket`(2),
`bind`(2), `listen`(2)

# NOTES

# Cygwin SO_PEERCRED

Cygwin emulates `AF_UNIX` sockets over TCP loopback (`127.0.0.1`).  To
authenticate peers, it performs a credential handshake during
`connect()`/`accept()` that exchanges a secret and `ucred` structure.

CPython's `socket.connect()` uses a non-blocking connect + poll +
`getsockopt(SO_ERROR)` pattern that races with this handshake, causing
`ECONNABORTED` (errno 113) on `accept()`.

Calling `setsockopt(SO_PEERCRED, NULL, 0)` disables the handshake
entirely (`af_local_set_no_getpeereid` in Cygwin's `net.cc`), allowing
Python clients to connect reliably.  The tradeoff is that
`getpeereid()` and `getsockopt(SO_PEERCRED)` will no longer return peer
credentials.

**Important**: The `NULL, 0` form is required.  Passing non-NULL `optval`
or non-zero `optlen` returns `EINVAL`.

# Security

The socket file is created with `0600` permissions, restricting access to
the owning user.  The parent directory should also be restricted (the
server creates it with `0700`).  Combined with the auth token, this
provides defense in depth against local unauthorized access.
