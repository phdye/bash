# bc\_pty\_spawn(3) -- spawn a PTY session on the server

# SYNOPSIS

    #include "bashclient.h"

    typedef struct bc_pty_info {
        int rows;        /* actual terminal rows */
        int cols;        /* actual terminal columns */
        int pid;         /* child process PID on server */
        int strip_ansi;  /* 1 if ANSI stripping is active */
    } bc_pty_info_t;

    int bc_pty_spawn(bc_client_t *c, int rows, int cols,
                     const char *shell, int strip_ansi,
                     bc_pty_info_t *info);

# DESCRIPTION

Spawns a new PTY (pseudo-terminal) session on the bash-server.  The
server calls `forkpty(3)` to create a child process running the
specified shell, with the terminal dimensions set to **rows** x
**cols**.

The PTY channel (`CHAN_PTY`, channel 5) supports full interactive
terminal I/O.  Once spawned, the client can write input with
`bc_pty_write(3)`, receive output via `bc_pty_on_output(3)`, resize
the terminal with `bc_pty_resize(3)`, send signals with
`bc_pty_signal(3)`, and close the session with `bc_pty_close(3)`.

If **strip\_ansi** is non-zero, the server enables an ANSI escape
sequence filter on the PTY output stream.  This removes CSI, OSC,
charset designators, and other escape sequences before delivering
output to the client, which is useful for programmatic consumption
of terminal output.  See `ansi_strip(3)` for details on the filter.

Only one PTY session may be active per client connection at a time.
Calling `bc_pty_spawn` while a PTY is already active returns
`BC_ERR_PROTOCOL`.

# Wire protocol

Sends on `CHAN_PTY` (channel 5):

```json
{"action":"spawn","rows":24,"cols":80,"shell":"/bin/bash","strip_ansi":0}
```

Receives on `CHAN_PTY`:

```json
{"type":"spawned","rows":24,"cols":80,"pid":12345,"strip_ansi":0}
```

# PARAMETERS

| Parameter    | Type              | Description                                    |
|--------------|-------------------|------------------------------------------------|
| `c`          | `bc_client_t *`   | Connected and authenticated client handle.     |
| `rows`       | `int`             | Terminal height in rows (e.g., 24). Must be > 0. |
| `cols`       | `int`             | Terminal width in columns (e.g., 80). Must be > 0. |
| `shell`      | `const char *`    | Shell to execute, or NULL for server default (`/bin/bash`). |
| `strip_ansi` | `int`             | Non-zero to enable ANSI escape stripping.      |
| `info`       | `bc_pty_info_t *` | Receives spawn result details. May be NULL.    |

# RETURN VALUE

Returns **BC\_OK** (0) on success.  If **info** is non-NULL, the
structure is populated with the actual terminal dimensions, child PID,
and strip\_ansi status as confirmed by the server.

Returns a negative error code on failure:

| Code | Condition |
|------|-----------|
| `BC_ERR_PARAM` (-7) | `c` is NULL, or rows/cols are not positive. |
| `BC_ERR_PROTOCOL` (-2) | A PTY session is already active, or unexpected server response. |
| `BC_ERR_TRANSPORT` (-4) | Connection lost or write/read failure. |
| `BC_ERR_SERVER` (-5) | Server rejected the spawn request. |
| `BC_ERR_TIMEOUT` (-3) | No response within the client timeout period. |
| `BC_ERR_AUTH` (-1) | Client is not authenticated. |

# ERRORS

On error, any partially-constructed server-side PTY is cleaned up
automatically.  The **info** structure is zeroed on failure.

If the server cannot allocate a PTY (e.g., out of file descriptors),
it responds with an error frame and the function returns
`BC_ERR_SERVER`.

# EXAMPLES

# Basic PTY spawn

```c
bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", token);
if (!c) {
    fprintf(stderr, "connect failed\n");
    return 1;
}

bc_pty_info_t info;
int rc = bc_pty_spawn(c, 24, 80, NULL, 0, &info);
if (rc != BC_OK) {
    fprintf(stderr, "pty spawn failed: %d\n", rc);
    bc_disconnect(c);
    return 1;
}

printf("PTY spawned: %dx%d, pid=%d\n", info.cols, info.rows, info.pid);
```

# Spawn with ANSI stripping for programmatic use

```c
bc_pty_info_t info;
int rc = bc_pty_spawn(c, 24, 80, "/bin/sh", 1, &info);
if (rc == BC_OK && info.strip_ansi) {
    printf("ANSI stripping active — output is clean text\n");
}
```

# Spawn with custom shell

```c
bc_pty_info_t info;
int rc = bc_pty_spawn(c, 40, 120, "/usr/bin/zsh", 0, &info);
if (rc != BC_OK) {
    fprintf(stderr, "spawn failed: %d\n", rc);
}
```

# SEE ALSO

`bc_pty_write(3)`, `bc_pty_resize(3)`, `bc_pty_signal(3)`,
`bc_pty_close(3)`, `bc_pty_on_output(3)`, `bc_pty_on_exit(3)`,
`bc_poll(3)`, `bc_connect(3)`, `ansi_strip(3)`,
`bash-server-client-c(7)`

# NOTES

- The server creates a new `forkpty(3)` session for each spawn.  The
  child inherits the server's environment, modified by any prior
  `CHAN_STATE` variable operations.

- The **shell** parameter is passed to `execl(3)` on the server side.
  If the specified shell does not exist, the child exits immediately
  and a `bc_pty_on_exit` callback fires with a non-zero exit code.

- Terminal dimensions of 0 or negative values are rejected client-side
  before any message is sent to the server.

- The **pid** field in `bc_pty_info_t` is the server-side PID of the
  child process.  It is informational; the client cannot send POSIX
  signals to it directly.  Use `bc_pty_signal(3)` instead.

- If the connection is using v1 protocol, this function returns
  `BC_ERR_PROTOCOL` because PTY channels require v2.
