# bc\_send\_msg(3) -- send a raw JSON message on a channel

# SYNOPSIS

    #include "bashclient.h"

    int bc_send_msg(bc_client_t *c, int channel, const char *json);

# DESCRIPTION

Sends a raw JSON message on the specified v2 protocol channel.  The
**json** string is the payload content without framing; the library
adds the appropriate wire framing (NDJSON or binary) based on the
negotiated wire format.

This is a **low-level function** intended for protocol extensions,
debugging, or cases where the channel-specific convenience functions
(e.g., `bc_pty_spawn(3)`, `bc_eval(3)`) are insufficient.  Most
callers should prefer the higher-level functions.

# Wire framing

**NDJSON mode** (default):  The message is sent as a single line:

    {"ch":<channel>,...json fields...}\n

The library merges the `"ch"` field into the JSON object.  The
**json** parameter should contain the inner fields without the
outer braces or `"ch"` field.  For example, if **json** is
`"\"action\":\"ping\""` and **channel** is 0, the wire output is:

    {"ch":0,"action":"ping"}\n

**Binary mode**:  The message is sent as a 6-byte header followed
by the JSON payload.  The **json** parameter is used as-is for the
payload.  The `"ch"` field is not automatically inserted in binary
mode (the channel is in the header).

# Channel IDs

| Constant | Value | Description |
|----------|-------|-------------|
| `BC_CHAN_CONTROL` | 0 | Auth, ping, disconnect, configure |
| `BC_CHAN_COMMAND` | 1 | eval, eval\_parsed |
| `BC_CHAN_STATE` | 2 | get/set/unset variables, functions, etc. |
| `BC_CHAN_OBSERVE` | 3 | Command observation events |
| `BC_CHAN_DEBUG` | 4 | Breakpoints, stepping, AST inspect |
| `BC_CHAN_PTY` | 5 | PTY spawn, I/O, resize, signal |

# PARAMETERS

| Parameter | Type            | Description                                    |
|-----------|-----------------|------------------------------------------------|
| `c`       | `bc_client_t *` | Connected and authenticated client handle.     |
| `channel` | `int`           | Channel ID (0--5).                             |
| `json`    | `const char *`  | JSON payload string (inner fields for NDJSON, full payload for binary). |

# RETURN VALUE

Returns **BC\_OK** (0) on success.  The message has been written to
the transport.

Returns a negative error code on failure:

| Code | Condition |
|------|-----------|
| `BC_ERR_PARAM` (-7) | `c` or `json` is NULL, or `channel` is out of range. |
| `BC_ERR_TRANSPORT` (-4) | Connection lost or write failure. |
| `BC_ERR_NOMEM` (-6) | Failed to allocate framing buffer. |
| `BC_ERR_AUTH` (-1) | Client is not authenticated. |
| `BC_ERR_PROTOCOL` (-2) | Client is using v1 protocol (v2 required). |

# ERRORS

The function does not validate that the JSON payload is well-formed.
Sending malformed JSON may cause the server to close the session or
return an error on the corresponding channel.

Messages exceeding `FRAME_MAX_PAYLOAD` (1 MB) after framing are
rejected with `BC_ERR_PARAM`.

# EXAMPLES

# Send a ping on the control channel

```c
int rc = bc_send_msg(c, BC_CHAN_CONTROL,
                     "\"action\":\"ping\"");
if (rc != BC_OK) {
    fprintf(stderr, "send failed: %d\n", rc);
}
```

# Send a custom state query

```c
int rc = bc_send_msg(c, BC_CHAN_STATE,
    "\"action\":\"get\",\"ns\":\"variable\",\"name\":\"PATH\"");
if (rc == BC_OK) {
    char *resp;
    int rrc = bc_recv_msg(c, BC_CHAN_STATE, &resp, 5000);
    if (rrc == BC_OK) {
        printf("Response: %s\n", resp);
        bc_free(resp);
    }
}
```

# Protocol debugging

```c
/* Send raw JSON and observe server response */
int rc = bc_send_msg(c, BC_CHAN_COMMAND,
    "\"action\":\"eval\",\"cmd\":\"echo test\"");
if (rc == BC_OK) {
    char *resp;
    while (bc_recv_msg(c, BC_CHAN_COMMAND, &resp, 5000) == BC_OK) {
        printf("CH1: %s\n", resp);
        bc_free(resp);
    }
}
```

# Send on PTY channel

```c
/* Equivalent to bc_pty_spawn(c, 24, 80, NULL, 0, NULL) */
int rc = bc_send_msg(c, BC_CHAN_PTY,
    "\"action\":\"spawn\",\"rows\":24,\"cols\":80,"
    "\"shell\":\"/bin/bash\",\"strip_ansi\":0");
```

# SEE ALSO

`bc_recv_msg(3)`, `bc_connect(3)`, `bc_poll(3)`,
`bash-server-ndjson(5)`, `bash-server-client-c(7)`

# NOTES

- In NDJSON mode, the library constructs the final JSON object by
  prepending `{"ch":N,` and appending `}\n` to the **json**
  parameter.  The **json** string must therefore be the inner
  key-value pairs without surrounding braces.

- In binary mode, the **json** parameter is sent as-is (it should
  be a complete JSON object).  The channel and flags are in the
  binary header.

- This function is synchronous: it blocks until the entire frame is
  written to the transport fd.  It does not wait for a server
  response; use `bc_recv_msg(3)` to read the response.

- Sending on a channel that requires prior setup (e.g., `CHAN_PTY`
  without a spawn) may cause server-side errors.  The server's
  error response arrives as a frame on the same channel.

- The function is not thread-safe.  Do not call `bc_send_msg` and
  `bc_recv_msg` concurrently from different threads on the same
  client handle.
