# bc\_observe\_unsubscribe(3) — unsubscribe from observation events

# SYNOPSIS

```c
#include <bashclient.h>

int bc_observe_unsubscribe(bc_client_t *c);
```

# DESCRIPTION

Unsubscribes the client from command observation events on
`CHAN_OBSERVE` (channel 3).  This is equivalent to calling
`bc_observe_subscribe(c, BC_OBSERVE_LEVEL_OFF)`.

After unsubscribing, the server stops generating `pre_command` and
`post_command` events for this session.  Any events already in the
client's receive buffer are discarded on the next `bc_poll()` call.

Registered callbacks are **not** removed by this function.  They
remain in place so that re-subscribing later will resume delivery
without needing to re-register callbacks.  To explicitly remove
callbacks, pass NULL to `bc_observe_on_pre_command()` or
`bc_observe_on_post_command()`.

Calling `bc_observe_unsubscribe()` when not currently subscribed
is a no-op and returns `BC_OK`.

# PARAMETERS

- **c** — Pointer to an authenticated `bc_client_t` connection.
  Must not be NULL.

# RETURN VALUE

Returns `BC_OK` (0) on success.

Returns a negative error code on failure:

| Code | Meaning |
|------|---------|
| `BC_ERR_PARAM` (-7) | NULL client pointer |
| `BC_ERR_AUTH` (-1) | Client not authenticated |
| `BC_ERR_PROTOCOL` (-2) | Unexpected server response |
| `BC_ERR_TIMEOUT` (-3) | Server did not respond in time |
| `BC_ERR_TRANSPORT` (-4) | Connection error |

# ERRORS

- Returns `BC_ERR_PARAM` if `c` is NULL.
- Returns `BC_ERR_AUTH` if the session has not been authenticated.
- Returns `BC_ERR_TRANSPORT` if the connection is lost during the
  request.

# EXAMPLES

```c
#include <bashclient.h>
#include <stdio.h>

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    /* Subscribe, run some commands, then unsubscribe */
    bc_observe_on_pre_command(c, my_pre_cb, NULL);
    bc_observe_subscribe(c, BC_OBSERVE_LEVEL_COMMAND);

    bc_eval(c, "ls -la", NULL, NULL, NULL);
    bc_poll(c, 500);  /* Process events */

    /* Stop receiving events */
    int rc = bc_observe_unsubscribe(c);
    if (rc != BC_OK)
        fprintf(stderr, "unsubscribe failed: %d\n", rc);

    /* Commands after this point generate no events */
    bc_eval(c, "echo silent", NULL, NULL, NULL);
    bc_poll(c, 100);  /* No events dispatched */

    bc_disconnect(c);
    return 0;
}
```

# SEE ALSO

`bc_observe_subscribe`(3), `bc_observe_on_pre_command`(3),
`bc_observe_on_post_command`(3), `bc_poll`(3)

# NOTES

- The function sends a control-channel message to set the observe
  level to 0 and blocks until the server acknowledges.
- Callbacks registered via `bc_observe_on_pre_command()` and
  `bc_observe_on_post_command()` persist across subscribe/unsubscribe
  cycles.  Only event delivery is affected.
- Buffered events from before the unsubscribe are discarded during
  the next `bc_poll()` invocation, not immediately.
- Thread safety: the client library is not thread-safe.  Do not
  call `bc_observe_unsubscribe()` concurrently with other operations
  on the same `bc_client_t`.
