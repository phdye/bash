# bc\_pty\_on\_exit(3) -- register PTY child exit callback

# SYNOPSIS

    #include "bashclient.h"

    typedef void (*bc_pty_exit_cb)(int exit_code, void *userdata);

    void bc_pty_on_exit(bc_client_t *c, bc_pty_exit_cb cb,
                        void *userdata);

# DESCRIPTION

Registers a callback function that is invoked when the PTY child
process exits.  The callback receives the child's exit code.

For normal exits, **exit\_code** is the value passed to `exit(3)` or
returned from `main()`.  For signal-caused deaths, **exit\_code** is
128 + signal number (e.g., 137 for `SIGKILL`, 130 for `SIGINT`).

**The callback is only invoked during `bc_poll(3)` calls.**  The
client must call `bc_poll` regularly for exit events to be delivered.

The exit callback fires at most once per PTY session.  After it
fires, the PTY session is in a terminated state, though
`bc_pty_close(3)` must still be called to release server-side
resources and allow a new session to be spawned.

To unregister the callback, call `bc_pty_on_exit(c, NULL, NULL)`.

# Wire protocol

Exit event arrives on `CHAN_PTY` (channel 5) as:

```json
{"type":"exit","exit_code":0}
```

# PARAMETERS

| Parameter  | Type              | Description                                  |
|------------|-------------------|----------------------------------------------|
| `c`        | `bc_client_t *`   | Connected and authenticated client handle.   |
| `cb`       | `bc_pty_exit_cb`  | Callback function, or NULL to unregister.    |
| `userdata` | `void *`          | Opaque pointer passed to the callback.       |

# RETURN VALUE

None.  This function always succeeds.  If `c` is NULL, the call is
silently ignored.

# ERRORS

This function does not return errors.  The exit code interpretation
follows standard POSIX conventions:

| Exit code | Meaning |
|-----------|---------|
| 0 | Normal successful exit |
| 1--125 | Application-defined error |
| 126 | Command invoked cannot execute (permission) |
| 127 | Command not found |
| 128+N | Killed by signal N |
| 255 | Exit status out of range |

# EXAMPLES

# Detect exit and print status

```c
void on_exit(int exit_code, void *userdata)
{
    int *exited = (int *)userdata;
    *exited = 1;

    if (exit_code == 0) {
        printf("PTY child exited normally\n");
    } else if (exit_code > 128) {
        printf("PTY child killed by signal %d\n", exit_code - 128);
    } else {
        printf("PTY child exited with code %d\n", exit_code);
    }
}

int main(void)
{
    bc_client_t *c = bc_connect(sock_path, token);
    bc_pty_spawn(c, 24, 80, NULL, 0, NULL);

    int exited = 0;
    bc_pty_on_exit(c, on_exit, &exited);

    /* Send exit command */
    bc_pty_write(c, "exit 42\n", 8);

    /* Poll until exit */
    while (!exited) {
        bc_poll(c, 100);
    }

    bc_pty_close(c);
    bc_disconnect(c);
}
```

# Combined output and exit handling

```c
static int g_exited = 0;

void on_output(const char *data, size_t len, void *ud)
{
    fwrite(data, 1, len, stdout);
}

void on_exit(int code, void *ud)
{
    g_exited = 1;
    printf("\n--- Process exited: %d ---\n", code);
}

int main(void)
{
    bc_client_t *c = bc_connect(sock_path, token);
    bc_pty_spawn(c, 24, 80, NULL, 0, NULL);

    bc_pty_on_output(c, on_output, NULL);
    bc_pty_on_exit(c, on_exit, NULL);

    bc_pty_write(c, "echo hello && exit\n", 19);

    while (!g_exited)
        bc_poll(c, 100);

    bc_pty_close(c);
    bc_disconnect(c);
}
```

# Automatic respawn on exit

```c
void on_exit_respawn(int code, void *userdata)
{
    bc_client_t *c = (bc_client_t *)userdata;
    fprintf(stderr, "Shell exited (%d), respawning...\n", code);
    bc_pty_close(c);
    bc_pty_spawn(c, 24, 80, NULL, 0, NULL);
}
```

# SEE ALSO

`bc_pty_spawn(3)`, `bc_pty_write(3)`, `bc_pty_resize(3)`,
`bc_pty_signal(3)`, `bc_pty_close(3)`, `bc_pty_on_output(3)`,
`bc_poll(3)`, `bc_connect(3)`, `bash-server-client-c(7)`

# NOTES

- The exit callback fires **before** the close acknowledgement in
  `bc_pty_close(3)`.  If close triggers a kill, the sequence is:
  exit event callback, then close returns `BC_OK`.

- Any remaining buffered output is delivered via
  `bc_pty_on_output(3)` before the exit callback fires.  This
  ensures the client receives all output from the child.

- The callback is invoked **synchronously** within `bc_poll(3)`.
  Spawning a new PTY from within the exit callback is supported
  but the new session's events will not be processed until the
  next `bc_poll` call.

- The callback registration persists across PTY close/spawn
  cycles.  If you close one session and spawn another, the same
  exit callback applies to the new session.

- Thread safety: `bc_pty_on_exit` and `bc_poll` must be called from
  the same thread.  The callback is always invoked on the thread
  that calls `bc_poll`.
