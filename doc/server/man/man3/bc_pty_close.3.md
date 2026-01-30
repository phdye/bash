# bc\_pty\_close(3) -- close the PTY session

# SYNOPSIS

    #include "bashclient.h"

    int bc_pty_close(bc_client_t *c);

# DESCRIPTION

Closes the active PTY session on the server.  The server terminates
the child process (if still running), closes the PTY master file
descriptor, and releases all associated resources.

If the child process is still running when `bc_pty_close` is called,
the server sends `SIGHUP` followed by `SIGKILL` (after a brief
grace period) to ensure clean termination.  The `bc_pty_on_exit(3)`
callback, if registered, fires with the child's exit status before
the close acknowledgement is sent.

After this call returns successfully, the client may spawn a new PTY
session with `bc_pty_spawn(3)`.

If no PTY session is active, this function returns `BC_ERR_PROTOCOL`.

# Wire protocol

Sends on `CHAN_PTY` (channel 5):

```json
{"action":"close"}
```

Receives acknowledgement:

```json
{"type":"closed"}
```

If the child was still running, an exit event may precede the close
acknowledgement:

```json
{"type":"exit","exit_code":137}
{"type":"closed"}
```

# PARAMETERS

| Parameter | Type            | Description                                    |
|-----------|-----------------|------------------------------------------------|
| `c`       | `bc_client_t *` | Connected and authenticated client handle.     |

# RETURN VALUE

Returns **BC\_OK** (0) on success.  The PTY session has been fully
torn down on the server.

Returns a negative error code on failure:

| Code | Condition |
|------|-----------|
| `BC_ERR_PARAM` (-7) | `c` is NULL. |
| `BC_ERR_PROTOCOL` (-2) | No PTY session is active. |
| `BC_ERR_TRANSPORT` (-4) | Connection lost or write/read failure. |
| `BC_ERR_TIMEOUT` (-3) | No response within the client timeout period. |
| `BC_ERR_AUTH` (-1) | Client is not authenticated. |

# ERRORS

Even if the close fails at the protocol level (e.g., transport
error), the client-side PTY state is reset so that a subsequent
`bc_pty_spawn(3)` call can attempt a fresh session.

If the connection is lost before the close acknowledgement arrives,
the server-side cleanup still occurs (the child process is reaped
when the session file descriptors close).

# EXAMPLES

# Close after interactive session

```c
/* Spawn and interact with PTY */
bc_pty_spawn(c, 24, 80, NULL, 0, NULL);
bc_pty_write(c, "exit\n", 5);

/* Give the shell time to exit gracefully */
bc_poll(c, 1000);

/* Close the PTY session */
int rc = bc_pty_close(c);
if (rc != BC_OK) {
    fprintf(stderr, "close failed: %d\n", rc);
}
```

# Force-close a hung process

```c
/* Signal first, then close */
bc_pty_signal(c, "SIGKILL");
bc_poll(c, 500);
bc_pty_close(c);
```

# Clean shutdown pattern

```c
void cleanup_pty(bc_client_t *c)
{
    /* Send Ctrl-D to signal EOF */
    bc_pty_write(c, "\x04", 1);

    /* Wait up to 2 seconds for graceful exit */
    for (int i = 0; i < 20; i++) {
        bc_poll(c, 100);
        if (!bc_pty_active(c))
            return;
    }

    /* Force close if still running */
    bc_pty_close(c);
}
```

# SEE ALSO

`bc_pty_spawn(3)`, `bc_pty_write(3)`, `bc_pty_resize(3)`,
`bc_pty_signal(3)`, `bc_pty_on_output(3)`, `bc_pty_on_exit(3)`,
`bc_poll(3)`, `bc_connect(3)`, `bash-server-client-c(7)`

# NOTES

- The close is idempotent at the transport level: if the connection
  drops during close, the server still cleans up when the socket
  closes.

- After `bc_pty_close` returns, any registered output and exit
  callbacks remain registered but will not fire until a new PTY
  session is spawned.  Call `bc_pty_on_output(c, NULL, NULL)` and
  `bc_pty_on_exit(c, NULL, NULL)` to explicitly unregister them.

- The exit event delivered before the close acknowledgement reports
  the child's actual exit status.  If killed by signal, the exit
  code is 128 + signal number (e.g., 137 for `SIGKILL`).

- This function blocks until the server acknowledgement is received
  or the timeout expires.  For non-blocking close semantics, use
  `bc_send_msg(3)` directly and handle the response in
  `bc_poll(3)`.
