# bc\_debug\_continue(3) — continue execution after a breakpoint hit

# SYNOPSIS

```c
#include <bashclient.h>

int bc_debug_continue(bc_client_t *c);
```

# DESCRIPTION

Resumes command execution after the debugger has stopped at a
breakpoint.  The function sends a `continue` command on
`CHAN_DEBUG` (channel 4), which tells the server to exit the
break state and resume normal execution.

After continuing, execution proceeds until the next breakpoint
is hit, or until the command completes.  The debugger enters
`"run"` mode.

This function is typically called from within a `bc_break_hit_cb`
callback registered via `bc_debug_on_break_hit()`, or after
returning from the callback while still in a `bc_poll()` loop.

Calling `bc_debug_continue()` when the debugger is not stopped
at a breakpoint is a no-op and returns `BC_OK`.

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
- Returns `BC_ERR_TRANSPORT` if the connection is broken.

# EXAMPLES

# Continue from callback

```c
#include <bashclient.h>
#include <stdio.h>

static void on_break(const bc_break_hit_event_t *ev, void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;

    printf("Hit breakpoint at line %d: %s\n", ev->line, ev->command);

    /* Inspect state if needed */
    char *ast = NULL;
    if (bc_debug_inspect_ast(c, &ast) == BC_OK) {
        printf("AST: %s\n", ast);
        free(ast);
    }

    /* Resume execution */
    bc_debug_continue(c);
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    bc_debug_enable(c);
    bc_debug_add_breakpoint(c, "line", NULL, 5, NULL);
    bc_debug_on_break_hit(c, on_break, c);

    bc_eval(c, "source test.sh", NULL, NULL, NULL);

    while (bc_poll(c, 5000) >= 0)
        ;

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

# Continue vs step comparison

```c
#include <bashclient.h>
#include <stdio.h>

static int break_count = 0;

static void on_break(const bc_break_hit_event_t *ev, void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;
    break_count++;

    printf("Break #%d at line %d (depth %d): %s\n",
           break_count, ev->line, ev->depth, ev->command);

    if (break_count < 3) {
        /* Step through first few commands */
        bc_debug_step(c);
    } else {
        /* Then continue to next breakpoint */
        bc_debug_continue(c);
    }
}
```

# SEE ALSO

`bc_debug_step`(3), `bc_debug_next`(3), `bc_debug_finish`(3),
`bc_debug_skip`(3), `bc_debug_on_break_hit`(3), `bc_poll`(3)

# NOTES

- After `bc_debug_continue()`, the debugger mode changes to
  `"run"`.  Execution will only stop again at the next matching
  breakpoint.
- It is safe to call `bc_debug_continue()` from within a
  `bc_break_hit_cb` callback.  The server queues the continue
  and resumes after the callback returns and `bc_poll()` yields
  control.
- If no breakpoints remain or none match subsequent commands,
  execution runs to completion after continue.
- Thread safety: the client library is not thread-safe.
