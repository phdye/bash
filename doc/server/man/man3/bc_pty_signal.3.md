# bc\_pty\_signal(3) -- send a signal to the PTY child process

# SYNOPSIS

    #include "bashclient.h"

    int bc_pty_signal(bc_client_t *c, const char *name);

# DESCRIPTION

Sends the signal identified by **name** to the PTY child process
on the server.  The server resolves the signal name to a number
and calls `kill(2)` on the child's process group.

This is the preferred mechanism for delivering signals to the PTY
child, rather than writing control characters (e.g., Ctrl-C as
`"\x03"`) via `bc_pty_write(3)`, because it bypasses the terminal
line discipline and is reliable regardless of the child's terminal
mode (raw, cbreak, or cooked).

A PTY session must have been previously spawned with
`bc_pty_spawn(3)`.  If no PTY session is active, this function
returns `BC_ERR_PROTOCOL`.

# Wire protocol

Sends on `CHAN_PTY` (channel 5):

```json
{"action":"signal","name":"SIGINT"}
```

Receives acknowledgement:

```json
{"type":"signal_sent","name":"SIGINT","pid":12345}
```

# Supported signals

The following signal names are recognized (case-sensitive, with or
without the `SIG` prefix):

| Name | Number | Common use |
|------|--------|------------|
| `SIGINT` / `INT` | 2 | Interrupt (Ctrl-C equivalent) |
| `SIGTERM` / `TERM` | 15 | Graceful termination |
| `SIGHUP` / `HUP` | 1 | Hangup / reload configuration |
| `SIGKILL` / `KILL` | 9 | Forced termination (cannot be caught) |
| `SIGQUIT` / `QUIT` | 3 | Quit with core dump |
| `SIGUSR1` / `USR1` | 10 | User-defined signal 1 |
| `SIGUSR2` / `USR2` | 12 | User-defined signal 2 |
| `SIGSTOP` / `STOP` | 19 | Suspend process (cannot be caught) |
| `SIGCONT` / `CONT` | 18 | Resume suspended process |
| `SIGTSTP` / `TSTP` | 20 | Terminal stop (Ctrl-Z equivalent) |
| `SIGWINCH` / `WINCH` | 28 | Window resize (prefer `bc_pty_resize`) |

# PARAMETERS

| Parameter | Type            | Description                                    |
|-----------|-----------------|------------------------------------------------|
| `c`       | `bc_client_t *` | Connected and authenticated client handle.     |
| `name`    | `const char *`  | Signal name (e.g., `"SIGINT"`, `"TERM"`).      |

# RETURN VALUE

Returns **BC\_OK** (0) on success.  The signal has been delivered to
the child process group.

Returns a negative error code on failure:

| Code | Condition |
|------|-----------|
| `BC_ERR_PARAM` (-7) | `c` or `name` is NULL, or `name` is empty. |
| `BC_ERR_PROTOCOL` (-2) | No PTY session is active, or unrecognized signal name. |
| `BC_ERR_TRANSPORT` (-4) | Connection lost or write/read failure. |
| `BC_ERR_SERVER` (-5) | Server-side `kill(2)` failed (e.g., child already exited). |
| `BC_ERR_TIMEOUT` (-3) | No response within the client timeout period. |
| `BC_ERR_AUTH` (-1) | Client is not authenticated. |

# ERRORS

If the child process has already exited, the server returns an error
and this function returns `BC_ERR_SERVER`.  The client should use
the `bc_pty_on_exit(3)` callback to detect child termination rather
than relying on signal delivery failure.

Unrecognized signal names cause the server to respond with an error,
resulting in `BC_ERR_PROTOCOL`.

# EXAMPLES

# Interrupt a running command

```c
int rc = bc_pty_signal(c, "SIGINT");
if (rc != BC_OK) {
    fprintf(stderr, "signal failed: %d\n", rc);
}
```

# Graceful shutdown sequence

```c
/* Try SIGTERM first */
int rc = bc_pty_signal(c, "SIGTERM");
if (rc != BC_OK) {
    /* Process may have already exited */
    goto cleanup;
}

/* Wait briefly for exit */
for (int i = 0; i < 10; i++) {
    bc_poll(c, 100);  /* 100ms poll */
    if (pty_exited)
        goto cleanup;
}

/* Force kill */
bc_pty_signal(c, "SIGKILL");

cleanup:
    bc_pty_close(c);
```

# Suspend and resume

```c
bc_pty_signal(c, "SIGTSTP");
/* ... do other work ... */
bc_pty_signal(c, "SIGCONT");
```

# SEE ALSO

`bc_pty_spawn(3)`, `bc_pty_write(3)`, `bc_pty_resize(3)`,
`bc_pty_close(3)`, `bc_pty_on_output(3)`, `bc_pty_on_exit(3)`,
`bc_poll(3)`, `bc_connect(3)`, `bash-server-client-c(7)`

# NOTES

- Signals are sent to the child's **process group** (negative PID),
  so all processes in the PTY session receive the signal, not just
  the shell itself.

- For window size changes, use `bc_pty_resize(3)` instead of sending
  `SIGWINCH` manually, as `bc_pty_resize` also updates the terminal
  dimensions via `TIOCSWINSZ`.

- Signal names are matched case-sensitively.  `"sigint"` is not
  recognized; use `"SIGINT"` or `"INT"`.

- The signal is delivered synchronously on the server side before the
  acknowledgement is sent.  When this function returns `BC_OK`, the
  `kill(2)` call has already completed.

- `SIGKILL` and `SIGSTOP` cannot be caught or ignored by the child.
  `SIGKILL` will always terminate the process; `SIGSTOP` will always
  suspend it.
