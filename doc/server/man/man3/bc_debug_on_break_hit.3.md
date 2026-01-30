# bc\_debug\_on\_break\_hit(3) — register break-hit event callback

# SYNOPSIS

```c
#include <bashclient.h>

typedef struct {
    int    line;        /* Source line number */
    char  *command;     /* Command string at the breakpoint */
    int    depth;       /* Execution nesting depth */
} bc_break_hit_event_t;

typedef void (*bc_break_hit_cb)(
    const bc_break_hit_event_t *event,
    void *userdata
);

void bc_debug_on_break_hit(bc_client_t *c,
                           bc_break_hit_cb cb,
                           void *userdata);
```

# DESCRIPTION

Registers a callback function that is invoked each time the
debugger stops at a breakpoint or after a step/next/finish
operation.  The callback receives a `bc_break_hit_event_t`
describing the stopped command.

Only one break-hit callback may be registered at a time.  Calling
this function again replaces the previous callback and userdata.
Pass NULL as `cb` to unregister the callback entirely.

The callback is invoked from within `bc_poll()` when a break-hit
event message is received from `CHAN_DEBUG` (channel 4).  The
`event` pointer is valid only for the duration of the callback
invocation; the caller must copy any data it needs to retain.

**Event fields:**

| Field | Description |
|-------|-------------|
| `line` | Source line number of the stopped command, or 0 for interactive input |
| `command` | The command string about to be executed |
| `depth` | Nesting depth (0 = top level, increments for functions/subshells) |

**Common callback actions:**

Within the callback, the following debug operations are typically
performed:

1. Inspect the current state with `bc_debug_inspect_ast()` or
   `bc_debug_status()`.
2. Query shell variables with `bc_state_get()`.
3. Choose a resumption strategy:
   - `bc_debug_continue()` — run to next breakpoint
   - `bc_debug_step()` — step into
   - `bc_debug_next()` — step over
   - `bc_debug_finish()` — run until block returns
   - `bc_debug_skip()` — skip without executing

If no resumption command is issued from the callback, execution
remains paused.  The next `bc_poll()` call will not receive
further events until a resumption command is sent.

# PARAMETERS

- **c** — Pointer to a `bc_client_t` connection.  Must not be
  NULL.  The client does not need to be authenticated or have
  the debugger enabled to register a callback; however, events
  will only be delivered when the debugger is active and stopped.

- **cb** — Callback function, or NULL to unregister.  The
  callback receives a pointer to a `bc_break_hit_event_t` and
  the user-supplied `userdata` pointer.

- **userdata** — Opaque pointer passed through to the callback.
  May be NULL.  Not freed by the library.

# RETURN VALUE

None.  This function always succeeds.  If `c` is NULL, the call
is silently ignored.

# ERRORS

This function does not report errors.  Invalid parameters (NULL
client) are silently ignored.

# EXAMPLES

# Basic debugger with break-hit handler

```c
#include <bashclient.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    bc_client_t *client;
    int           hit_count;
    int           max_hits;
} debugger_ctx_t;

static void on_break(const bc_break_hit_event_t *ev, void *ud)
{
    debugger_ctx_t *ctx = (debugger_ctx_t *)ud;
    ctx->hit_count++;

    printf("Break #%d at line %d (depth %d): %s\n",
           ctx->hit_count, ev->line, ev->depth, ev->command);

    /* Inspect the AST */
    char *ast = NULL;
    if (bc_debug_inspect_ast(ctx->client, &ast) == BC_OK) {
        printf("  AST: %.200s%s\n", ast,
               strlen(ast) > 200 ? "..." : "");
        free(ast);
    }

    if (ctx->hit_count >= ctx->max_hits) {
        printf("Max hits reached, continuing...\n");
        bc_debug_continue(ctx->client);
    } else {
        bc_debug_step(ctx->client);
    }
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    debugger_ctx_t ctx = {
        .client    = c,
        .hit_count = 0,
        .max_hits  = 50
    };

    /* Register callback before enabling debugger */
    bc_debug_on_break_hit(c, on_break, &ctx);

    bc_debug_enable(c);
    bc_debug_add_breakpoint(c, "line", NULL, 1, NULL);

    bc_eval(c, "source target.sh", NULL, NULL, NULL);

    while (bc_poll(c, 5000) >= 0)
        ;

    printf("Total break hits: %d\n", ctx.hit_count);

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

# Combining observe and debug callbacks

```c
#include <bashclient.h>
#include <stdio.h>

static void on_pre(const bc_pre_command_event_t *ev, void *ud)
{
    /* Observe sees all commands, even when debugger is active */
    printf("[observe] %s\n", ev->command);
}

static void on_break(const bc_break_hit_event_t *ev, void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;
    printf("[debug] BREAK line %d: %s\n", ev->line, ev->command);
    bc_debug_continue(c);
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    /* Register both callback types */
    bc_observe_on_pre_command(c, on_pre, NULL);
    bc_debug_on_break_hit(c, on_break, c);

    /* Enable both observe and debug */
    bc_observe_subscribe(c, BC_OBSERVE_LEVEL_COMMAND);
    bc_debug_enable(c);
    bc_debug_add_breakpoint(c, "command", "echo", -1, NULL);

    bc_eval(c, "echo test; ls; echo done", NULL, NULL, NULL);

    /* Single poll loop handles both event types */
    while (bc_poll(c, 5000) >= 0)
        ;

    bc_debug_disable(c);
    bc_observe_unsubscribe(c);
    bc_disconnect(c);
    return 0;
}
```

# SEE ALSO

`bc_debug_enable`(3), `bc_debug_add_breakpoint`(3),
`bc_debug_continue`(3), `bc_debug_step`(3), `bc_debug_next`(3),
`bc_debug_finish`(3), `bc_debug_skip`(3),
`bc_debug_inspect_ast`(3), `bc_poll`(3)

# NOTES

- The `event` struct and its `command` string are owned by the
  library and freed after the callback returns.  Copy with
  `strdup()` if needed beyond the callback scope.
- Break-hit events are generated for breakpoint matches **and**
  after step/next/finish operations.  The callback is the unified
  entry point for all debugger stops.
- It is safe to call debug control functions (`bc_debug_continue`,
  `bc_debug_step`, etc.) from within the callback.  The server
  queues the command and resumes after `bc_poll()` returns.
- If no resumption command is issued, the debugger remains paused.
  Subsequent `bc_poll()` calls will block (up to timeout) waiting
  for events that will not arrive until execution resumes.
- The `depth` field helps distinguish between top-level commands
  and commands inside functions, subshells, or loops.
- Registering a callback does not enable the debugger.  Call
  `bc_debug_enable()` and add breakpoints separately.
- The callback persists across `bc_debug_disable()` /
  `bc_debug_enable()` cycles.
- Thread safety: callbacks are always invoked from the thread
  that calls `bc_poll()`.  The library is not thread-safe.
