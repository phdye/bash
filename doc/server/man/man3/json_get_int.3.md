# json_get_int(3) -- Extract an integer value from a JSON object

# SYNOPSIS

    #include "server.h"

    int json_get_int(const char *json, const char *key, int *value);

# DESCRIPTION

Extracts the integer value associated with `key` from a JSON object string.
Searches for the pattern `"key":N` using `strstr(3)`, then converts the
numeric portion using `atoi(3)`.

The function accepts both positive and negative integers (values starting
with `-` or a digit).

This is a minimal JSON parser designed for the flat JSON objects used in the
bash-server protocol.  It does not traverse nested objects.

# PARAMETERS

| Parameter | Type           | Description                                |
|-----------|----------------|--------------------------------------------|
| `json`    | `const char *` | JSON object string to search.              |
| `key`     | `const char *` | Key name to look up (without quotes).      |
| `value`   | `int *`        | Receives the integer value on success.     |

# RETURN VALUE

Returns **0** on success, with `*value` set to the extracted integer.

Returns **-1** if the key is not found or the value is not a number.

# ERRORS

Returns -1 when:

- The key is not found in the JSON string.
- The character after `"key":` (skipping whitespace) is neither a digit
  nor `-`.
- The key name exceeds 253 characters.

# EXAMPLES

```c
const char *json = "{\"ch\":2,\"exit_code\":0}";
int channel, exit_code;

if (json_get_int(json, "ch", &channel) == 0) {
    printf("channel = %d\n", channel);  /* 2 */
}

if (json_get_int(json, "exit_code", &exit_code) == 0) {
    printf("exit_code = %d\n", exit_code);  /* 0 */
}

/* Key not found */
if (json_get_int(json, "missing", &channel) < 0) {
    printf("key not found\n");
}
```

Negative values:

```c
const char *json = "{\"offset\":-42}";
int offset;
json_get_int(json, "offset", &offset);
/* offset = -42 */
```

# SEE ALSO

`json_get_string(3)`, `json_frame_read(3)`

# NOTES

- Uses `atoi(3)` for conversion, which does not detect overflow or
  invalid trailing characters.  For the bash-server protocol, integer
  values are small (channel IDs, exit codes, levels) so this is adequate.

- The search uses the same `strstr(3)` approach as `json_get_string(3)`.
  See the notes on that function regarding potential false matches in
  string values.

- The key search buffer is limited to 256 bytes.

- Boolean values (`true`, `false`) and `null` are not matched by this
  function.  Use `json_get_bool()` (internal, not exported) for booleans.

- Floating-point values will be truncated to integer by `atoi(3)`.
