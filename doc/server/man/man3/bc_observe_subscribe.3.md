# bc\_observe\_subscribe(3) — subscribe to command observation events

# SYNOPSIS

```c
#include <bashclient.h>

#define BC_OBSERVE_LEVEL_OFF      0
#define BC_OBSERVE_LEVEL_COMMAND  1

int bc_observe_subscribe(bc_client_t *c, int level);
```

# DESCRIPTION

Subscribes the client to command observation events on `CHAN_OBSERVE`
(channel 3).  After a successful subscription, the server will push
pre-command and post-command event messages whenever the bash
interpreter executes commands.

The function sends a v2 JSON request on the control channel to set
the observe level, then waits for the server's acknowledgement.

**Observation levels:**

| Level | Constant | Events generated |
|-------|----------|-----------------|
| 0 | `BC_OBSERVE_LEVEL_OFF` | None (observation disabled) |
| 1 | `BC_OBSERVE_LEVEL_COMMAND` | `pre_command` and `post_command` events |

After subscribing, events are not delivered automatically.  The
caller must invoke `bc_poll()` in a loop to receive and dispatch
events to registered callbacks.  Events that arrive before callbacks
are registered are silently discarded.

A typical usage pattern is:

1. Register callbacks with `bc_observe_on_pre_command()` and/or
   `bc_observe_on_post_command()`.
2. Call `bc_observe_subscribe(c, BC_OBSERVE_LEVEL_COMMAND)`.
3. Execute commands via `bc_eval()`.
4. Call `bc_poll()` to process incoming events.

Calling `bc_observe_subscribe()` with level 1 when already
subscribed at level 1 is a no-op on the server side and returns
`BC_OK`.

# PARAMETERS

- **c** — Pointer to an authenticated `bc_client_t` connection.
  Must not be NULL.

- **level** — Desired observation level.
  - `BC_OBSERVE_LEVEL_OFF` (0): Disable observation (equivalent
    to calling `bc_observe_unsubscribe()`).
  - `BC_OBSERVE_LEVEL_COMMAND` (1): Enable command-level events.
  - Values outside the valid range return `BC_ERR_PARAM`.

# RETURN VALUE

Returns `BC_OK` (0) on success.

Returns a negative error code on failure:

| Code | Meaning |
|------|---------|
| `BC_ERR_PARAM` (-7) | NULL client or invalid level |
| `BC_ERR_AUTH` (-1) | Client not authenticated |
| `BC_ERR_PROTOCOL` (-2) | Unexpected server response |
| `BC_ERR_TIMEOUT` (-3) | Server did not respond in time |
| `BC_ERR_TRANSPORT` (-4) | Connection error |

# ERRORS

- If the client has not yet authenticated, the server rejects the
  request and the function returns `BC_ERR_AUTH`.
- If the connection is broken during the exchange, returns
  `BC_ERR_TRANSPORT`.

# EXAMPLES

```c
#include <bashclient.h>
#include <stdio.h>

static void on_pre(const bc_pre_command_event_t *ev, void *ud)
{
    printf("[pre] seq=%d cmd=%s cwd=%s line=%d\n",
           ev->seq, ev->command, ev->cwd, ev->line_number);
}

static void on_post(const bc_post_command_event_t *ev, void *ud)
{
    printf("[post] seq=%d cmd=%s exit=%d duration=%dms\n",
           ev->seq, ev->command, ev->exit_status, ev->duration_ms);
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;

    if (bc_auth(c, "mytoken") != BC_OK) {
        bc_disconnect(c);
        return 1;
    }

    /* Register callbacks before subscribing */
    bc_observe_on_pre_command(c, on_pre, NULL);
    bc_observe_on_post_command(c, on_post, NULL);

    /* Subscribe to command-level events */
    int rc = bc_observe_subscribe(c, BC_OBSERVE_LEVEL_COMMAND);
    if (rc != BC_OK) {
        fprintf(stderr, "subscribe failed: %d\n", rc);
        bc_disconnect(c);
        return 1;
    }

    /* Execute a command — events will be generated */
    bc_eval(c, "echo hello", NULL, NULL, NULL);

    /* Poll to receive and dispatch events */
    bc_poll(c, 1000);

    bc_observe_unsubscribe(c);
    bc_disconnect(c);
    return 0;
}
```

**Compile:**

```bash
gcc -o observe_demo observe_demo.c -lbashclient
```

# SEE ALSO

`bc_observe_unsubscribe`(3), `bc_observe_on_pre_command`(3),
`bc_observe_on_post_command`(3), `bc_poll`(3),
`observe_set_level`(3), `observe_init`(3)

# NOTES

- The subscription state is per-session.  Each authenticated
  session has its own independent observation level.
- Events are serialized as JSON on `CHAN_OBSERVE` and decoded by
  the client library before dispatch to callbacks.
- The server clamps out-of-range levels silently; the client
  library validates the level parameter before sending the request
  and returns `BC_ERR_PARAM` for invalid values.
- Subscribing at level 0 is equivalent to unsubscribing and is
  a valid operation.
- The function blocks until the server acknowledges the level
  change.  Use `bc_poll()` for event processing, not for the
  subscription handshake itself.
- Thread safety: the client library is not thread-safe.  Do not
  call `bc_observe_subscribe()` concurrently with other operations
  on the same `bc_client_t`.
