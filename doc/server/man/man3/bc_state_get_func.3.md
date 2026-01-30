# bc\_state\_get\_func(3) — get a shell function definition from the server

# SYNOPSIS

```c
#include "bashclient.h"

int bc_state_get_func(bc_client_t *c, const char *name,
                      char **definition);
```

# DESCRIPTION

Retrieves the definition of a shell function from the bash-server
session associated with the client connection **c**.  On success,
**\*definition** is set to a newly allocated string containing the
complete function body as it would be displayed by `declare -f`.

The function sends a `get` request on the STATE channel
(`CHAN_STATE`, channel 2) with namespace `"func"` and the given
function name.  The server looks up the function in its internal
function table and returns the definition text.

The returned definition string includes the function body with
braces and whitespace, matching the output format of
`declare -f name`.  For example, for a function defined as
`greet() { echo "hello $1"; }`, the returned definition would be:

```
{
    echo "hello $1"
}
```

The caller is responsible for freeing the returned string by
calling `bc_free()` when it is no longer needed.

If the function does not exist on the server, the function returns
`BC_ERR_SERVER` and **\*definition** is not modified.

# PARAMETERS

| Parameter | Type | Description |
|-----------|------|-------------|
| `c` | `bc_client_t *` | Client connection handle, previously established via `bc_connect()`. Must not be NULL. |
| `name` | `const char *` | Name of the shell function to retrieve. Must not be NULL or empty. Function names follow bash identifier rules (alphanumeric and underscore). |
| `definition` | `char **` | Pointer to a `char *` that receives the malloc'd definition string on success. Must not be NULL. Caller frees via `bc_free()`. |

# RETURN VALUE

Returns `BC_OK` (0) on success.  **\*definition** points to a
newly allocated string containing the function body.

On failure, returns one of the following error codes:

| Code | Value | Meaning |
|------|-------|---------|
| `BC_ERR_PARAM` | -7 | A required parameter is NULL or invalid. |
| `BC_ERR_PROTOCOL` | -2 | The server response could not be parsed. |
| `BC_ERR_TIMEOUT` | -3 | The server did not respond within the configured timeout. |
| `BC_ERR_TRANSPORT` | -4 | The underlying connection failed. |
| `BC_ERR_SERVER` | -5 | The server reported an error (e.g., function not found). |
| `BC_ERR_NOMEM` | -6 | Memory allocation failed. |

# ERRORS

- If **c** is NULL, returns `BC_ERR_PARAM`.
- If **name** is NULL or empty, returns `BC_ERR_PARAM`.
- If **definition** is NULL, returns `BC_ERR_PARAM`.
- If the named function does not exist in the server session,
  returns `BC_ERR_SERVER`.
- If the connection is not authenticated, returns
  `BC_ERR_PROTOCOL`.

# EXAMPLES

# Retrieve and display a function definition

```c
#include "bashclient.h"
#include <stdio.h>

int main(void)
{
    bc_client_t *client;
    char *def = NULL;
    int rc;

    client = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!client) {
        fprintf(stderr, "connection failed\n");
        return 1;
    }

    /* First, define a function via eval */
    bc_eval(client, "greet() { echo \"hello $1\"; }",
            NULL, NULL, NULL);

    /* Now retrieve its definition */
    rc = bc_state_get_func(client, "greet", &def);
    if (rc == BC_OK) {
        printf("greet() %s\n", def);
        bc_free(def);
    } else {
        fprintf(stderr, "get_func failed: %d\n", rc);
    }

    bc_disconnect(client);
    return 0;
}
```

# Check if a function exists

```c
char *def = NULL;
int rc = bc_state_get_func(client, "my_func", &def);
if (rc == BC_ERR_SERVER) {
    printf("function does not exist\n");
} else if (rc == BC_OK) {
    printf("function exists, %zu bytes\n", strlen(def));
    bc_free(def);
}
```

# Enumerate all functions

```c
/* Use bc_state_inspect for listing, then get_func for details */
char *json = NULL;
int rc = bc_state_inspect(client, "functions", &json);
if (rc == BC_OK) {
    /* Parse JSON array of function names, then call
       bc_state_get_func for each */
    bc_free(json);
}
```

# SEE ALSO

`bc_state_unset_func`(3), `bc_state_inspect`(3),
`bc_state_get_var`(3), `bc_free`(3),
`bc_connect`(3), `bash-server-client-c`(7),
`bash-server-channels`(7)

# NOTES

- The returned string must be freed with `bc_free()`, not
  standard `free()`.  While they may be equivalent in the current
  implementation, using `bc_free()` ensures correct behavior if
  the library's allocator changes.
- There is no `bc_state_set_func()` in the client library.
  To define or replace a shell function, use `bc_eval()` with
  the function definition syntax (e.g.,
  `"myfunc() { commands; }"`).
- The function definition string may contain newlines and
  embedded special characters.  It is the raw bash function body,
  not escaped for any particular output format.
- The function uses the v2 binary protocol internally.  The STATE
  channel request is a JSON object with `"op": "get"`,
  `"ns": "func"`, and `"name"` fields.
- A single `bc_client_t` must not be used concurrently from
  multiple threads.
