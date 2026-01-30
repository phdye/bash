# bc\_pty\_resize(3) -- resize the PTY terminal dimensions

# SYNOPSIS

    #include "bashclient.h"

    int bc_pty_resize(bc_client_t *c, int rows, int cols);

# DESCRIPTION

Resizes the active PTY session's terminal to **rows** x **cols**.
The server applies `TIOCSWINSZ` to the PTY master file descriptor
and sends `SIGWINCH` to the child process group, notifying terminal-
aware applications (such as editors, pagers, and shells) of the
dimension change.

A PTY session must have been previously spawned with
`bc_pty_spawn(3)`.  If no PTY session is active, this function
returns `BC_ERR_PROTOCOL`.

# Wire protocol

Sends on `CHAN_PTY` (channel 5):

```json
{"action":"resize","rows":40,"cols":120}
```

Receives acknowledgement:

```json
{"type":"resized","rows":40,"cols":120}
```

# PARAMETERS

| Parameter | Type            | Description                                    |
|-----------|-----------------|------------------------------------------------|
| `c`       | `bc_client_t *` | Connected and authenticated client handle.     |
| `rows`    | `int`           | New terminal height in rows. Must be > 0.      |
| `cols`    | `int`           | New terminal width in columns. Must be > 0.    |

# RETURN VALUE

Returns **BC\_OK** (0) on success.  The server has applied the new
dimensions and signalled the child.

Returns a negative error code on failure:

| Code | Condition |
|------|-----------|
| `BC_ERR_PARAM` (-7) | `c` is NULL, or rows/cols are not positive. |
| `BC_ERR_PROTOCOL` (-2) | No PTY session is active, or unexpected response. |
| `BC_ERR_TRANSPORT` (-4) | Connection lost or write/read failure. |
| `BC_ERR_TIMEOUT` (-3) | No response within the client timeout period. |
| `BC_ERR_AUTH` (-1) | Client is not authenticated. |

# ERRORS

If the child process has already exited, the resize request may
succeed at the protocol level (the ioctl still applies to the PTY
master) but has no practical effect.

# EXAMPLES

# Respond to local terminal resize

```c
#include <signal.h>
#include <sys/ioctl.h>

static bc_client_t *g_client;

void handle_winch(int sig)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        bc_pty_resize(g_client, ws.ws_row, ws.ws_col);
    }
}

int main(void)
{
    g_client = bc_connect(sock_path, token);
    /* ... authenticate, spawn PTY ... */

    struct sigaction sa = { .sa_handler = handle_winch };
    sigaction(SIGWINCH, &sa, NULL);

    /* ... event loop ... */
}
```

# Explicit resize

```c
int rc = bc_pty_resize(c, 50, 132);
if (rc != BC_OK) {
    fprintf(stderr, "resize failed: %d\n", rc);
}
```

# SEE ALSO

`bc_pty_spawn(3)`, `bc_pty_write(3)`, `bc_pty_signal(3)`,
`bc_pty_close(3)`, `bc_pty_on_output(3)`, `bc_pty_on_exit(3)`,
`bc_poll(3)`, `bc_connect(3)`, `bash-server-client-c(7)`

# NOTES

- The resize is applied immediately on the server.  Applications
  running in the PTY receive `SIGWINCH` and can query the new
  dimensions via `TIOCGWINSZ`.

- There is no server-side validation of dimension limits beyond
  positivity.  Extremely large values (e.g., 10000x10000) are
  accepted but may cause issues with applications that allocate
  screen buffers.

- Frequent rapid resizes (e.g., during a window drag) are safe;
  only the final dimensions matter to the child, and `SIGWINCH`
  signals coalesce if the child has not yet handled the previous
  one.

- This function waits for the server acknowledgement before
  returning, ensuring the dimensions are applied by the time
  the call completes.
