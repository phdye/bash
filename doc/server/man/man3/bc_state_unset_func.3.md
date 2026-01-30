# bc\_state\_unset\_func(3) — unset a shell function on the server

# SYNOPSIS

```c
#include "bashclient.h"

int bc_state_unset_func(bc_client_t *c, const char *name);
```

# DESCRIPTION

Removes a shell function from the bash-server session associated
with the client connection **c**.  After a successful call, the
function no longer exists in the session's function namespace.

The function sends an `unset` request on the STATE channel
(`CHAN_STATE`, channel 2) with namespace `"func"` and the given
function name.  The server performs the equivalent of `unset -f name`
in the shell session.

If the function does not exist, the server treats this as a
successful operation (no error is returned).  This matches the
behavior of `unset -f` in bash, which succeeds silently when the
target function is not defined.

# PARAMETERS

| Parameter | Type | Description |
|-----------|------|-------------|
| `c` | `bc_client_t *` | Client connection handle. Must not be NULL. |
| `name` | `const char *` | Name of the function to unset. Must not be NULL or empty. |

# RETURN VALUE

Returns `BC_OK` (0) on success (including when the function did
not exist).

On failure, returns one of the following error codes:

| Code | Value | Meaning |
|------|-------|---------|
| `BC_ERR_PARAM` | -7 | A required parameter is NULL or invalid. |
| `BC_ERR_PROTOCOL` | -2 | The server response could not be parsed. |
| `BC_ERR_TIMEOUT` | -3 | The server did not respond within the configured timeout. |
| `BC_ERR_TRANSPORT` | -4 | The underlying connection failed. |
| `BC_ERR_SERVER` | -5 | The server rejected the operation. |
| `BC_ERR_NOMEM` | -6 | Memory allocation failed. |

# ERRORS

- If **c** is NULL, returns `BC_ERR_PARAM`.
- If **name** is NULL or empty, returns `BC_ERR_PARAM`.
- If the function has the `readonly` attribute (set via
  `readonly -f name`), the server refuses the unset and the
  function returns `BC_ERR_SERVER`.
- If the connection is not authenticated, returns
  `BC_ERR_PROTOCOL`.

# EXAMPLES

# Remove a function

```c
int rc = bc_state_unset_func(client, "greet");
if (rc != BC_OK)
    fprintf(stderr, "unset_func failed: %d\n", rc);
```

# Define, verify, then remove a function

```c
/* Define a function */
bc_eval(client, "cleanup() { rm -f /tmp/work_*; }", NULL, NULL, NULL);

/* Verify it exists */
char *def = NULL;
int rc = bc_state_get_func(client, "cleanup", &def);
assert(rc == BC_OK);
bc_free(def);

/* Remove it */
rc = bc_state_unset_func(client, "cleanup");
assert(rc == BC_OK);

/* Verify it is gone */
rc = bc_state_get_func(client, "cleanup", &def);
assert(rc == BC_ERR_SERVER);
```

# Idempotent removal

```c
/* Safe to call even if the function does not exist */
int rc = bc_state_unset_func(client, "nonexistent_func");
assert(rc == BC_OK);  /* succeeds silently */
```

# SEE ALSO

`bc_state_get_func`(3), `bc_state_inspect`(3),
`bc_state_get_var`(3), `bc_free`(3),
`bc_connect`(3), `bash-server-client-c`(7),
`bash-server-channels`(7)

# NOTES

- Unsetting a nonexistent function is not an error.  This matches
  POSIX `unset -f` semantics.
- There is no `bc_state_set_func()` function.  To define shell
  functions, use `bc_eval()` with the function definition syntax.
- Functions marked readonly with `readonly -f` cannot be unset.
  This restriction is enforced by the server, which returns an
  error message.
- After unsetting a function, any aliases, variables, or traps
  with the same name are unaffected.  Each namespace is
  independent.
- The function uses the v2 binary protocol internally.  The STATE
  channel request includes `"op": "unset"`, `"ns": "func"`, and
  `"name"` fields.
- A single `bc_client_t` must not be used concurrently from
  multiple threads.
