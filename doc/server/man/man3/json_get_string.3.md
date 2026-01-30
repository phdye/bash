# json_get_string(3) -- Extract a string value from a JSON object

# SYNOPSIS

    #include "server.h"

    const char *json_get_string(const char *json, const char *key,
                                char *buf, size_t bufsize);

# DESCRIPTION

Extracts the string value associated with `key` from a JSON object string.
Searches for the pattern `"key":"value"` using `strstr(3)`, then copies the
unescaped value into `buf`.

The function handles standard JSON string escapes: `\"`, `\\`, `\/`, `\n`,
`\r`, `\t`, `\b`, `\f`, and `\uXXXX` (ASCII range only, codes >= 0x80 are
silently dropped).

This is a minimal JSON parser designed for the flat JSON objects used in the
bash-server protocol.  It does not traverse nested objects or arrays.

# PARAMETERS

| Parameter | Type           | Description                                    |
|-----------|----------------|------------------------------------------------|
| `json`    | `const char *` | JSON object string to search.                  |
| `key`     | `const char *` | Key name to look up (without quotes).          |
| `buf`     | `char *`       | Output buffer for the unescaped string value.  |
| `bufsize` | `size_t`       | Size of `buf` in bytes.                        |

# RETURN VALUE

Returns a pointer to `buf` on success (same pointer passed in).

Returns **NULL** if:
- The key is not found in the JSON string.
- The value associated with the key is not a string (does not start with `"`).
- The key name exceeds 253 characters.
- Unescape fails (malformed escape sequence).

# ERRORS

No `errno` is set.  NULL return indicates the key was not found or the value
is not a string.

# EXAMPLES

```c
const char *json = "{\"type\":\"auth\",\"token\":\"abc123\"}";
char type[64], token[256];

if (json_get_string(json, "type", type, sizeof(type))) {
    printf("type = %s\n", type);  /* "auth" */
}

if (json_get_string(json, "token", token, sizeof(token))) {
    printf("token = %s\n", token);  /* "abc123" */
}

/* Key not found */
if (!json_get_string(json, "missing", type, sizeof(type))) {
    printf("key not found\n");
}
```

Handling escaped values:

```c
const char *json = "{\"msg\":\"hello\\nworld\"}";
char msg[256];

json_get_string(json, "msg", msg, sizeof(msg));
/* msg = "hello\nworld" (actual newline character) */
```

# SEE ALSO

`json_get_int(3)`, `json_frame_read(3)`,
`protocol_parse_command(3)`

# NOTES

- The search uses `strstr(3)` to find `"key"` in the JSON string.  This
  means a key could theoretically match inside a string value if the value
  contains the same pattern.  For the bash-server protocol's limited
  vocabulary, this is not a practical concern.

- The key search buffer is limited to 256 bytes (including quotes).  Keys
  longer than 253 characters will not be found.

- Unicode escape sequences (`\uXXXX`) are only decoded for code points in
  the ASCII range (< 0x80).  Non-ASCII code points are silently skipped.

- The function skips whitespace and colons between the key and value,
  tolerating variations in JSON formatting.

- This is not a general-purpose JSON parser.  It does not handle nested
  objects, arrays, or values that are not strings.  Use `json_get_int(3)`
  for integer values.
