# bc\_state\_get\_var(3) — get a shell variable from the server

# SYNOPSIS

```c
#include "bashclient.h"

typedef struct {
    char  *name;
    char  *value;
    char **attributes;
    int    num_attributes;
} bc_var_info_t;

int bc_state_get_var(bc_client_t *c, const char *name,
                     bc_var_info_t *info);
```

# DESCRIPTION

Retrieves a shell variable from the bash-server session associated
with the client connection **c**.  On success, the **info** structure
is populated with the variable's name, value, and any attribute
flags (such as `exported`, `readonly`, `integer`, etc.).

The function sends a `get` request on the STATE channel
(`CHAN_STATE`, channel 2) with namespace `"var"` and the given
variable name.  The server responds with the variable's current
value and attributes, which are decoded and stored into the
caller-provided `bc_var_info_t` structure.

All strings within **info** are freshly allocated.  The caller is
responsible for freeing them by calling `bc_var_info_free()` when
they are no longer needed.  Failure to do so will result in memory
leaks.

If the variable does not exist on the server, the function returns
`BC_ERR_SERVER` and the contents of **info** are left in an
indeterminate state (no strings are allocated in the error case).

# PARAMETERS

| Parameter | Type | Description |
|-----------|------|-------------|
| `c` | `bc_client_t *` | Client connection handle, previously established via `bc_connect()`. Must not be NULL. |
| `name` | `const char *` | Name of the shell variable to retrieve. Must not be NULL or empty. Standard bash variable naming rules apply (alphanumeric and underscore, not starting with a digit). |
| `info` | `bc_var_info_t *` | Pointer to a caller-allocated structure that receives the variable information. Must not be NULL. |

# bc\_var\_info\_t fields

| Field | Type | Description |
|-------|------|-------------|
| `name` | `char *` | Variable name (malloc'd copy). |
| `value` | `char *` | Variable value as a string (malloc'd copy). NULL values on the server are returned as an empty string. |
| `attributes` | `char **` | Array of attribute strings (e.g., `"exported"`, `"readonly"`). Each string is malloc'd. The array itself is malloc'd. NULL if no attributes. |
| `num_attributes` | `int` | Number of elements in the `attributes` array. Zero if no attributes. |

# RETURN VALUE

Returns `BC_OK` (0) on success.

On failure, returns one of the following error codes:

| Code | Value | Meaning |
|------|-------|---------|
| `BC_ERR_PARAM` | -7 | A required parameter is NULL or invalid. |
| `BC_ERR_PROTOCOL` | -2 | The server response could not be parsed. |
| `BC_ERR_TIMEOUT` | -3 | The server did not respond within the configured timeout. |
| `BC_ERR_TRANSPORT` | -4 | The underlying connection failed (broken pipe, closed socket). |
| `BC_ERR_SERVER` | -5 | The server reported an error (e.g., variable not found). |
| `BC_ERR_NOMEM` | -6 | Memory allocation failed while building the request or parsing the response. |

# ERRORS

- If **c** is NULL, returns `BC_ERR_PARAM`.
- If **name** is NULL or empty, returns `BC_ERR_PARAM`.
- If **info** is NULL, returns `BC_ERR_PARAM`.
- If the connection is not authenticated, the server rejects the
  request and the function returns `BC_ERR_PROTOCOL`.
- If the named variable does not exist, returns `BC_ERR_SERVER`.

# EXAMPLES

# Retrieve and print a variable

```c
#include "bashclient.h"
#include <stdio.h>

int main(void)
{
    bc_client_t *client;
    bc_var_info_t info;
    int rc;

    client = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!client) {
        fprintf(stderr, "connection failed\n");
        return 1;
    }

    rc = bc_state_get_var(client, "PATH", &info);
    if (rc != BC_OK) {
        fprintf(stderr, "get_var failed: %d\n", rc);
        bc_disconnect(client);
        return 1;
    }

    printf("name:  %s\n", info.name);
    printf("value: %s\n", info.value);
    for (int i = 0; i < info.num_attributes; i++)
        printf("attr:  %s\n", info.attributes[i]);

    bc_var_info_free(&info);
    bc_disconnect(client);
    return 0;
}
```

# Check if a variable exists

```c
bc_var_info_t info;
int rc = bc_state_get_var(client, "MY_VAR", &info);
if (rc == BC_ERR_SERVER) {
    /* Variable does not exist */
} else if (rc == BC_OK) {
    /* Variable exists — use info, then free */
    bc_var_info_free(&info);
}
```

# SEE ALSO

`bc_state_set_var`(3), `bc_state_unset_var`(3),
`bc_var_info_free`(3), `bc_state_inspect`(3),
`bc_connect`(3), `bash-server-client-c`(7),
`bash-server-channels`(7)

# NOTES

- The `bc_var_info_t` structure must be freed with
  `bc_var_info_free()`, not manually.  The free function handles
  all internal pointers and NULLs them out for safety.
- Array and associative array variables are returned as their
  string representation.  Use `bc_state_inspect()` with the
  `"vars"` query for structured enumeration.
- The function uses the v2 binary protocol internally.  The STATE
  channel request is a JSON object with `"op": "get"`,
  `"ns": "var"`, and `"name"` fields.
- This function is thread-safe with respect to different client
  connections, but a single `bc_client_t` must not be used
  concurrently from multiple threads.
