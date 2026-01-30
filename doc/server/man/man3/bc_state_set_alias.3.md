# bc\_state\_set\_alias(3) — set a shell alias on the server

# SYNOPSIS

```c
#include "bashclient.h"

int bc_state_set_alias(bc_client_t *c, const char *name,
                       const char *value);
```

# DESCRIPTION

Defines or replaces a shell alias on the bash-server session
associated with the client connection **c**.  If an alias with the
given name already exists, its expansion value is replaced.  If it
does not exist, a new alias is created.

The function sends a `set` request on the STATE channel
(`CHAN_STATE`, channel 2) with namespace `"alias"`, the alias name,
and the expansion value.  The server performs the equivalent of
`alias name='value'` in the shell session.

The alias takes effect immediately for subsequent command
evaluations in the session, provided that alias expansion is
enabled (`shopt -s expand_aliases`, which is the default for
interactive sessions).

# PARAMETERS

| Parameter | Type | Description |
|-----------|------|-------------|
| `c` | `bc_client_t *` | Client connection handle. Must not be NULL. |
| `name` | `const char *` | Alias name. Must not be NULL or empty. May contain most printable characters except `=` and `/`. |
| `value` | `const char *` | Expansion value for the alias. Must not be NULL. May be an empty string. Special characters are stored literally (not shell-escaped). |

# RETURN VALUE

Returns `BC_OK` (0) on success.

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
- If **value** is NULL, returns `BC_ERR_PARAM`.
- If the connection is not authenticated, returns
  `BC_ERR_PROTOCOL`.

# EXAMPLES

# Define a simple alias

```c
int rc = bc_state_set_alias(client, "ll", "ls -la");
if (rc != BC_OK)
    fprintf(stderr, "set_alias failed: %d\n", rc);
```

# Define an alias with special characters

```c
/* Alias expansion values can contain pipes, quotes, etc. */
int rc = bc_state_set_alias(client, "grep",
                            "grep --color=auto");
if (rc != BC_OK)
    fprintf(stderr, "set_alias failed: %d\n", rc);
```

# Replace an existing alias

```c
/* First definition */
bc_state_set_alias(client, "ll", "ls -l");

/* Replace with a different value */
int rc = bc_state_set_alias(client, "ll", "ls -la --color=auto");
assert(rc == BC_OK);

/* Verify the new value */
char *val = NULL;
rc = bc_state_get_alias(client, "ll", &val);
if (rc == BC_OK) {
    printf("ll -> %s\n", val);  /* "ls -la --color=auto" */
    bc_free(val);
}
```

# Define a trailing-space alias for chaining

```c
/* A trailing space causes the next word to also be checked
   for alias expansion */
int rc = bc_state_set_alias(client, "sudo", "sudo ");
assert(rc == BC_OK);
```

# SEE ALSO

`bc_state_get_alias`(3), `bc_state_unset_alias`(3),
`bc_state_inspect`(3), `bc_state_set_var`(3),
`bc_connect`(3), `bash-server-client-c`(7),
`bash-server-channels`(7)

# NOTES

- Alias expansion in bash is a text substitution performed at
  parse time, before any other expansions.  Aliases defined via
  this function affect subsequent `bc_eval()` calls in the same
  session.
- Aliases are distinct from functions.  An alias and a function
  may share the same name; the alias takes precedence during
  interactive command parsing.
- The **value** parameter is stored literally.  The caller does
  not need to shell-quote the value; quoting is only needed in
  shell syntax (`alias name='value'`), not in the C API.
- There is no built-in mechanism to make an alias readonly.
  Unlike variables, aliases cannot have attributes.
- The function uses the v2 binary protocol internally.  The STATE
  channel request includes `"op": "set"`, `"ns": "alias"`,
  `"name"`, and `"value"` fields.
- A single `bc_client_t` must not be used concurrently from
  multiple threads.
