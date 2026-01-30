# bc\_pty\_write(3) -- write data to a PTY session

# SYNOPSIS

    #include "bashclient.h"

    int bc_pty_write(bc_client_t *c, const char *data, size_t len);

# DESCRIPTION

Writes **len** bytes from **data** to the active PTY session on the
server.  The data is base64-encoded before transmission over the wire
to preserve binary transparency (the PTY channel carries arbitrary
terminal data including control characters and escape sequences).

This function sends input to the PTY master file descriptor on the
server, which delivers it to the child process's standard input.
Common uses include sending keystrokes, command text, and terminal
control sequences (Ctrl-C as `"\x03"`, Ctrl-D as `"\x04"`, etc.).

A PTY session must have been previously spawned with
`bc_pty_spawn(3)`.  If no PTY session is active, this function
returns `BC_ERR_PROTOCOL`.

# Wire protocol

Sends on `CHAN_PTY` (channel 5):

```json
{"action":"write","data":"aGVsbG8gd29ybGQK"}
```

The server decodes the base64 payload and writes the raw bytes to
the PTY master fd.

# PARAMETERS

| Parameter | Type            | Description                                    |
|-----------|-----------------|------------------------------------------------|
| `c`       | `bc_client_t *` | Connected and authenticated client handle.     |
| `data`    | `const char *`  | Buffer of bytes to write to the PTY.           |
| `len`     | `size_t`        | Number of bytes to write. Must be > 0.         |

# RETURN VALUE

Returns **BC\_OK** (0) on success.  The data has been sent to the
server; actual delivery to the child process is asynchronous.

Returns a negative error code on failure:

| Code | Condition |
|------|-----------|
| `BC_ERR_PARAM` (-7) | `c` or `data` is NULL, or `len` is 0. |
| `BC_ERR_PROTOCOL` (-2) | No PTY session is active. |
| `BC_ERR_TRANSPORT` (-4) | Connection lost or write failure. |
| `BC_ERR_NOMEM` (-6) | Failed to allocate base64 encoding buffer. |
| `BC_ERR_AUTH` (-1) | Client is not authenticated. |

# ERRORS

If the child process has already exited, the write may succeed at
the protocol level (the server accepts the frame) but the data is
discarded.  The client learns of the exit through the
`bc_pty_on_exit(3)` callback.

Large writes are sent as a single frame.  The maximum payload size
is limited by `FRAME_MAX_PAYLOAD` (1 MB) after base64 encoding,
which allows approximately 750 KB of raw data per call.

# EXAMPLES

# Send a command string

```c
const char *cmd = "echo hello world\n";
int rc = bc_pty_write(c, cmd, strlen(cmd));
if (rc != BC_OK) {
    fprintf(stderr, "write failed: %d\n", rc);
}
```

# Send Ctrl-C to interrupt

```c
int rc = bc_pty_write(c, "\x03", 1);
if (rc != BC_OK) {
    fprintf(stderr, "failed to send SIGINT: %d\n", rc);
}
```

# Send binary data

```c
/* Send raw escape sequence: ESC [ 1 ; 3 1 m (red text) */
const char esc[] = "\x1b[1;31m";
bc_pty_write(c, esc, sizeof(esc) - 1);
```

# Interactive relay loop

```c
bc_pty_info_t info;
bc_pty_spawn(c, 24, 80, NULL, 0, &info);

/* Set up output callback */
bc_pty_on_output(c, my_output_handler, stdout);

/* Read from local stdin, write to PTY */
char buf[4096];
ssize_t n;
while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) {
    int rc = bc_pty_write(c, buf, (size_t)n);
    if (rc != BC_OK)
        break;
    bc_poll(c, 0);  /* process any pending output */
}
```

# SEE ALSO

`bc_pty_spawn(3)`, `bc_pty_resize(3)`, `bc_pty_signal(3)`,
`bc_pty_close(3)`, `bc_pty_on_output(3)`, `bc_pty_on_exit(3)`,
`bc_poll(3)`, `bc_connect(3)`, `bc_b64_encode(3)`,
`bash-server-client-c(7)`

# NOTES

- The base64 encoding is handled internally.  Callers pass raw
  bytes; no pre-encoding is necessary.

- Writes are not flow-controlled.  If the server-side PTY buffer
  fills (e.g., the child is not reading), subsequent writes on the
  server side may block until the child consumes data.

- For sending signals to the PTY child, prefer `bc_pty_signal(3)`
  over writing control characters, as it uses the server-side
  `kill(2)` which is more reliable.

- Zero-length writes (`len == 0`) are rejected with `BC_ERR_PARAM`
  to avoid sending empty frames.

- This function is synchronous with respect to the send: it blocks
  until the frame is written to the transport, but does not wait
  for the server to process it.
