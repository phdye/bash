# session_init(3) -- Initialize a client session structure

# SYNOPSIS

    #include "server.h"

    int session_init(client_session_t *session, int client_fd);

# DESCRIPTION

Initializes a `client_session_t` structure for a new client connection.
Zeroes the entire structure with `memset`, then sets the primary file
descriptor to `client_fd` and establishes default values for all fields.

The session starts unauthenticated, using protocol version `PROTOCOL_V1`
with `WIRE_BINARY` wire format.  The write file descriptor is set to `-1`,
indicating that the primary `fd` should be used for both reading and
writing (the common case for Unix domain sockets).  Pipe file descriptors
for stdout/stderr capture are initialized to `-1` (closed).

The session's `pid` field is set to the current process ID via `getpid()`.

This function must be called before `session_handle()` or any other
session operation.

# PARAMETERS

- **session** -- Pointer to the `client_session_t` structure to initialize.
  Must not be NULL.

- **client_fd** -- File descriptor for the connected client.  For socket
  transport, this is the fd returned by `server_accept_client()`.  For
  stdio transport, this is `STDIN_FILENO`.  For `--fd` transport, this is
  the inherited file descriptor number.

# RETURN VALUE

Returns `0` on success.  Currently always succeeds.

# ERRORS

None.  The function does not perform any system calls that can fail.

# SEE ALSO

`session_cleanup`(3), `session_handle`(3), `server_accept_client`(3)

# NOTES

After `session_init()`, the caller may set `session->write_fd` to a
different file descriptor if the read and write channels are separate
(e.g., stdio mode where `fd=STDIN_FILENO` and `write_fd=STDOUT_FILENO`).

The `client_session_t` structure is defined in `server.h`:

    typedef struct client_session {
        int   fd;              /* Primary fd (read, or read+write) */
        int   write_fd;        /* Write fd (-1 = use fd for both) */
        int   authenticated;   /* 1 after successful AUTH */
        int   protocol_version;/* PROTOCOL_V1, V2, or V2_NDJSON */
        int   wire_format;     /* WIRE_BINARY or WIRE_NDJSON */
        pid_t pid;
        int   stdout_pipe[2];
        int   stderr_pipe[2];
    } client_session_t;
