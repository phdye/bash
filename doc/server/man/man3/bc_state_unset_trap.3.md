# bc\_state\_unset\_trap(3) — unset a signal trap on the server

# SYNOPSIS

```c
#include "bashclient.h"

int bc_state_unset_trap(bc_client_t *c, const char *signal);
```

# DESCRIPTION

Removes a signal trap from the bash-server session associated with
the client connection **c**.  After a successful call, the signal
reverts to its default disposition (the behavior it had before any
trap was set for it).

The function sends an `unset` request on the STATE channel
(`CHAN_STATE`, channel 2) with namespace `"trap"` and the given
signal name.  The server performs the equivalent of `trap - signal`
in the shell session, which resets the signal handler to its
default action.

If no trap is set for the specified signal, the server treats this
as a successful operation (no error is returned).

Signal names follow the same conventions as `bc_state_set_trap()`:
they may include or omit the `SIG` prefix and are
case-insensitive.

# PARAMETERS

| Parameter | Type | Description |
|-----------|------|-------------|
| `c` | `bc_client_t *` | Client connection handle. Must not be NULL. |
| `signal` | `const char *` | Signal name (e.g., `"SIGINT"`, `"EXIT"`, `"ERR"`). Must not be NULL or empty. |

# RETURN VALUE

Returns `BC_OK` (0) on success (including when no trap was set for
the signal).

On failure, returns one of the following error codes:

| Code | Value | Meaning |
|------|-------|---------|
| `BC_ERR_PARAM` | -7 | A required parameter is NULL or invalid. |
| `BC_ERR_PROTOCOL` | -2 | The server response could not be parsed. |
| `BC_ERR_TIMEOUT` | -3 | The server did not respond within the configured timeout. |
| `BC_ERR_TRANSPORT` | -4 | The underlying connection failed. |
| `BC_ERR_SERVER` | -5 | The server rejected the operation (e.g., invalid signal name). |
| `BC_ERR_NOMEM` | -6 | Memory allocation failed. |

# ERRORS

- If **c** is NULL, returns `BC_ERR_PARAM`.
- If **signal** is NULL or empty, returns `BC_ERR_PARAM`.
- If the signal name is not recognized by the server, returns
  `BC_ERR_SERVER`.
- If the connection is not authenticated, returns
  `BC_ERR_PROTOCOL`.

# EXAMPLES

# Remove an EXIT trap

```c
int rc = bc_state_unset_trap(client, "EXIT");
if (rc != BC_OK)
    fprintf(stderr, "unset_trap failed: %d\n", rc);
```

# Set and then remove a SIGINT trap

```c
/* Set a trap */
bc_state_set_trap(client, "SIGINT", "echo caught");

/* Later, remove it */
int rc = bc_state_unset_trap(client, "SIGINT");
assert(rc == BC_OK);
/* SIGINT now reverts to default behavior (terminate) */
```

# Idempotent removal

```c
/* Safe to call even if no trap is set */
int rc = bc_state_unset_trap(client, "SIGUSR1");
assert(rc == BC_OK);  /* succeeds silently */
```

# Remove all traps

```c
/* Retrieve trap listing, then unset each one */
char *json = NULL;
int rc = bc_state_inspect(client, "traps", &json);
if (rc == BC_OK) {
    /* Parse JSON array of signal names with active traps,
       then call bc_state_unset_trap for each */
    /* ... JSON parsing omitted for brevity ... */
    bc_free(json);
}
```

# Difference between unset and empty command

```c
/* Setting an empty command IGNORES the signal */
bc_state_set_trap(client, "SIGINT", "");
/* SIGINT is now ignored (not default) */

/* Unsetting RESETS to default behavior */
bc_state_unset_trap(client, "SIGINT");
/* SIGINT now terminates the process (default) */
```

# SEE ALSO

`bc_state_set_trap`(3), `bc_state_inspect`(3),
`bc_state_unset_var`(3), `bc_state_unset_alias`(3),
`bc_connect`(3), `bash-server-client-c`(7),
`bash-server-channels`(7)

# NOTES

- Unsetting a trap is different from setting an empty trap
  command.  `bc_state_set_trap(c, "SIGINT", "")` causes the
  signal to be ignored, while `bc_state_unset_trap(c, "SIGINT")`
  restores the default signal handler.  This matches the
  distinction between `trap '' SIGINT` and `trap - SIGINT` in
  bash.
- Unsetting a trap for a signal that was never trapped is not
  an error.  The signal remains at its default disposition.
- The EXIT pseudo-signal trap is typically set for cleanup
  operations.  Unsetting it before session termination prevents
  the cleanup code from running.
- Unsetting the DEBUG trap restores normal command execution
  without per-command hook invocations.
- The function uses the v2 binary protocol internally.  The STATE
  channel request includes `"op": "unset"`, `"ns": "trap"`, and
  `"name"` (signal name) fields.
- A single `bc_client_t` must not be used concurrently from
  multiple threads.
