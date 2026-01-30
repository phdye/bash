# bc\_recv\_msg(3) -- receive a raw JSON message on a channel

# SYNOPSIS

    #include "bashclient.h"

    int bc_recv_msg(bc_client_t *c, int channel, char **json,
                    int timeout_ms);

# DESCRIPTION

Receives the next JSON message on the specified v2 protocol channel.
Blocks up to **timeout\_ms** milliseconds waiting for a message.  If
**timeout\_ms** is 0, the function returns immediately if no message
is available (non-blocking mode).

The library handles wire framing (NDJSON or binary) internally and
delivers the JSON payload as a NUL-terminated string.  The `"ch"`
field is stripped from the payload in NDJSON mode.

Messages arriving on channels other than the requested one are
queued internally and delivered by subsequent `bc_recv_msg` calls
for those channels, or processed by `bc_poll(3)` and its registered
callbacks.

This is a **low-level function** intended for protocol extensions
and debugging.  Most callers should prefer channel-specific functions
that parse the JSON response automatically.

# Queuing behavior

The client library maintains a per-channel message queue.  When a
message arrives on channel N but the caller is waiting on channel M,
the message for channel N is enqueued.  This allows interleaved
messages from different channels to be processed correctly.

The queue has a bounded size (default: 64 messages per channel).
If a channel's queue is full, the oldest message is discarded and
a diagnostic is logged.

# PARAMETERS

| Parameter    | Type            | Description                                    |
|--------------|-----------------|------------------------------------------------|
| `c`          | `bc_client_t *` | Connected and authenticated client handle.     |
| `channel`    | `int`           | Channel ID to receive from (0--5).             |
| `json`       | `char **`       | Receives `malloc`'d JSON payload (caller frees with `bc_free`). |
| `timeout_ms` | `int`           | Maximum wait time in milliseconds. 0 = non-blocking. -1 = block indefinitely. |

# RETURN VALUE

Returns **BC\_OK** (0) on success.  `*json` points to a `malloc`'d
NUL-terminated JSON string.  The caller must free it with
`bc_free(3)`.

Returns a negative error code on failure:

| Code | Condition |
|------|-----------|
| `BC_ERR_PARAM` (-7) | `c` or `json` is NULL, or `channel` is out of range. |
| `BC_ERR_TIMEOUT` (-3) | No message received within `timeout_ms`. |
| `BC_ERR_TRANSPORT` (-4) | Connection lost or read failure. |
| `BC_ERR_PROTOCOL` (-2) | Client is using v1 protocol, or malformed frame. |
| `BC_ERR_AUTH` (-1) | Client is not authenticated. |
| `BC_ERR_NOMEM` (-6) | Failed to allocate response buffer. |

On error, `*json` is set to NULL.

# ERRORS

If the connection is closed by the server (EOF), the function
returns `BC_ERR_TRANSPORT`.  The client should call
`bc_disconnect(3)` to clean up.

If `timeout_ms` is -1 (block indefinitely), the function may block
forever if the server never sends a message on the requested
channel.  Use bounded timeouts in production code.

# EXAMPLES

# Blocking receive with timeout

```c
char *json;
int rc = bc_recv_msg(c, BC_CHAN_CONTROL, &json, 5000);
switch (rc) {
case BC_OK:
    printf("Received: %s\n", json);
    bc_free(json);
    break;
case BC_ERR_TIMEOUT:
    fprintf(stderr, "No response within 5 seconds\n");
    break;
default:
    fprintf(stderr, "Receive error: %d\n", rc);
    break;
}
```

# Non-blocking poll

```c
char *json;
int rc = bc_recv_msg(c, BC_CHAN_COMMAND, &json, 0);
if (rc == BC_OK) {
    printf("Got command response: %s\n", json);
    bc_free(json);
} else if (rc == BC_ERR_TIMEOUT) {
    /* No message available — try again later */
}
```

# Drain all messages on a channel

```c
char *json;
while (bc_recv_msg(c, BC_CHAN_COMMAND, &json, 0) == BC_OK) {
    printf("Message: %s\n", json);
    bc_free(json);
}
```

# Send/receive round-trip

```c
/* Send eval request */
int rc = bc_send_msg(c, BC_CHAN_COMMAND,
    "\"action\":\"eval\",\"cmd\":\"echo hello\"");
if (rc != BC_OK) {
    fprintf(stderr, "send failed: %d\n", rc);
    return;
}

/* Collect responses until completion */
char *json;
while (bc_recv_msg(c, BC_CHAN_COMMAND, &json, 10000) == BC_OK) {
    printf("Response: %s\n", json);

    /* Check for completion marker */
    if (strstr(json, "\"type\":\"complete\"")) {
        bc_free(json);
        break;
    }
    bc_free(json);
}
```

# Multiplexed channel reading

```c
/* Read from multiple channels using non-blocking receives */
char *json;

if (bc_recv_msg(c, BC_CHAN_CONTROL, &json, 0) == BC_OK) {
    handle_control(json);
    bc_free(json);
}
if (bc_recv_msg(c, BC_CHAN_OBSERVE, &json, 0) == BC_OK) {
    handle_observe(json);
    bc_free(json);
}
if (bc_recv_msg(c, BC_CHAN_COMMAND, &json, 0) == BC_OK) {
    handle_command(json);
    bc_free(json);
}
```

# SEE ALSO

`bc_send_msg(3)`, `bc_poll(3)`, `bc_free(3)`, `bc_connect(3)`,
`bash-server-ndjson(5)`, `bash-server-client-c(7)`

# NOTES

- The `*json` payload is the message content without the `"ch"`
  field (NDJSON) or without the binary header (binary mode).  In
  NDJSON mode, it is a valid JSON object string.

- Messages received while waiting for a specific channel are
  queued, not discarded.  This means `bc_recv_msg` on one channel
  does not lose messages destined for other channels.

- The per-channel queue is bounded (default 64 messages).  For
  high-throughput channels like `CHAN_OBSERVE` or `CHAN_PTY`,
  callers should drain messages frequently to avoid queue overflow.

- Using `timeout_ms = -1` is discouraged; prefer a finite timeout
  with retry logic to avoid indefinite hangs.

- The function uses `poll(2)` internally for the timeout mechanism,
  which has millisecond resolution.

- This function is not thread-safe.  Do not call `bc_recv_msg` and
  `bc_send_msg` concurrently from different threads on the same
  client handle.

- For event-driven architectures, prefer `bc_poll(3)` with
  registered callbacks over manual `bc_recv_msg` loops.
