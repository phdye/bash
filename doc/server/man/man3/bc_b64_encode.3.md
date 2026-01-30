# bc\_b64\_encode(3) -- base64-encode binary data

# SYNOPSIS

    #include "bashclient.h"

    char *bc_b64_encode(const char *data, size_t len);

# DESCRIPTION

Encodes **len** bytes of binary **data** into a NUL-terminated
base64 string using the standard RFC 4648 alphabet
(`A-Z`, `a-z`, `0-9`, `+`, `/`) with `=` padding.

The returned string is dynamically allocated and must be freed by
the caller using `bc_free(3)`.

This is a low-level utility function used internally by the client
library for PTY data encoding (`bc_pty_write(3)`) and other binary
payloads.  Most callers do not need to use it directly; the
channel-specific functions handle encoding automatically.

# Encoding details

- Input of 0 bytes produces an empty string (`""`).
- Output length is `4 * ceil(len / 3) + 1` bytes (including NUL).
- No line breaks are inserted (single continuous string).
- Padding `=` characters are appended to make the output length a
  multiple of 4.

# PARAMETERS

| Parameter | Type           | Description                              |
|-----------|----------------|------------------------------------------|
| `data`    | `const char *` | Input buffer of raw bytes to encode.     |
| `len`     | `size_t`       | Number of bytes to encode.               |

# RETURN VALUE

Returns a `malloc`'d NUL-terminated base64 string on success.
The caller must free it with `bc_free(3)`.

Returns **NULL** if memory allocation fails or if `data` is NULL
and `len` is non-zero.

# ERRORS

The only failure mode is memory allocation failure, which returns
NULL.  `errno` is set to `ENOMEM` in this case.

# EXAMPLES

# Encode a simple string

```c
const char *msg = "Hello, World!";
char *encoded = bc_b64_encode(msg, strlen(msg));
if (encoded) {
    printf("Encoded: %s\n", encoded);  /* SGVsbG8sIFdvcmxkIQ== */
    bc_free(encoded);
}
```

# Encode binary data

```c
unsigned char binary[] = {0x00, 0xFF, 0x42, 0x13, 0x37};
char *encoded = bc_b64_encode((const char *)binary, sizeof(binary));
if (encoded) {
    printf("Encoded: %s\n", encoded);
    bc_free(encoded);
}
```

# Round-trip encode/decode

```c
const char *original = "binary\x00data\x01here";
size_t orig_len = 16;

char *encoded = bc_b64_encode(original, orig_len);
size_t decoded_len;
char *decoded = bc_b64_decode(encoded, &decoded_len);

assert(decoded_len == orig_len);
assert(memcmp(original, decoded, orig_len) == 0);

bc_free(encoded);
bc_free(decoded);
```

# Encoding size calculation

```c
/* Output is always 4 * ceil(N/3) characters + NUL */
size_t input_len = 100;
size_t expected_len = 4 * ((input_len + 2) / 3);  /* 136 */

char *encoded = bc_b64_encode(data, input_len);
assert(strlen(encoded) == expected_len);
bc_free(encoded);
```

# SEE ALSO

`bc_b64_decode(3)`, `bc_free(3)`, `bc_pty_write(3)`,
`bc_connect(3)`, `bash-server-ndjson(5)`,
`bash-server-client-c(7)`

# NOTES

- The encoding uses standard base64 (not URL-safe base64).  The
  `+` and `/` characters appear in the output and are safe within
  JSON string values.

- The function handles NUL bytes in the input correctly; the
  **len** parameter determines how many bytes are encoded, not
  `strlen`.

- For large inputs, the output is approximately 33% larger than
  the input.  A 750 KB input produces approximately 1 MB of
  base64 output.

- This function is not thread-safe with respect to `malloc`.
  If the application uses a custom allocator, ensure it is
  thread-safe.

- The implementation does not use lookup tables; it uses arithmetic
  encoding for minimal code size.
