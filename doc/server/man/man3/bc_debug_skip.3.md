# bc\_debug\_skip(3) — skip the current command without executing it

# SYNOPSIS

```c
#include <bashclient.h>

int bc_debug_skip(bc_client_t *c);
```

# DESCRIPTION

Skips the currently stopped command without executing it and
advances to the next command.  The function sends a `skip` command
on `CHAN_DEBUG` (channel 4).

The skipped command is treated as if it returned exit status 0
(success).  No side effects from the command occur: no files are
modified, no processes are spawned, and no variable assignments
take effect.

After skipping, the debugger stops at the next command (as if
`bc_debug_step()` were called, but without executing the current
command).  A new break-hit event is generated.

This is useful for:

- Preventing destructive commands (e.g., `rm -rf`) from executing
  during a debugging session.
- Testing control flow by skipping over specific commands.
- Bypassing commands that would hang or produce unwanted side
  effects.

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

# Skip destructive commands

```c
#include <bashclient.h>
#include <stdio.h>
#include <string.h>

static void safe_handler(const bc_break_hit_event_t *ev, void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;

    /* Skip any rm commands */
    if (strstr(ev->command, "rm ")) {
        printf("SKIPPING destructive command: %s\n", ev->command);
        bc_debug_skip(c);
        return;
    }

    printf("Executing: %s\n", ev->command);
    bc_debug_step(c);
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    bc_debug_enable(c);

    /* Break on every command to inspect it */
    bc_debug_add_breakpoint(c, "line", NULL, 1, NULL);
    bc_debug_on_break_hit(c, safe_handler, c);

    /* Run a script that might delete files */
    bc_eval(c, "source cleanup.sh", NULL, NULL, NULL);

    while (bc_poll(c, 5000) >= 0)
        ;

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

# Interactive skip decision

```c
#include <bashclient.h>
#include <stdio.h>

static void interactive_handler(const bc_break_hit_event_t *ev,
                                void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;

    printf("line %d: %s\n", ev->line, ev->command);
    printf("  [s]tep  [n]ext  [c]ontinue  [k]ip  [f]inish ? ");

    int ch = getchar();
    while (getchar() != '\n')  /* drain input */
        ;

    switch (ch) {
    case 's': bc_debug_step(c);     break;
    case 'n': bc_debug_next(c);     break;
    case 'c': bc_debug_continue(c); break;
    case 'k': bc_debug_skip(c);     break;
    case 'f': bc_debug_finish(c);   break;
    default:  bc_debug_step(c);     break;
    }
}
```

# SEE ALSO

`bc_debug_step`(3), `bc_debug_next`(3), `bc_debug_continue`(3),
`bc_debug_finish`(3), `bc_debug_on_break_hit`(3), `bc_poll`(3)

# NOTES

- The skipped command's exit status is implicitly 0.  This can
  affect control flow in conditionals.  For example, skipping
  the test in `if test_cmd; then ...` causes the `then` branch
  to execute.
- Skipping a variable assignment means the variable retains its
  previous value (or remains unset if it was not previously set).
- Skipping a `cd` command means the working directory does not
  change.
- Skip does not work on compound command wrappers (`if`, `while`,
  `for` headers).  It applies to the simple commands within them.
- After skipping, the debugger generates a break-hit event for the
  next command, similar to `bc_debug_step()`.
- Thread safety: the client library is not thread-safe.
