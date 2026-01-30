# server_signal_handler(3) -- Signal handler for server process signals

# SYNOPSIS

    #include "server.h"

    void server_signal_handler(int sig);

# DESCRIPTION

Async-signal-safe signal handler installed for `SIGINT`, `SIGTERM`,
`SIGCHLD`, and `SIGPIPE` in the server's main process.

The handler only sets `volatile sig_atomic_t` flags, which are then
checked by the main accept loop.  It does not perform any non-reentrant
operations (no `malloc`, no `printf`, no `write`).

Signal behavior:

| Signal    | Action                                               |
|-----------|------------------------------------------------------|
| `SIGINT`  | Sets `server_running = 0` to initiate graceful shutdown. |
| `SIGTERM` | Sets `server_running = 0` to initiate graceful shutdown. |
| `SIGCHLD` | Sets `got_sigchld = 1` to trigger zombie reaping.        |
| `SIGPIPE` | Ignored (no action).                                     |

# Signal Configuration

The signals are configured via `sigaction()` in `setup_signals()`:

- `SIGINT` and `SIGTERM` are installed **without** `SA_RESTART`, so that
  `accept()` in the main loop returns `EINTR` and the loop can check
  `server_running`.

- `SIGCHLD` is installed **with** `SA_RESTART`, so that child reaping
  does not interrupt ongoing I/O operations.

- `SIGPIPE` is installed with `SIG_IGN` (not this handler), so writes
  to closed sockets return `EPIPE` instead of killing the process.

# PARAMETERS

- **sig** -- The signal number.  One of `SIGINT`, `SIGTERM`, `SIGCHLD`,
  or `SIGPIPE`.

# RETURN VALUE

None.

# ERRORS

None.  The handler is async-signal-safe.

# SEE ALSO

`server_shutdown`(3), `server_daemonize`(3), `sigaction`(2), `signal`(7)

# NOTES

In child (session) processes, signal handlers are reset to defaults after
`fork()`:

    signal(SIGCHLD, SIG_DFL);
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);

This ensures that session processes respond to signals normally rather
than setting flags intended for the parent's accept loop.

The `server_running`, `got_sigchld`, and `active_clients` variables are
file-scoped (`static`) in `server_main.c`.  The handler modifies only
`server_running` and `got_sigchld`.

The `SIGPIPE` case in the switch statement is a defensive no-op.  In
practice, `SIGPIPE` is handled by `SIG_IGN` installed separately, and
this handler is not invoked for it.
