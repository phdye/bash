# bc\_debug\_finish(3) — run until current function or block returns

# SYNOPSIS

```c
#include <bashclient.h>

int bc_debug_finish(bc_client_t *c);
```

# DESCRIPTION

Continues execution until the current function, subshell, or
compound command block returns, then stops.  The function sends a
`finish` command on `CHAN_DEBUG` (channel 4).

The debugger records the current nesting depth and resumes
execution.  When the depth decreases below the recorded level
(i.e., the enclosing block returns), a break-hit event is
generated for the next command at the caller's level.

This is useful when you have stepped into a function and want to
run the rest of it without stopping, returning control to the
caller.

The debugger mode changes to `"finish"` after this call.

**Behavior at different depths:**

| Current context | Finish behavior |
|----------------|-----------------|
| Inside a function | Run until function returns, stop at caller |
| Inside a subshell `(...)` | Run until subshell exits |
| Inside a loop body | Run until loop iteration completes |
| Top-level (depth 0) | Equivalent to `bc_debug_continue()` |

This function should be called when the debugger is stopped at a
breakpoint.  Calling it when not stopped is a no-op.

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

# Finish a function after stepping in

```c
#include <bashclient.h>
#include <stdio.h>

static int inside_function = 0;

static void on_break(const bc_break_hit_event_t *ev, void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;

    printf("depth=%d: %s\n", ev->depth, ev->command);

    if (ev->depth > 0 && !inside_function) {
        /* We just stepped into a function */
        inside_function = 1;
        printf("Inside function, inspecting first command...\n");

        /* Run the rest of the function without stopping */
        bc_debug_finish(c);
    } else if (inside_function && ev->depth == 0) {
        /* Back at top level after finish */
        inside_function = 0;
        printf("Returned from function\n");
        bc_debug_next(c);
    } else {
        /* Top level: step into functions */
        bc_debug_step(c);
    }
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    bc_debug_enable(c);
    bc_debug_add_breakpoint(c, "line", NULL, 1, NULL);
    bc_debug_on_break_hit(c, on_break, c);

    bc_eval(c, "source script.sh", NULL, NULL, NULL);

    while (bc_poll(c, 5000) >= 0)
        ;

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

# Depth-aware stepping

```c
#include <bashclient.h>
#include <stdio.h>

static void depth_stepper(const bc_break_hit_event_t *ev, void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;

    if (ev->depth > 2) {
        /* Too deep — finish back to a manageable depth */
        printf("Too deep (depth=%d), finishing...\n", ev->depth);
        bc_debug_finish(c);
    } else {
        printf("[depth %d] %s\n", ev->depth, ev->command);
        bc_debug_step(c);
    }
}
```

# SEE ALSO

`bc_debug_step`(3), `bc_debug_next`(3), `bc_debug_continue`(3),
`bc_debug_skip`(3), `bc_debug_on_break_hit`(3), `bc_poll`(3)

# NOTES

- Breakpoints inside the finishing block still fire.  Finish is
  not a "run ignoring breakpoints" command; it only sets the
  depth threshold for the next automatic stop.
- At top level (depth 0), there is no enclosing block to return
  from, so `finish` behaves like `continue` and runs until the
  next breakpoint or command completion.
- The break-hit event generated after finish fires at the first
  command **after** the block returns, not at the return itself.
- If the block calls `exit`, no break-hit event is generated
  (the session ends).
- Thread safety: the client library is not thread-safe.
