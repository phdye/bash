# bc\_state\_set\_var(3) — set a shell variable on the server

# SYNOPSIS

```c
#include "bashclient.h"

int bc_state_set_var(bc_client_t *c, const char *name,
                     const char *value,
                     const char *const *attributes,
                     int num_attrs);
```

# DESCRIPTION

Sets or creates a shell variable on the bash-server session
associated with the client connection **c**.  If the variable
already exists, its value and attributes are replaced.  If it does
not exist, a new variable is created.

The function sends a `set` request on the STATE channel
(`CHAN_STATE`, channel 2) with namespace `"var"`, the variable
name, its value, and an optional list of attribute strings.

Attributes control shell-level properties of the variable.  The
following attribute strings are recognized by bash-server:

| Attribute | Effect |
|-----------|--------|
| `"exported"` | Variable is exported to child processes (`-x`). |
| `"readonly"` | Variable cannot be unset or reassigned (`-r`). |
| `"integer"` | Variable undergoes arithmetic evaluation on assignment (`-i`). |
| `"local"` | Variable is local to a function scope. |
| `"array"` | Variable is an indexed array (`-a`). |
| `"assoc"` | Variable is an associative array (`-A`). |
| `"nameref"` | Variable is a name reference (`-n`). |
| `"uppercase"` | Value is converted to uppercase on assignment (`-u`). |
| `"lowercase"` | Value is converted to lowercase on assignment (`-l`). |
| `"trace"` | Variable has the trace attribute (`-t`). |

# PARAMETERS

| Parameter | Type | Description |
|-----------|------|-------------|
| `c` | `bc_client_t *` | Client connection handle. Must not be NULL. |
| `name` | `const char *` | Variable name. Must not be NULL or empty. Must follow bash naming rules. |
| `value` | `const char *` | Value to assign. Must not be NULL. May be an empty string. |
| `attributes` | `const char *const *` | Array of attribute strings, or NULL for no attributes. Each element must be a valid attribute name from the table above. |
| `num_attrs` | `int` | Number of elements in the **attributes** array. Must be 0 if **attributes** is NULL. |

# RETURN VALUE

Returns `BC_OK` (0) on success.

On failure, returns one of the following error codes:

| Code | Value | Meaning |
|------|-------|---------|
| `BC_ERR_PARAM` | -7 | A required parameter is NULL or invalid, or `num_attrs` is negative. |
| `BC_ERR_PROTOCOL` | -2 | The server response could not be parsed. |
| `BC_ERR_TIMEOUT` | -3 | The server did not respond within the configured timeout. |
| `BC_ERR_TRANSPORT` | -4 | The underlying connection failed. |
| `BC_ERR_SERVER` | -5 | The server rejected the operation (e.g., variable is readonly). |
| `BC_ERR_NOMEM` | -6 | Memory allocation failed. |

# ERRORS

- If **c** is NULL, returns `BC_ERR_PARAM`.
- If **name** is NULL or empty, returns `BC_ERR_PARAM`.
- If **value** is NULL, returns `BC_ERR_PARAM`.
- If **num_attrs** > 0 but **attributes** is NULL, returns
  `BC_ERR_PARAM`.
- If the target variable is marked `readonly` on the server,
  the server refuses the operation and the function returns
  `BC_ERR_SERVER`.
- If an unrecognized attribute name is given, the server may
  ignore it or return `BC_ERR_SERVER` depending on server
  configuration.

# EXAMPLES

# Set a simple variable

```c
int rc = bc_state_set_var(client, "GREETING", "hello", NULL, 0);
if (rc != BC_OK)
    fprintf(stderr, "set_var failed: %d\n", rc);
```

# Set an exported variable

```c
const char *attrs[] = { "exported" };
int rc = bc_state_set_var(client, "MY_PATH", "/usr/local/bin",
                          attrs, 1);
if (rc != BC_OK)
    fprintf(stderr, "set_var failed: %d\n", rc);
```

# Set a readonly integer variable

```c
const char *attrs[] = { "readonly", "integer" };
int rc = bc_state_set_var(client, "MAX_RETRIES", "5",
                          attrs, 2);
if (rc != BC_OK)
    fprintf(stderr, "set_var failed: %d\n", rc);
```

# Update an existing variable's value

```c
/* Existing attributes are replaced, not merged */
int rc = bc_state_set_var(client, "COUNTER", "42", NULL, 0);
```

# SEE ALSO

`bc_state_get_var`(3), `bc_state_unset_var`(3),
`bc_var_info_free`(3), `bc_state_inspect`(3),
`bc_connect`(3), `bash-server-client-c`(7),
`bash-server-channels`(7)

# NOTES

- Setting a variable replaces both its value and its attributes.
  If you want to preserve existing attributes, first retrieve
  them with `bc_state_get_var()`, then pass them back along with
  any additions.
- The **value** parameter is always a string.  For integer
  variables (attribute `"integer"`), the server performs
  arithmetic evaluation on the string value at assignment time.
- Variable names are case-sensitive.  `"PATH"` and `"path"` are
  distinct variables.
- The function uses the v2 binary protocol.  The STATE channel
  request includes `"op": "set"`, `"ns": "var"`, `"name"`,
  `"value"`, and optionally `"attributes"` fields.
- A single `bc_client_t` must not be used concurrently from
  multiple threads.
