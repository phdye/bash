# bc\_state\_inspect(3) — inspect shell state on the server

# SYNOPSIS

```c
#include "bashclient.h"

int bc_state_inspect(bc_client_t *c, const char *query,
                     char **json_result);
```

# DESCRIPTION

Queries the bash-server session for a listing of shell state
objects in a particular namespace.  Returns a JSON-formatted
string containing an array of entries for the requested category.

The function sends an `inspect` request on the STATE channel
(`CHAN_STATE`, channel 2) with the given query string.  The server
enumerates the requested namespace and returns a JSON array.

# Supported queries

| Query | Returns |
|-------|---------|
| `"vars"` | Array of all shell variables with their names, values, and attributes. |
| `"functions"` | Array of all defined shell functions with their names and definitions. |
| `"aliases"` | Array of all defined aliases with their names and expansion values. |
| `"traps"` | Array of all active signal traps with signal names and command strings. |

# Response format

The returned JSON string is a JSON array.  The structure of each
element depends on the query type:

**vars:**
```json
[
  {"name": "HOME", "value": "/home/user", "attributes": ["exported"]},
  {"name": "BASH_VERSION", "value": "5.1.16(1)-release", "attributes": []},
  ...
]
```

**functions:**
```json
[
  {"name": "greet", "definition": "{\n    echo \"hello $1\"\n}"},
  ...
]
```

**aliases:**
```json
[
  {"name": "ll", "value": "ls -la"},
  {"name": "grep", "value": "grep --color=auto"},
  ...
]
```

**traps:**
```json
[
  {"signal": "EXIT", "command": "cleanup"},
  {"signal": "SIGINT", "command": "echo caught"},
  ...
]
```

The caller is responsible for freeing the returned JSON string by
calling `bc_free()`.

# PARAMETERS

| Parameter | Type | Description |
|-----------|------|-------------|
| `c` | `bc_client_t *` | Client connection handle, previously established via `bc_connect()`. Must not be NULL. |
| `query` | `const char *` | Query type string. Must be one of `"vars"`, `"functions"`, `"aliases"`, or `"traps"`. Must not be NULL or empty. |
| `json_result` | `char **` | Pointer to a `char *` that receives the malloc'd JSON string on success. Must not be NULL. Caller frees via `bc_free()`. |

# RETURN VALUE

Returns `BC_OK` (0) on success.  **\*json_result** points to a
newly allocated JSON array string.

On failure, returns one of the following error codes:

| Code | Value | Meaning |
|------|-------|---------|
| `BC_ERR_PARAM` | -7 | A required parameter is NULL or invalid, or the query string is not recognized. |
| `BC_ERR_PROTOCOL` | -2 | The server response could not be parsed. |
| `BC_ERR_TIMEOUT` | -3 | The server did not respond within the configured timeout. |
| `BC_ERR_TRANSPORT` | -4 | The underlying connection failed. |
| `BC_ERR_SERVER` | -5 | The server reported an error. |
| `BC_ERR_NOMEM` | -6 | Memory allocation failed. |

# ERRORS

- If **c** is NULL, returns `BC_ERR_PARAM`.
- If **query** is NULL, empty, or not one of the four recognized
  values, returns `BC_ERR_PARAM`.
- If **json_result** is NULL, returns `BC_ERR_PARAM`.
- If the connection is not authenticated, returns
  `BC_ERR_PROTOCOL`.
- If the JSON response from the server is malformed, returns
  `BC_ERR_PROTOCOL`.

# EXAMPLES

# List all variables

```c
#include "bashclient.h"
#include <stdio.h>

int main(void)
{
    bc_client_t *client;
    char *json = NULL;
    int rc;

    client = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!client)
        return 1;

    rc = bc_state_inspect(client, "vars", &json);
    if (rc == BC_OK) {
        printf("Variables:\n%s\n", json);
        bc_free(json);
    } else {
        fprintf(stderr, "inspect failed: %d\n", rc);
    }

    bc_disconnect(client);
    return 0;
}
```

# List all functions

```c
char *json = NULL;
int rc = bc_state_inspect(client, "functions", &json);
if (rc == BC_OK) {
    printf("Functions:\n%s\n", json);
    bc_free(json);
}
```

# List all aliases

```c
char *json = NULL;
int rc = bc_state_inspect(client, "aliases", &json);
if (rc == BC_OK) {
    printf("Aliases:\n%s\n", json);
    bc_free(json);
}
```

# List active traps

```c
char *json = NULL;
int rc = bc_state_inspect(client, "traps", &json);
if (rc == BC_OK) {
    printf("Traps:\n%s\n", json);
    bc_free(json);
}
```

# Snapshot entire session state

```c
const char *queries[] = { "vars", "functions", "aliases",
                          "traps", NULL };
for (int i = 0; queries[i]; i++) {
    char *json = NULL;
    int rc = bc_state_inspect(client, queries[i], &json);
    if (rc == BC_OK) {
        printf("=== %s ===\n%s\n\n", queries[i], json);
        bc_free(json);
    }
}
```

# SEE ALSO

`bc_state_get_var`(3), `bc_state_get_func`(3),
`bc_state_get_alias`(3), `bc_state_set_trap`(3),
`bc_free`(3), `bc_connect`(3),
`bash-server-client-c`(7), `bash-server-channels`(7)

# NOTES

- The returned JSON string can be large if the session has many
  variables or functions.  The `"vars"` query in particular may
  return hundreds of entries for a typical bash session that
  includes environment variables.
- The JSON response is a single string, not a parsed data
  structure.  The caller must use a JSON parsing library (e.g.,
  cJSON, jansson) to extract individual entries.
- The `"vars"` query returns all variables including shell
  built-in variables (`BASH_VERSION`, `RANDOM`, etc.) and
  environment variables.  There is no built-in filtering
  mechanism; filter client-side after parsing.
- The `"traps"` query only returns signals that have active
  traps.  Signals at their default disposition are not listed.
- For retrieving a single item by name, prefer the specific
  get functions (`bc_state_get_var`, `bc_state_get_func`,
  `bc_state_get_alias`) which are more efficient than parsing
  the full inspect output.
- The returned string must be freed with `bc_free()`, not
  standard `free()`.
- The function uses the v2 binary protocol internally.  The STATE
  channel request is a JSON object with `"op": "inspect"` and
  `"query"` fields.
- A single `bc_client_t` must not be used concurrently from
  multiple threads.
