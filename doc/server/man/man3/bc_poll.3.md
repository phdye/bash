# bc\_poll(3) — poll for server-push messages and dispatch callbacks

# SYNOPSIS

```c
#include <bashclient.h>

int bc_poll(bc_client_t *c, int timeout_ms);
```

# DESCRIPTION

Polls the server connection for incoming push messages and
dispatches them to registered callbacks.  Push messages include
observation events (`CHAN_OBSERVE`), debug break-hit events
(`CHAN_DEBUG`), and PTY output (`CHAN_PTY`).

The function reads available messages from the connection and
invokes the appropriate registered callback for each message type:

| Message source | Callback registration function |
|----------------|-------------------------------|
| `pre_command` event | `bc_observe_on_pre_command()` |
| `post_command` event | `bc_observe_on_post_command()` |
| Break-hit event | `bc_debug_on_break_hit()` |
| PTY output | `bc_pty_on_output()` |

Messages for which no callback is registered are silently
discarded.

**Timeout behavior:**

| `timeout_ms` value | Behavior |
|--------------------|----------|
| 0 | Non-blocking: process already-buffered messages only |
| > 0 | Wait up to `timeout_ms` milliseconds for messages |
| -1 | Block indefinitely until at least one message arrives |

The function returns as soon as no more messages are immediately
available after processing at least one, or when the timeout
expires, whichever comes first.

Internally, `bc_poll()` uses `select()` or `poll()` on the
underlying file descriptor to wait for data availability.

# PARAMETERS

- **c** — Pointer to an authenticated `bc_client_t` connection.
  Must not be NULL.

- **timeout_ms** — Maximum time to wait for messages in
  milliseconds.  Use 0 for non-blocking, -1 for indefinite wait,
  or a positive value for a bounded wait.

# RETURN VALUE

Returns the number of messages processed (>= 0) on success.

Returns a negative error code on failure:

| Code | Meaning |
|------|---------|
| `BC_ERR_PARAM` (-7) | NULL client pointer |
| `BC_ERR_AUTH` (-1) | Client not authenticated |
| `BC_ERR_PROTOCOL` (-2) | Malformed message received |
| `BC_ERR_TRANSPORT` (-4) | Connection error or server disconnected |

A return value of 0 means the timeout expired with no messages
received (not an error).

# ERRORS

- Returns `BC_ERR_PARAM` if `c` is NULL.
- Returns `BC_ERR_AUTH` if the session is not authenticated.
- Returns `BC_ERR_TRANSPORT` if the server closes the connection
  or a network error occurs during the poll.
- Returns `BC_ERR_PROTOCOL` if a received message cannot be parsed
  as valid v2 JSON.

# EXAMPLES

# Non-blocking poll

```c
#include <bashclient.h>
#include <stdio.h>

void drain_events(bc_client_t *c)
{
    int n;
    while ((n = bc_poll(c, 0)) > 0)
        printf("Processed %d messages\n", n);

    if (n < 0)
        fprintf(stderr, "Poll error: %d\n", n);
}
```

# Event loop with timeout

```c
#include <bashclient.h>
#include <stdio.h>

static volatile int running = 1;

static void on_pre(const bc_pre_command_event_t *ev, void *ud)
{
    printf(">>> %s\n", ev->command);
}

static void on_post(const bc_post_command_event_t *ev, void *ud)
{
    printf("<<< %s [exit=%d, %dms]\n",
           ev->command, ev->exit_status, ev->duration_ms);
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    bc_observe_on_pre_command(c, on_pre, NULL);
    bc_observe_on_post_command(c, on_post, NULL);
    bc_observe_subscribe(c, BC_OBSERVE_LEVEL_COMMAND);

    /* Main event loop */
    while (running) {
        int n = bc_poll(c, 1000);
        if (n < 0) {
            fprintf(stderr, "Connection lost: %d\n", n);
            break;
        }
        /* n == 0 means timeout, loop again */
    }

    bc_observe_unsubscribe(c);
    bc_disconnect(c);
    return 0;
}
```

# Combined observe and debug polling

```c
#include <bashclient.h>
#include <stdio.h>

static void on_break(const bc_break_hit_event_t *ev, void *ud)
{
    printf("BREAK at line %d: %s (depth=%d)\n",
           ev->line, ev->command, ev->depth);
    /* Continue execution after inspecting state */
    bc_client_t *c = (bc_client_t *)ud;
    bc_debug_continue(c);
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    /* Set up both observe and debug callbacks */
    bc_observe_on_pre_command(c, my_pre_cb, NULL);
    bc_debug_on_break_hit(c, on_break, c);

    bc_observe_subscribe(c, BC_OBSERVE_LEVEL_COMMAND);
    bc_debug_enable(c);
    bc_debug_add_breakpoint(c, "line", NULL, 5, NULL);

    bc_eval(c, "source myscript.sh", NULL, NULL, NULL);

    /* Single poll loop handles all event types */
    while (bc_poll(c, 2000) >= 0)
        ;

    bc_disconnect(c);
    return 0;
}
```

# SEE ALSO

`bc_observe_on_pre_command`(3), `bc_observe_on_post_command`(3),
`bc_debug_on_break_hit`(3), `bc_observe_subscribe`(3),
`bc_debug_enable`(3)

# NOTES

- `bc_poll()` is the central event dispatch mechanism.  All
  server-push messages (observe events, debug breaks, PTY output)
  are delivered through `bc_poll()`.
- Callbacks execute synchronously in the calling thread during
  `bc_poll()`.  A callback that blocks will prevent other messages
  from being processed.
- It is safe to call other `bc_*` functions (such as
  `bc_debug_continue()`) from within a callback, but not
  `bc_poll()` itself (no re-entrant polling).
- When using `timeout_ms = -1`, ensure the server will eventually
  send a message or that another thread can close the connection;
  otherwise the call blocks indefinitely.
- The return count includes all dispatched messages regardless of
  channel.  Messages with no registered callback are counted as
  processed (discarded).
- Thread safety: `bc_poll()` must not be called concurrently with
  any other operation on the same `bc_client_t`.
