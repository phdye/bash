# bc\_debug\_remove\_breakpoint(3) — remove a breakpoint by ID

# SYNOPSIS

```c
#include <bashclient.h>

int bc_debug_remove_breakpoint(bc_client_t *c, int bp_id);
```

# DESCRIPTION

Removes a single breakpoint from the debugger by its ID.  The
function sends a `remove_breakpoint` request on `CHAN_DEBUG`
(channel 4) and waits for the server's acknowledgement.

The breakpoint ID is the value returned by a previous call to
`bc_debug_add_breakpoint()`.  Once removed, the breakpoint no
longer triggers and its ID is never reused.

If the removed breakpoint was the last one and the debugger is in
`"run"` mode, the debug pre-command hook remains registered (the
debugger is still enabled).  Use `bc_debug_disable()` to fully
deactivate the debugger.

# PARAMETERS

- **c** — Pointer to an authenticated `bc_client_t` connection.
  Must not be NULL.

- **bp_id** — The breakpoint ID to remove.  Must be a positive
  integer previously returned by `bc_debug_add_breakpoint()`.

# RETURN VALUE

Returns `BC_OK` (0) on success.

Returns a negative error code on failure:

| Code | Meaning |
|------|---------|
| `BC_ERR_PARAM` (-7) | NULL client or invalid bp_id (<= 0) |
| `BC_ERR_AUTH` (-1) | Client not authenticated |
| `BC_ERR_PROTOCOL` (-2) | Unexpected server response |
| `BC_ERR_TIMEOUT` (-3) | Server did not respond in time |
| `BC_ERR_TRANSPORT` (-4) | Connection error |
| `BC_ERR_SERVER` (-5) | Breakpoint ID not found |

# ERRORS

- Returns `BC_ERR_PARAM` if `bp_id` is not positive.
- Returns `BC_ERR_SERVER` if the server does not have a breakpoint
  with the specified ID (already removed or never existed).

# EXAMPLES

```c
#include <bashclient.h>
#include <stdio.h>

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");
    bc_debug_enable(c);

    /* Add two breakpoints */
    int bp1 = bc_debug_add_breakpoint(c, "command", "rm", -1, NULL);
    int bp2 = bc_debug_add_breakpoint(c, "line", NULL, 42, NULL);
    printf("Added breakpoints %d and %d\n", bp1, bp2);

    /* Remove the first one */
    int rc = bc_debug_remove_breakpoint(c, bp1);
    if (rc == BC_OK) {
        printf("Breakpoint %d removed\n", bp1);
    } else {
        fprintf(stderr, "Remove failed: %d\n", rc);
    }

    /* Attempting to remove again fails */
    rc = bc_debug_remove_breakpoint(c, bp1);
    if (rc == BC_ERR_SERVER)
        printf("Breakpoint %d already removed\n", bp1);

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

# SEE ALSO

`bc_debug_add_breakpoint`(3), `bc_debug_list_breakpoints`(3),
`bc_debug_disable`(3), `debug_remove_breakpoint`(3)

# NOTES

- Breakpoint IDs are never reused.  Once removed, the same ID
  cannot be added back.
- Removing a breakpoint while execution is stopped at that
  breakpoint is valid.  The command will proceed normally on
  the next `bc_debug_continue()` or `bc_debug_step()`.
- To remove all breakpoints at once, use `bc_debug_disable()`
  followed by `bc_debug_enable()`, or iterate over the list
  from `bc_debug_list_breakpoints()`.
- Thread safety: the client library is not thread-safe.
