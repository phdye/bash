# bc\_b64\_decode(3) -- base64-decode a string

# SYNOPSIS

    #include "bashclient.h"

    char *bc_b64_decode(const char *encoded, size_t *out_len);

# DESCRIPTION

Decodes a NUL-terminated base64 string into raw binary data.
Accepts standard RFC 4648 base64 (`A-Z`, `a-z`, `0-9`, `+`, `/`)
with optional `=` padding.

The returned buffer is dynamically allocated and must be freed by
the caller using `bc_free(3)`.  The buffer is NUL-terminated for
convenience (one extra byte beyond `*out_len`), but the decoded
data may contain embedded NUL bytes, so `*out_len` should be used
to determine the actual length.

This is a low-level utility function used internally by the client
library for decoding PTY output (`bc_pty_on_output(3)`) and other
binary payloads.  Most callers do not need to use it directly.

# Decoding details

- Whitespace (spaces, tabs, newlines, carriage returns) in the
  input is silently skipped, allowing formatted base64 input.
- Missing padding is tolerated (1, 2, or 3 trailing characters
  without `=` padding are decoded correctly).
- Invalid characters (not in the base64 alphabet, whitespace, or
  `=`) cause the function to return NULL.
- An empty input string produces a zero-length buffer
  (`*out_len == 0`).

# PARAMETERS

| Parameter  | Type           | Description                                    |
|------------|----------------|------------------------------------------------|
| `encoded`  | `const char *` | NUL-terminated base64 string to decode.        |
| `out_len`  | `size_t *`     | Receives the number of decoded bytes. Must not be NULL. |

# RETURN VALUE

Returns a `malloc`'d buffer containing the decoded bytes on success.
`*out_len` is set to the number of decoded bytes.  The buffer has
an extra NUL byte at `buffer[*out_len]` for convenience.

The caller must free the buffer with `bc_free(3)`.

Returns **NULL** on error (invalid input or allocation failure).
`*out_len` is set to 0 on error.

# ERRORS

Returns NULL when:

- `encoded` is NULL.
- `out_len` is NULL.
- The input contains invalid base64 characters.
- Memory allocation fails (`errno` set to `ENOMEM`).

# EXAMPLES

# Decode a simple string

```c
size_t len;
char *decoded = bc_b64_decode("SGVsbG8sIFdvcmxkIQ==", &len);
if (decoded) {
    printf("Decoded (%zu bytes): %s\n", len, decoded);
    /* Output: Decoded (13 bytes): Hello, World! */
    bc_free(decoded);
}
```

# Decode without padding

```c
size_t len;
/* Same input without padding — still works */
char *decoded = bc_b64_decode("SGVsbG8sIFdvcmxkIQ", &len);
assert(len == 13);
assert(memcmp(decoded, "Hello, World!", 13) == 0);
bc_free(decoded);
```

# Handle binary data with embedded NULs

```c
size_t len;
char *decoded = bc_b64_decode("AABBAA==", &len);
if (decoded) {
    /* Use len, not strlen — data may contain NUL bytes */
    for (size_t i = 0; i < len; i++) {
        printf("%02x ", (unsigned char)decoded[i]);
    }
    printf("\n");
    bc_free(decoded);
}
```

# Error handling

```c
size_t len;
char *decoded = bc_b64_decode("not!valid@base64", &len);
if (decoded == NULL) {
    fprintf(stderr, "decode failed (len=%zu)\n", len);
    /* len is 0 on error */
}
```

# Round-trip with bc\_b64\_encode

```c
const char original[] = {0x00, 0x01, 0x02, 0xFE, 0xFF};
size_t orig_len = sizeof(original);

char *encoded = bc_b64_encode(original, orig_len);
size_t decoded_len;
char *decoded = bc_b64_decode(encoded, &decoded_len);

assert(decoded_len == orig_len);
assert(memcmp(original, decoded, orig_len) == 0);

bc_free(encoded);
bc_free(decoded);
```

# SEE ALSO

`bc_b64_encode(3)`, `bc_free(3)`, `bc_pty_on_output(3)`,
`bc_connect(3)`, `bash-server-ndjson(5)`,
`bash-server-client-c(7)`

# NOTES

- The output buffer size is at most `3 * strlen(encoded) / 4`
  bytes plus a NUL terminator.

- Whitespace skipping allows decoding base64 that has been
  line-wrapped (e.g., at 76 characters as in MIME).  However,
  the bash-server wire protocol never wraps base64.

- The NUL-terminated convenience byte means the buffer allocation
  is `*out_len + 1` bytes.  This is useful when the decoded data
  is known to be text, but callers should always rely on
  `*out_len` for binary data.

- This function is not thread-safe with respect to `malloc`.

- The implementation is constant-time with respect to the input
  alphabet validation (no early exits on invalid characters) to
  avoid timing side channels, consistent with the server-side
  `b64_decode` implementation.
