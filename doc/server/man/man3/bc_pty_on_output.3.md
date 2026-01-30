# bc\_pty\_on\_output(3) -- register PTY output callback

# SYNOPSIS

    #include "bashclient.h"

    typedef void (*bc_pty_output_cb)(const char *data, size_t len,
                                     void *userdata);

    void bc_pty_on_output(bc_client_t *c, bc_pty_output_cb cb,
                          void *userdata);

# DESCRIPTION

Registers a callback function that is invoked whenever output data
arrives from the active PTY session.  The callback receives decoded
output bytes from the PTY child process.

The **data** passed to the callback is raw terminal output.  If ANSI
stripping was enabled in `bc_pty_spawn(3)`, the data has already been
filtered by the server-side `ansi_strip(3)` function and contains
no escape sequences.  Otherwise, it may contain ANSI escape
sequences, control characters, and arbitrary binary data.

**The callback is only invoked during `bc_poll(3)` calls.**  The
client must call `bc_poll` regularly (in an event loop or polling
cycle) for output to be delivered.

To unregister the callback, call `bc_pty_on_output(c, NULL, NULL)`.

# Wire protocol

Output arrives on `CHAN_PTY` (channel 5) as:

```json
{"type":"output","data":"bHMgLWxhCg=="}
```

The `data` field is base64-encoded.  The library decodes it before
invoking the callback.

# PARAMETERS

| Parameter  | Type                | Description                                 |
|------------|---------------------|---------------------------------------------|
| `c`        | `bc_client_t *`     | Connected and authenticated client handle.  |
| `cb`       | `bc_pty_output_cb`  | Callback function, or NULL to unregister.   |
| `userdata` | `void *`            | Opaque pointer passed to each callback invocation. |

# RETURN VALUE

None.  This function always succeeds.  If `c` is NULL, the call is
silently ignored.

# ERRORS

This function does not return errors.  However, the callback itself
may be invoked with partial data if a multi-byte character is split
across two PTY reads.  The caller is responsible for handling partial
UTF-8 sequences if needed.

# EXAMPLES

# Simple output printer

```c
void on_output(const char *data, size_t len, void *userdata)
{
    fwrite(data, 1, len, (FILE *)userdata);
    fflush((FILE *)userdata);
}

int main(void)
{
    bc_client_t *c = bc_connect(sock_path, token);
    bc_pty_spawn(c, 24, 80, NULL, 0, NULL);

    bc_pty_on_output(c, on_output, stdout);

    bc_pty_write(c, "ls -la\n", 7);

    /* Poll for output */
    for (int i = 0; i < 100; i++) {
        bc_poll(c, 50);  /* 50ms timeout */
    }

    bc_pty_close(c);
    bc_disconnect(c);
}
```

# Capture output to buffer

```c
typedef struct {
    char *buf;
    size_t len;
    size_t cap;
} capture_t;

void on_capture(const char *data, size_t len, void *userdata)
{
    capture_t *cap = (capture_t *)userdata;
    if (cap->len + len > cap->cap) {
        cap->cap = (cap->len + len) * 2;
        cap->buf = realloc(cap->buf, cap->cap);
    }
    memcpy(cap->buf + cap->len, data, len);
    cap->len += len;
}

int main(void)
{
    bc_client_t *c = bc_connect(sock_path, token);
    bc_pty_spawn(c, 24, 80, NULL, 1, NULL);  /* strip ANSI */

    capture_t cap = { .buf = malloc(4096), .cap = 4096 };
    bc_pty_on_output(c, on_capture, &cap);

    bc_pty_write(c, "echo hello\n", 11);

    /* Poll until output received */
    for (int i = 0; i < 50; i++)
        bc_poll(c, 100);

    /* Use captured output */
    printf("Got %zu bytes: %.*s\n", cap.len, (int)cap.len, cap.buf);

    free(cap.buf);
    bc_pty_close(c);
    bc_disconnect(c);
}
```

# Unregister callback

```c
/* Stop receiving output */
bc_pty_on_output(c, NULL, NULL);
```

# SEE ALSO

`bc_pty_spawn(3)`, `bc_pty_write(3)`, `bc_pty_resize(3)`,
`bc_pty_signal(3)`, `bc_pty_close(3)`, `bc_pty_on_exit(3)`,
`bc_poll(3)`, `bc_connect(3)`, `ansi_strip(3)`,
`bash-server-client-c(7)`

# NOTES

- The callback is invoked **synchronously** within `bc_poll(3)`.
  Long-running operations in the callback will block the event
  loop.  For compute-intensive processing, copy the data and
  handle it asynchronously.

- The **data** pointer is valid only for the duration of the
  callback.  The caller must copy the data if it needs to persist
  beyond the callback return.

- The **len** may be 0 in degenerate cases (e.g., a PTY read
  returned only ANSI sequences that were fully stripped).

- Multiple output events may be coalesced into a single callback
  invocation, or a single `write(2)` from the child may result
  in multiple callbacks, depending on buffering and read sizes.

- The callback registration persists across PTY close/spawn
  cycles.  If you spawn a new PTY after closing one, the same
  callback fires for the new session's output.

- Thread safety: `bc_pty_on_output` and `bc_poll` must be called
  from the same thread.  The callback is always invoked on the
  thread that calls `bc_poll`.
