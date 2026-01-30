# bc\_debug\_disable(3) — disable the debugger and remove all breakpoints

# SYNOPSIS

```c
#include <bashclient.h>

int bc_debug_disable(bc_client_t *c);
```

# DESCRIPTION

Disables the debugger for the current session.  This removes all
breakpoints, unregisters the debug pre-command hook, and resets
the debug state to inactive.  The server sends a confirmation
response on `CHAN_DEBUG` (channel 4).

After disabling, commands execute at full speed without breakpoint
checking overhead.  The break-hit callback registered via
`bc_debug_on_break_hit()` is **not** removed; it persists so that
re-enabling the debugger does not require re-registration.

Calling `bc_debug_disable()` when the debugger is not enabled is
a no-op and returns `BC_OK`.

If execution is currently stopped at a breakpoint when
`bc_debug_disable()` is called, execution is resumed (equivalent
to an implicit `bc_debug_continue()`) before the debugger is
disabled.

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
- Returns `BC_ERR_AUTH` if the session is not authenticated.
- Returns `BC_ERR_TRANSPORT` if the connection is broken.

# EXAMPLES

```c
#include <bashclient.h>
#include <stdio.h>

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    /* Enable, debug a script, then disable */
    bc_debug_enable(c);
    bc_debug_add_breakpoint(c, "command", "rm", -1, NULL);

    bc_eval(c, "source cleanup.sh", NULL, NULL, NULL);
    bc_poll(c, 5000);

    /* Done debugging — disable for full-speed execution */
    int rc = bc_debug_disable(c);
    if (rc != BC_OK) {
        fprintf(stderr, "Failed to disable debugger: %d\n", rc);
    }

    /* Subsequent commands run without debug overhead */
    bc_eval(c, "echo running at full speed", NULL, NULL, NULL);

    bc_disconnect(c);
    return 0;
}
```

# SEE ALSO

`bc_debug_enable`(3), `bc_debug_status`(3),
`bc_debug_remove_breakpoint`(3), `bc_debug_continue`(3),
`debug_cleanup`(3)

# NOTES

- All breakpoints are removed when the debugger is disabled.
  Breakpoint IDs are not preserved across enable/disable cycles.
- The break-hit callback is not cleared.  To remove it, call
  `bc_debug_on_break_hit(c, NULL, NULL)` explicitly.
- If a command is stopped at a breakpoint, disabling the debugger
  implicitly continues execution before cleanup.
- Thread safety: the client library is not thread-safe.
