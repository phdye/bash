# bc\_observe\_on\_post\_command(3) — register post-command observation callback

# SYNOPSIS

```c
#include <bashclient.h>

typedef struct {
    int         seq;            /* Monotonic sequence number */
    long long   timestamp;      /* Unix epoch milliseconds */
    char       *command;        /* Command string that executed */
    int         exit_status;    /* Exit status ($?) */
    int         signal_number;  /* Signal that killed the command, or 0 */
    int         duration_ms;    /* Wall-clock execution time in ms */
} bc_post_command_event_t;

typedef void (*bc_post_command_cb)(
    const bc_post_command_event_t *event,
    void *userdata
);

void bc_observe_on_post_command(bc_client_t *c,
                                bc_post_command_cb cb,
                                void *userdata);
```

# DESCRIPTION

Registers a callback function that is invoked each time the server
emits a `post_command` event on `CHAN_OBSERVE` (channel 3).
Post-command events fire immediately after the bash interpreter
finishes executing a command, including its exit status and
execution duration.

Only one post-command callback may be registered at a time.
Calling this function again replaces the previous callback and
userdata.  Pass NULL as `cb` to unregister the callback entirely.

The callback is invoked from within `bc_poll()` when a post-command
event message is received and decoded.  The `event` pointer is
valid only for the duration of the callback invocation; the caller
must copy any data it needs to retain.

**Event fields:**

| Field | Description |
|-------|-------------|
| `seq` | Sequence number matching the corresponding `pre_command` event |
| `timestamp` | Unix epoch time in milliseconds when the event was generated |
| `command` | The command string that was executed |
| `exit_status` | The command's exit status (0-255), equivalent to `$?` |
| `signal_number` | Signal that terminated the command, or 0 if it exited normally |
| `duration_ms` | Wall-clock execution time in milliseconds |

# PARAMETERS

- **c** — Pointer to a `bc_client_t` connection.  Must not be NULL.
  The client does not need to be authenticated or subscribed to
  register a callback; however, events will only be delivered after
  both authentication and subscription.

- **cb** — Callback function, or NULL to unregister.  The callback
  receives a pointer to a `bc_post_command_event_t` and the
  user-supplied `userdata` pointer.

- **userdata** — Opaque pointer passed through to the callback.
  May be NULL.  Not freed by the library.

# RETURN VALUE

None.  This function always succeeds.  If `c` is NULL, the call is
silently ignored.

# ERRORS

This function does not report errors.  Invalid parameters (NULL
client) are silently ignored.

# EXAMPLES

```c
#include <bashclient.h>
#include <stdio.h>

/* Track slow commands */
typedef struct {
    int threshold_ms;
    int slow_count;
} perf_monitor_t;

static void check_duration(const bc_post_command_event_t *ev,
                           void *ud)
{
    perf_monitor_t *mon = (perf_monitor_t *)ud;

    if (ev->duration_ms > mon->threshold_ms) {
        fprintf(stderr, "SLOW [%dms]: %s (exit=%d)\n",
                ev->duration_ms, ev->command, ev->exit_status);
        mon->slow_count++;
    }

    if (ev->signal_number != 0) {
        fprintf(stderr, "KILLED by signal %d: %s\n",
                ev->signal_number, ev->command);
    }
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    perf_monitor_t mon = { .threshold_ms = 100, .slow_count = 0 };

    /* Register callback and subscribe */
    bc_observe_on_post_command(c, check_duration, &mon);
    bc_observe_subscribe(c, BC_OBSERVE_LEVEL_COMMAND);

    /* Execute commands */
    bc_eval(c, "sleep 0.2", NULL, NULL, NULL);
    bc_eval(c, "echo fast", NULL, NULL, NULL);
    bc_eval(c, "sleep 0.5", NULL, NULL, NULL);

    /* Process all pending events */
    while (bc_poll(c, 1000) > 0)
        ;

    printf("Detected %d slow commands\n", mon.slow_count);

    /* Clean up */
    bc_observe_on_post_command(c, NULL, NULL);
    bc_observe_unsubscribe(c);
    bc_disconnect(c);
    return 0;
}
```

# SEE ALSO

`bc_observe_on_pre_command`(3), `bc_observe_subscribe`(3),
`bc_observe_unsubscribe`(3), `bc_poll`(3)

# NOTES

- The `event` struct and all its string fields are owned by the
  library and are freed after the callback returns.  Copy strings
  with `strdup()` if they are needed beyond the callback scope.
- The `seq` field correlates pre and post events for the same
  command.  A post-command event with `seq=N` always follows a
  pre-command event with the same `seq=N`.
- If a command is terminated by a signal, `signal_number` is set
  to the signal value and `exit_status` is set to `128 + signal`.
- `duration_ms` measures wall-clock time, not CPU time.  For
  background commands, this is the time until the command completes,
  not until it is launched.
- Callbacks execute synchronously within `bc_poll()`.  Keep
  callbacks short and non-blocking to avoid delaying event
  processing.
- Thread safety: callbacks are always invoked from the thread that
  calls `bc_poll()`.  The library is not thread-safe.
