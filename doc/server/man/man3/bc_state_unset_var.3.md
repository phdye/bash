# bc\_state\_unset\_var(3) — unset a shell variable on the server

# SYNOPSIS

```c
#include "bashclient.h"

int bc_state_unset_var(bc_client_t *c, const char *name);
```

# DESCRIPTION

Removes a shell variable from the bash-server session associated
with the client connection **c**.  After a successful call, the
variable no longer exists in the session's variable namespace.

The function sends an `unset` request on the STATE channel
(`CHAN_STATE`, channel 2) with namespace `"var"` and the given
variable name.  The server performs the equivalent of `unset name`
in the shell session.

If the variable does not exist, the server treats this as a
successful operation (no error is returned).  This matches the
behavior of the `unset` builtin in bash, which succeeds silently
when the target variable is not set.

If the variable has the `readonly` attribute, the server refuses
the operation and returns an error.

# PARAMETERS

| Parameter | Type | Description |
|-----------|------|-------------|
| `c` | `bc_client_t *` | Client connection handle. Must not be NULL. |
| `name` | `const char *` | Name of the variable to unset. Must not be NULL or empty. |

# RETURN VALUE

Returns `BC_OK` (0) on success (including when the variable did
not exist).

On failure, returns one of the following error codes:

| Code | Value | Meaning |
|------|-------|---------|
| `BC_ERR_PARAM` | -7 | A required parameter is NULL or invalid. |
| `BC_ERR_PROTOCOL` | -2 | The server response could not be parsed. |
| `BC_ERR_TIMEOUT` | -3 | The server did not respond within the configured timeout. |
| `BC_ERR_TRANSPORT` | -4 | The underlying connection failed. |
| `BC_ERR_SERVER` | -5 | The server rejected the operation (e.g., readonly variable). |
| `BC_ERR_NOMEM` | -6 | Memory allocation failed. |

# ERRORS

- If **c** is NULL, returns `BC_ERR_PARAM`.
- If **name** is NULL or empty, returns `BC_ERR_PARAM`.
- If the variable is marked `readonly`, the server refuses the
  unset and the function returns `BC_ERR_SERVER`.
- If the connection has been closed or is not authenticated,
  returns `BC_ERR_TRANSPORT` or `BC_ERR_PROTOCOL` respectively.

# EXAMPLES

# Unset a variable

```c
int rc = bc_state_unset_var(client, "TEMP_DIR");
if (rc != BC_OK)
    fprintf(stderr, "unset_var failed: %d\n", rc);
```

# Unset followed by verification

```c
bc_state_unset_var(client, "MY_VAR");

bc_var_info_t info;
int rc = bc_state_get_var(client, "MY_VAR", &info);
if (rc == BC_ERR_SERVER) {
    printf("MY_VAR successfully unset\n");
} else if (rc == BC_OK) {
    printf("MY_VAR still exists: %s\n", info.value);
    bc_var_info_free(&info);
}
```

# Idempotent unset

```c
/* Safe to call even if the variable does not exist */
int rc = bc_state_unset_var(client, "NONEXISTENT_VAR");
assert(rc == BC_OK);  /* succeeds silently */
```

# Error handling for readonly variables

```c
/* Attempting to unset a readonly variable */
const char *attrs[] = { "readonly" };
bc_state_set_var(client, "LOCKED", "secret", attrs, 1);

int rc = bc_state_unset_var(client, "LOCKED");
if (rc == BC_ERR_SERVER)
    fprintf(stderr, "cannot unset readonly variable\n");
```

# SEE ALSO

`bc_state_get_var`(3), `bc_state_set_var`(3),
`bc_var_info_free`(3), `bc_state_inspect`(3),
`bc_connect`(3), `bash-server-client-c`(7),
`bash-server-channels`(7)

# NOTES

- Unsetting a nonexistent variable is not an error.  This matches
  POSIX `unset` semantics.
- Unsetting a variable that has been exported also removes it from
  the environment of subsequently executed child processes.
- Special shell variables (e.g., `BASH_VERSION`, `RANDOM`, `LINENO`)
  may not be unsettable or may be automatically re-created by the
  shell.  The server's behavior in these cases matches the behavior
  of the `unset` builtin.
- The function uses the v2 binary protocol.  The STATE channel
  request includes `"op": "unset"`, `"ns": "var"`, and `"name"`
  fields.
- A single `bc_client_t` must not be used concurrently from
  multiple threads.
