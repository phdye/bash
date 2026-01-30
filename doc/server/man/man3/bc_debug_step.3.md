# bc\_debug\_step(3) — step to the next command (step into)

# SYNOPSIS

```c
#include <bashclient.h>

int bc_debug_step(bc_client_t *c);
```

# DESCRIPTION

Steps execution to the next command, descending into functions,
subshells, and compound commands.  This is the "step into"
operation.  The function sends a `step` command on `CHAN_DEBUG`
(channel 4).

After stepping, the debugger stops at the very next command
that would be executed, regardless of nesting depth.  A new
break-hit event is delivered via `bc_poll()` when execution
reaches that command.

**Step vs Next:**

| Command | Behavior at function call |
|---------|-------------------------|
| `bc_debug_step()` | Stops at the first command inside the function |
| `bc_debug_next()` | Stops at the next command after the function returns |

The debugger mode changes to `"step"` after this call.

This function should be called when the debugger is stopped at a
breakpoint (i.e., from within or after a `bc_break_hit_cb`
callback).  Calling it when not stopped is a no-op.

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

# Single-step through a script

```c
#include <bashclient.h>
#include <stdio.h>

static void step_handler(const bc_break_hit_event_t *ev, void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;
    int *steps = (int *)((char *)ud + sizeof(bc_client_t *));

    printf("[step %d] line=%d depth=%d: %s\n",
           *steps, ev->line, ev->depth, ev->command);
    (*steps)++;

    if (*steps >= 20) {
        /* Stop stepping after 20 commands */
        bc_debug_continue(c);
    } else {
        bc_debug_step(c);
    }
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    bc_debug_enable(c);

    /* Break at the start of the script */
    bc_debug_add_breakpoint(c, "line", NULL, 1, NULL);

    int steps = 0;
    bc_debug_on_break_hit(c, step_handler, c);

    bc_eval(c, "source myscript.sh", NULL, NULL, NULL);

    while (bc_poll(c, 5000) >= 0)
        ;

    printf("Stepped through %d commands\n", steps);

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

# Step into a function

```c
#include <bashclient.h>
#include <stdio.h>

/*
 * Given this script:
 *   line 1: greet() { echo "hello $1"; }
 *   line 2: greet world
 *
 * bc_debug_step() at line 2 stops at "echo hello world"
 * bc_debug_next() at line 2 would skip over the function
 */
static void on_break(const bc_break_hit_event_t *ev, void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;
    printf("depth=%d: %s\n", ev->depth, ev->command);

    /* Step into the function call */
    bc_debug_step(c);
}
```

# SEE ALSO

`bc_debug_next`(3), `bc_debug_continue`(3), `bc_debug_finish`(3),
`bc_debug_skip`(3), `bc_debug_on_break_hit`(3), `bc_poll`(3)

# NOTES

- After stepping, a new break-hit event is generated for the
  next command.  The client must call `bc_poll()` to receive it.
- Stepping into a simple command (no subcommands) is equivalent
  to `bc_debug_next()` for that command.
- If the current command is the last in a function or block,
  stepping continues to the next command after the block returns
  (similar to `bc_debug_finish()` in that case).
- The `depth` field in the break-hit event increases when stepping
  into functions and subshells.
- Thread safety: the client library is not thread-safe.
