# bc\_state\_get\_alias(3) — get an alias value from the server

# SYNOPSIS

```c
#include "bashclient.h"

int bc_state_get_alias(bc_client_t *c, const char *name,
                       char **value);
```

# DESCRIPTION

Retrieves the expansion value of a shell alias from the bash-server
session associated with the client connection **c**.  On success,
**\*value** is set to a newly allocated string containing the alias
expansion text.

The function sends a `get` request on the STATE channel
(`CHAN_STATE`, channel 2) with namespace `"alias"` and the given
alias name.  The server looks up the alias in its internal alias
table and returns the expansion string.

For example, if the alias was defined as `alias ll='ls -la'`, the
returned value would be `ls -la` (without the surrounding quotes).

The caller is responsible for freeing the returned string by
calling `bc_free()` when it is no longer needed.

If the alias does not exist on the server, the function returns
`BC_ERR_SERVER` and **\*value** is not modified.

# PARAMETERS

| Parameter | Type | Description |
|-----------|------|-------------|
| `c` | `bc_client_t *` | Client connection handle, previously established via `bc_connect()`. Must not be NULL. |
| `name` | `const char *` | Name of the alias to retrieve. Must not be NULL or empty. Alias names may contain most printable characters except `=` and `/`. |
| `value` | `char **` | Pointer to a `char *` that receives the malloc'd alias expansion string on success. Must not be NULL. Caller frees via `bc_free()`. |

# RETURN VALUE

Returns `BC_OK` (0) on success.  **\*value** points to a newly
allocated string containing the alias expansion.

On failure, returns one of the following error codes:

| Code | Value | Meaning |
|------|-------|---------|
| `BC_ERR_PARAM` | -7 | A required parameter is NULL or invalid. |
| `BC_ERR_PROTOCOL` | -2 | The server response could not be parsed. |
| `BC_ERR_TIMEOUT` | -3 | The server did not respond within the configured timeout. |
| `BC_ERR_TRANSPORT` | -4 | The underlying connection failed. |
| `BC_ERR_SERVER` | -5 | The server reported an error (e.g., alias not found). |
| `BC_ERR_NOMEM` | -6 | Memory allocation failed. |

# ERRORS

- If **c** is NULL, returns `BC_ERR_PARAM`.
- If **name** is NULL or empty, returns `BC_ERR_PARAM`.
- If **value** is NULL, returns `BC_ERR_PARAM`.
- If the named alias does not exist, returns `BC_ERR_SERVER`.
- If aliases are disabled in the session (e.g., via
  `shopt -u expand_aliases`), the alias table may still be
  queried; this function queries the table regardless of
  whether alias expansion is active.

# EXAMPLES

# Retrieve an alias

```c
#include "bashclient.h"
#include <stdio.h>

int main(void)
{
    bc_client_t *client;
    char *val = NULL;
    int rc;

    client = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!client)
        return 1;

    /* Define an alias via eval */
    bc_eval(client, "alias ll='ls -la --color=auto'",
            NULL, NULL, NULL);

    /* Retrieve it */
    rc = bc_state_get_alias(client, "ll", &val);
    if (rc == BC_OK) {
        printf("ll -> %s\n", val);
        bc_free(val);
    } else {
        fprintf(stderr, "get_alias failed: %d\n", rc);
    }

    bc_disconnect(client);
    return 0;
}
```

# Check if an alias exists

```c
char *val = NULL;
int rc = bc_state_get_alias(client, "grep", &val);
if (rc == BC_ERR_SERVER) {
    printf("alias 'grep' is not defined\n");
} else if (rc == BC_OK) {
    printf("alias grep='%s'\n", val);
    bc_free(val);
}
```

# Retrieve multiple aliases

```c
const char *names[] = { "ls", "ll", "la", "grep", NULL };
for (int i = 0; names[i]; i++) {
    char *val = NULL;
    int rc = bc_state_get_alias(client, names[i], &val);
    if (rc == BC_OK) {
        printf("alias %s='%s'\n", names[i], val);
        bc_free(val);
    }
}
```

# SEE ALSO

`bc_state_set_alias`(3), `bc_state_unset_alias`(3),
`bc_state_inspect`(3), `bc_state_get_var`(3),
`bc_free`(3), `bc_connect`(3),
`bash-server-client-c`(7), `bash-server-channels`(7)

# NOTES

- The returned string must be freed with `bc_free()`, not
  standard `free()`.
- Alias values may contain embedded single quotes and other
  special characters.  The returned value is the raw expansion
  text, not shell-quoted.
- The alias namespace is separate from variables, functions,
  and traps.  A variable and an alias can share the same name
  without conflict.
- Aliases are a bash feature, not available in POSIX `sh` mode.
  If the server session is running in POSIX mode, alias
  operations still work on the internal alias table.
- The function uses the v2 binary protocol internally.  The STATE
  channel request is a JSON object with `"op": "get"`,
  `"ns": "alias"`, and `"name"` fields.
- A single `bc_client_t` must not be used concurrently from
  multiple threads.
