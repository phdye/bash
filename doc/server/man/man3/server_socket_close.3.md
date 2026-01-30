# server_socket_close(3) -- Close a listening socket and remove socket file

# SYNOPSIS

    #include "server.h"

    void server_socket_close(int fd, const char *path);

# DESCRIPTION

Closes the listening socket file descriptor and removes the associated
socket file from the filesystem.

If `fd` is non-negative, calls `close(fd)`.  If `path` is non-NULL,
calls `unlink(path)` to remove the socket file.  Either parameter may
safely be `-1` or `NULL` respectively.

This function is typically called during server shutdown to release the
listening socket and clean up the filesystem entry.

# PARAMETERS

- **fd** -- The listening socket file descriptor to close.  If negative,
  no close is performed.

- **path** -- Filesystem path to the Unix domain socket file.  If NULL,
  no unlink is performed.

# RETURN VALUE

None.

# ERRORS

Errors from `close()` and `unlink()` are silently ignored.  This is
intentional for shutdown paths where the socket may already be closed
or the file may already be removed.

# SEE ALSO

`server_socket_create`(3), `server_shutdown`(3), `close`(2), `unlink`(2)

# NOTES

Called by `server_shutdown()` as part of the clean shutdown sequence.
Also called on error paths during server startup if socket creation
succeeds but subsequent initialization (e.g., token generation) fails.

The function does not remove the parent directory.  Directory cleanup is
left to the operating system or the user.
