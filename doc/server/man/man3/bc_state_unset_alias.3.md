# bc\_state\_unset\_alias(3) — unset a shell alias on the server

# SYNOPSIS

```c
#include "bashclient.h"

int bc_state_unset_alias(bc_client_t *c, const char *name);
```

# DESCRIPTION

Removes a shell alias from the bash-server session associated with
the client connection **c**.  After a successful call, the alias no
longer exists in the session's alias table.

The function sends an `unset` request on the STATE channel
(`CHAN_STATE`, channel 2) with namespace `"alias"` and the given
alias name.  The server performs the equivalent of `unalias name`
in the shell session.

If the alias does not exist, the server treats this as a successful
operation (no error is returned).  This provides idempotent
behavior, allowing callers to remove aliases without first checking
whether they are defined.

# PARAMETERS

| Parameter | Type | Description |
|-----------|------|-------------|
| `c` | `bc_client_t *` | Client connection handle. Must not be NULL. |
| `name` | `const char *` | Name of the alias to remove. Must not be NULL or empty. |

# RETURN VALUE

Returns `BC_OK` (0) on success (including when the alias did not
exist).

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
- If the connection is not authenticated, returns
  `BC_ERR_PROTOCOL`.

# EXAMPLES

# Remove an alias

```c
int rc = bc_state_unset_alias(client, "ll");
if (rc != BC_OK)
    fprintf(stderr, "unset_alias failed: %d\n", rc);
```

# Define, verify, then remove an alias

```c
/* Define an alias */
bc_state_set_alias(client, "la", "ls -A");

/* Verify it exists */
char *val = NULL;
int rc = bc_state_get_alias(client, "la", &val);
assert(rc == BC_OK);
printf("la -> %s\n", val);
bc_free(val);

/* Remove it */
rc = bc_state_unset_alias(client, "la");
assert(rc == BC_OK);

/* Verify it is gone */
rc = bc_state_get_alias(client, "la", &val);
assert(rc == BC_ERR_SERVER);
```

# Clear all aliases

```c
/* Get the list of all aliases */
char *json = NULL;
int rc = bc_state_inspect(client, "aliases", &json);
if (rc == BC_OK) {
    /* Parse JSON array, then unset each alias by name */
    /* ... JSON parsing omitted for brevity ... */
    bc_free(json);
}
```

# Idempotent removal

```c
/* Safe to call even if the alias does not exist */
int rc = bc_state_unset_alias(client, "nonexistent_alias");
assert(rc == BC_OK);  /* succeeds silently */
```

# SEE ALSO

`bc_state_get_alias`(3), `bc_state_set_alias`(3),
`bc_state_inspect`(3), `bc_state_unset_var`(3),
`bc_connect`(3), `bash-server-client-c`(7),
`bash-server-channels`(7)

# NOTES

- Unsetting a nonexistent alias is not an error.  This provides
  idempotent semantics for cleanup operations.
- Removing an alias does not affect variables, functions, or
  traps with the same name.  Each namespace is independent.
- Unlike variables, aliases cannot have a `readonly` attribute
  in standard bash, so unset operations on aliases should
  always succeed if the alias exists.
- The built-in aliases (if any were defined during session
  initialization via `--init` or profile scripts) can be
  removed just like user-defined aliases.
- The function uses the v2 binary protocol internally.  The STATE
  channel request includes `"op": "unset"`, `"ns": "alias"`, and
  `"name"` fields.
- A single `bc_client_t` must not be used concurrently from
  multiple threads.
