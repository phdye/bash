# server_shutdown(3) -- Perform clean server shutdown

# SYNOPSIS

    #include "server.h"

    void server_shutdown(void);

# DESCRIPTION

Performs a clean shutdown of the bash-server, releasing all server-level
resources.  This function is called from the main accept loop when
`server_running` becomes `0` (set by `server_signal_handler()` on
`SIGINT` or `SIGTERM`).

The shutdown sequence is:

1. **Close listening socket**: Calls `server_socket_close(server_fd,
   config.socket_path)` to close the socket file descriptor and remove
   the socket file from the filesystem.  Sets `server_fd = -1`.

2. **Remove token file**: If `config.auth_file` is set, calls
   `unlink(config.auth_file)` to remove the authentication token file
   (`<socket_path>.token`).

3. **Remove PID file**: If `config.pid_file` is set, calls
   `unlink(config.pid_file)` to remove the PID file.

4. **Reap children**: Calls `reap_children()` to collect exit status of
   any remaining session child processes via `waitpid()` with `WNOHANG`.

# PARAMETERS

None.  Operates on the file-scoped global variables `server_fd` and
`config` in `server_main.c`.

# RETURN VALUE

None.

# ERRORS

Errors during cleanup (e.g., `unlink()` failing because a file was
already removed) are silently ignored.  Shutdown is best-effort.

# SEE ALSO

`server_socket_close`(3), `server_signal_handler`(3),
`server_daemonize`(3), `server_socket_create`(3)

# NOTES

This function handles cleanup for the **socket transport** mode only.
The named pipe transport mode (`--named-pipe`) has its own inline cleanup
in `run_named_pipe_server()`.  The stdio/fd transport modes do not create
socket or token files, so `server_shutdown()` is not called for them.

Child process reaping uses `waitpid(-1, &status, WNOHANG)` in a loop,
which collects all terminated children without blocking.  Any children
still running at shutdown time will be orphaned (re-parented to init/PID 1)
and continue until they exit naturally or are killed externally.

The function does not remove the parent directory of the socket file.
On systems using `$XDG_RUNTIME_DIR`, the directory is managed by the
session infrastructure.  On fallback to `/tmp/bash-server-<uid>/`, the
directory persists until system cleanup.

The `config` structure's heap-allocated fields (`auth_token`, `auth_file`,
`socket_path`) are not freed, since `server_shutdown()` is followed
immediately by process exit.
