# session_cleanup(3) -- Clean up client session resources

# SYNOPSIS

    #include "server.h"

    void session_cleanup(client_session_t *session);

# DESCRIPTION

Releases all resources associated with a client session.  Closes all open
file descriptors owned by the session and resets fields to safe values.

The cleanup sequence is:

1. If `write_fd` is open (`>= 0`) and distinct from `fd`, close `write_fd`
   and set it to `-1`.
2. Close `fd` if open (`>= 0`) and set it to `-1`.
3. Close both ends of `stdout_pipe` if open (`>= 0`).
4. Close both ends of `stderr_pipe` if open (`>= 0`).

This function is safe to call multiple times on the same session.  After
cleanup, the session structure should not be reused without calling
`session_init()` again.

# PARAMETERS

- **session** -- Pointer to the `client_session_t` structure to clean up.
  Must not be NULL.  The structure must have been previously initialized
  by `session_init()`.

# RETURN VALUE

None.

# ERRORS

None.  Errors from `close()` are silently ignored.

# SEE ALSO

`session_init`(3), `session_handle`(3)

# NOTES

Called automatically after `session_handle()` returns in the main server
loop, and also on error paths during connection setup.

The function does not free the `client_session_t` structure itself, as it
is typically stack-allocated.

Care is taken to avoid double-closing: `write_fd` is only closed if it
differs from `fd`, preventing a double-close when both point to the same
socket descriptor (the common case for Unix domain socket transport).
