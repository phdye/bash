# bc\_observe\_on\_pre\_command(3) — register pre-command observation callback

# SYNOPSIS

```c
#include <bashclient.h>

typedef struct {
    int         seq;            /* Monotonic sequence number */
    long long   timestamp;      /* Unix epoch milliseconds */
    char       *command;        /* Command string about to execute */
    char       *cwd;            /* Working directory at invocation */
    int         line_number;    /* Source line number */
    int         is_subshell;    /* 1 if executing in a subshell */
    int         is_async;       /* 1 if background (&) command */
} bc_pre_command_event_t;

typedef void (*bc_pre_command_cb)(
    const bc_pre_command_event_t *event,
    void *userdata
);

void bc_observe_on_pre_command(bc_client_t *c,
                               bc_pre_command_cb cb,
                               void *userdata);
```

# DESCRIPTION

Registers a callback function that is invoked each time the server
emits a `pre_command` event on `CHAN_OBSERVE` (channel 3).
Pre-command events fire immediately before the bash interpreter
begins executing a command.

Only one pre-command callback may be registered at a time.  Calling
this function again replaces the previous callback and userdata.
Pass NULL as `cb` to unregister the callback entirely.

The callback is invoked from within `bc_poll()` when a pre-command
event message is received and decoded.  The `event` pointer is
valid only for the duration of the callback invocation; the caller
must copy any data it needs to retain.

**Event fields:**

| Field | Description |
|-------|-------------|
| `seq` | Monotonically increasing sequence number, shared with post-command events |
| `timestamp` | Unix epoch time in milliseconds when the event was generated |
| `command` | The command string about to be executed (may be truncated for very long commands) |
| `cwd` | Current working directory at the time of execution |
| `line_number` | Line number in the source script, or 0 for interactive input |
| `is_subshell` | Non-zero if the command is executing inside a subshell `(...)` |
| `is_async` | Non-zero if the command was launched with `&` |

# PARAMETERS

- **c** — Pointer to a `bc_client_t` connection.  Must not be NULL.
  The client does not need to be authenticated or subscribed to
  register a callback; however, events will only be delivered after
  both authentication and subscription.

- **cb** — Callback function, or NULL to unregister.  The callback
  receives a pointer to a `bc_pre_command_event_t` and the
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
#include <string.h>

/* Log all commands to a file */
typedef struct {
    FILE *logfile;
    int   count;
} logger_t;

static void log_pre_command(const bc_pre_command_event_t *ev,
                            void *ud)
{
    logger_t *log = (logger_t *)ud;
    fprintf(log->logfile,
            "[%lld] #%d %s:%d %s%s%s\n",
            ev->timestamp, ev->seq, ev->cwd, ev->line_number,
            ev->command,
            ev->is_subshell ? " (subshell)" : "",
            ev->is_async   ? " (async)"    : "");
    fflush(log->logfile);
    log->count++;
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    logger_t log = { .logfile = fopen("commands.log", "a"), .count = 0 };

    /* Register the callback */
    bc_observe_on_pre_command(c, log_pre_command, &log);

    /* Subscribe to events */
    bc_observe_subscribe(c, BC_OBSERVE_LEVEL_COMMAND);

    /* Execute several commands */
    bc_eval(c, "cd /tmp", NULL, NULL, NULL);
    bc_eval(c, "ls -la", NULL, NULL, NULL);
    bc_eval(c, "echo done", NULL, NULL, NULL);

    /* Process events — callbacks fire here */
    while (bc_poll(c, 500) > 0)
        ;

    printf("Logged %d pre-command events\n", log.count);

    /* Unregister callback */
    bc_observe_on_pre_command(c, NULL, NULL);

    fclose(log.logfile);
    bc_observe_unsubscribe(c);
    bc_disconnect(c);
    return 0;
}
```

# SEE ALSO

`bc_observe_on_post_command`(3), `bc_observe_subscribe`(3),
`bc_observe_unsubscribe`(3), `bc_poll`(3)

# NOTES

- The `event` struct and all its string fields are owned by the
  library and are freed after the callback returns.  Copy strings
  with `strdup()` if they are needed beyond the callback scope.
- The `seq` field correlates pre and post events for the same
  command execution.  A `pre_command` event with `seq=N` will be
  followed by a `post_command` event with the same `seq=N`.
- The `command` string may be truncated by the server if it exceeds
  the maximum event payload size (currently 64 KB).
- Callbacks execute synchronously within `bc_poll()`.  Long-running
  callbacks delay processing of subsequent messages.  Keep
  callbacks short and non-blocking.
- Registering a callback does not automatically subscribe to
  events.  Call `bc_observe_subscribe()` separately.
- Thread safety: callbacks are always invoked from the thread that
  calls `bc_poll()`.  The library is not thread-safe.
