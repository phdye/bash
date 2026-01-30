# server_daemonize(3) -- Daemonize the server process

# SYNOPSIS

    #include "server.h"

    int server_daemonize(void);

# DESCRIPTION

Daemonizes the current process using the standard double-fork pattern to
ensure the daemon cannot acquire a controlling terminal.

The sequence is:

1. **First fork**: The parent exits immediately via `_exit(0)`.  The
   child continues.
2. **setsid()**: The child becomes a new session leader with no
   controlling terminal.
3. **Second fork**: The session leader exits via `_exit(0)`.  The
   grandchild continues as the daemon -- it is not a session leader,
   so it cannot acquire a controlling terminal by opening a TTY device.
4. **chdir("/")**: Changes to the root directory to avoid holding a
   mount point busy.
5. **Close stdio**: Closes `STDIN_FILENO`, `STDOUT_FILENO`, and
   `STDERR_FILENO`.
6. **Redirect to /dev/null**: Opens `/dev/null` and duplicates it to
   stdin, stdout, and stderr.

After this function returns successfully, the process is fully detached
from the controlling terminal and running as a background daemon.

# PARAMETERS

None.

# RETURN VALUE

Returns `0` on success in the final daemon process (the grandchild).

Returns `-1` if `fork()` or `setsid()` fails, with `errno` set.

**Note**: On success, this function does not return in the original
calling process or the intermediate child -- those processes exit via
`_exit(0)`.

# ERRORS

- **EAGAIN** -- `fork()` failed due to resource limits.
- **ENOMEM** -- `fork()` failed due to insufficient memory.
- **EPERM** -- `setsid()` failed (the process is already a session leader).

# SEE ALSO

`server_shutdown`(3), `server_signal_handler`(3), `fork`(2), `setsid`(2),
`daemon`(3)

# NOTES

This function is called early in `main()` when the `--daemon` flag is set,
before signal handlers are installed and before the socket is created.
This ensures the listening socket is created by the daemon process, not
the parent.

The `--daemon` flag is incompatible with `--stdio` and `--fd` transport
modes, which require the original stdin/stdout or inherited file
descriptors.  The server rejects this combination at startup.

After daemonization, diagnostic output goes nowhere (stderr is
`/dev/null`).  Use `--verbose` with `--pidfile` to debug daemon issues
by examining the PID file and sending signals.

The PID file (if requested via `--pidfile`) is written after daemonization
and socket creation, so it contains the daemon's actual PID.
