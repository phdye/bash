# bc\_debug\_enable(3) — enable the debugger on a bash-server session

# SYNOPSIS

```c
#include <bashclient.h>

int bc_debug_enable(bc_client_t *c);
```

# DESCRIPTION

Enables the debugger for the current session on `CHAN_DEBUG`
(channel 4).  Once enabled, the server enters debug mode and will
honor breakpoints, stepping commands, and AST inspection requests.

The function sends an `enable` message on the debug channel and
waits for the server's acknowledgement.  If the debugger is already
enabled, the server acknowledges without error and the function
returns `BC_OK`.

After enabling the debugger, the typical workflow is:

1. Add breakpoints with `bc_debug_add_breakpoint()`.
2. Register a break-hit callback with `bc_debug_on_break_hit()`.
3. Execute commands with `bc_eval()`.
4. Call `bc_poll()` to receive break-hit events.
5. Inspect state, then continue/step/skip.

Enabling the debugger has a performance impact on command
execution, as the server must check breakpoint conditions before
each command.  Disable the debugger with `bc_debug_disable()` when
debugging is no longer needed.

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
- Returns `BC_ERR_AUTH` if the session has not been authenticated.
- Returns `BC_ERR_TRANSPORT` if the connection is lost during
  the request.

# EXAMPLES

```c
#include <bashclient.h>
#include <stdio.h>

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;

    if (bc_auth(c, "mytoken") != BC_OK) {
        bc_disconnect(c);
        return 1;
    }

    /* Enable the debugger */
    int rc = bc_debug_enable(c);
    if (rc != BC_OK) {
        fprintf(stderr, "Failed to enable debugger: %d\n", rc);
        bc_disconnect(c);
        return 1;
    }

    /* Add a breakpoint on line 10 */
    int bp_id = bc_debug_add_breakpoint(c, "line", NULL, 10, NULL);
    if (bp_id < 0) {
        fprintf(stderr, "Failed to add breakpoint: %d\n", bp_id);
        bc_debug_disable(c);
        bc_disconnect(c);
        return 1;
    }
    printf("Breakpoint %d set at line 10\n", bp_id);

    /* Register break-hit callback */
    bc_debug_on_break_hit(c, my_break_handler, c);

    /* Execute a script — will stop at line 10 */
    bc_eval(c, "source myscript.sh", NULL, NULL, NULL);

    /* Poll for debug events */
    while (bc_poll(c, 5000) >= 0)
        ;

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

# SEE ALSO

`bc_debug_disable`(3), `bc_debug_status`(3),
`bc_debug_add_breakpoint`(3), `bc_debug_on_break_hit`(3),
`bc_poll`(3), `debug_init`(3)

# NOTES

- Enabling the debugger activates the server's `pre_command_hook`
  for breakpoint checking, which is separate from the observe
  hook.  Both can be active simultaneously.
- The debugger state is per-session.  Enabling the debugger in
  one session does not affect other sessions.
- If breakpoints were added in a previous enable/disable cycle,
  they are cleared by `bc_debug_disable()`.  Re-enabling starts
  with an empty breakpoint list.
- The function blocks until the server acknowledges.  Use
  `bc_poll()` for event processing, not for the enable handshake.
- Thread safety: the client library is not thread-safe.  Do not
  call `bc_debug_enable()` concurrently with other operations on
  the same `bc_client_t`.
