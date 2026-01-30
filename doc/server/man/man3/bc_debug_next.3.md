# bc\_debug\_next(3) — step to the next command at same depth (step over)

# SYNOPSIS

```c
#include <bashclient.h>

int bc_debug_next(bc_client_t *c);
```

# DESCRIPTION

Steps execution to the next command at the same nesting depth,
skipping over function calls, subshells, and compound commands.
This is the "step over" operation.  The function sends a `next`
command on `CHAN_DEBUG` (channel 4).

After stepping over, the debugger stops at the next command whose
nesting depth is less than or equal to the depth where `next` was
issued.  Commands inside called functions or subshells execute
without stopping.

**Comparison of stepping commands:**

| Command | Stops at | Mode |
|---------|----------|------|
| `bc_debug_step()` | Very next command (any depth) | `"step"` |
| `bc_debug_next()` | Next command at same or lower depth | `"next"` |
| `bc_debug_finish()` | After current function/block returns | `"finish"` |
| `bc_debug_continue()` | Next breakpoint | `"run"` |

The debugger mode changes to `"next"` after this call.

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

# Step over function calls

```c
#include <bashclient.h>
#include <stdio.h>

/*
 * Script:
 *   line 1: setup() { echo "setting up"; configure; }
 *   line 2: setup
 *   line 3: echo "done"
 *
 * With bc_debug_next() at line 2:
 *   - setup() executes fully without stopping
 *   - Debugger stops at line 3 ("echo done")
 *
 * With bc_debug_step() at line 2:
 *   - Debugger stops at "echo setting up" inside setup()
 */

static void on_break(const bc_break_hit_event_t *ev, void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;

    printf("line=%d depth=%d: %s\n",
           ev->line, ev->depth, ev->command);

    /* Step over — skip into function internals */
    bc_debug_next(c);
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    bc_debug_enable(c);
    bc_debug_add_breakpoint(c, "line", NULL, 1, NULL);
    bc_debug_on_break_hit(c, on_break, c);

    bc_eval(c, "source myscript.sh", NULL, NULL, NULL);

    while (bc_poll(c, 5000) >= 0)
        ;

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

# Mixed stepping strategy

```c
#include <bashclient.h>
#include <stdio.h>
#include <string.h>

static void smart_step(const bc_break_hit_event_t *ev, void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;

    /* Step into functions of interest, step over others */
    if (strstr(ev->command, "my_function")) {
        printf("Stepping INTO: %s\n", ev->command);
        bc_debug_step(c);
    } else {
        printf("Stepping OVER: %s\n", ev->command);
        bc_debug_next(c);
    }
}
```

# SEE ALSO

`bc_debug_step`(3), `bc_debug_continue`(3), `bc_debug_finish`(3),
`bc_debug_skip`(3), `bc_debug_on_break_hit`(3), `bc_poll`(3)

# NOTES

- "Same depth" means the execution nesting depth recorded when
  `next` was issued.  If the current command returns from a
  function (decreasing depth), the debugger stops at the caller.
- Breakpoints inside stepped-over functions still fire.  Use
  `bc_debug_continue()` if you want to skip breakpoints too
  (note: continue does honor breakpoints; there is no "run
  ignoring breakpoints" command).
- If the command at the current depth is the last in a block,
  `next` behaves like `finish` for that block.
- After `bc_debug_next()`, a new break-hit event is generated
  when the debugger stops.  Call `bc_poll()` to receive it.
- Thread safety: the client library is not thread-safe.
