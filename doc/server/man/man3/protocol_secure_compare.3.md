# protocol_secure_compare(3) -- Constant-time string comparison for security tokens

# SYNOPSIS

    #include "server.h"

    int protocol_secure_compare(const char *a, const char *b);

# DESCRIPTION

Performs a constant-time comparison of two NUL-terminated strings to prevent
timing side-channel attacks when verifying authentication tokens.

If the strings differ in length, the function returns 0 immediately.  This
reveals the length mismatch but is acceptable for fixed-length hex-encoded
tokens (which are always `SERVER_TOKEN_HEXLEN` = 64 characters).

When the strings have equal length, every byte is compared using XOR
accumulation.  The comparison always processes the full length regardless of
where the first difference occurs, ensuring that the execution time does not
leak information about the position of mismatching characters.

This function is used by the authentication handler to verify client tokens
against the server's stored token.

# PARAMETERS

| Parameter | Type           | Description                          |
|-----------|----------------|--------------------------------------|
| `a`       | `const char *` | First NUL-terminated string.         |
| `b`       | `const char *` | Second NUL-terminated string.        |

# RETURN VALUE

Returns **1** if the strings are identical (same length and same content).

Returns **0** if the strings differ in length or content.

# ERRORS

No error conditions.  The function always returns 0 or 1.

# EXAMPLES

```c
/* Verify client authentication token */
if (protocol_secure_compare(client_token, config->auth_token)) {
    session->authenticated = 1;
    protocol_write_line(fd, "OK authenticated");
} else {
    protocol_write_line(fd, "ERR invalid token");
}
```

# SEE ALSO

`session_handle(3)`, `protocol_read_line(3)`

# NOTES

- The comparison is constant-time only when both strings have the same
  length.  A length mismatch returns 0 immediately, which could reveal
  that the lengths differ.  For the bash-server protocol, tokens are
  always 64 hex characters (`SERVER_TOKEN_HEXLEN`), so this early return
  only triggers for malformed input.

- The XOR accumulation technique (`result |= a[i] ^ b[i]`) ensures that
  all bytes are compared even if an early byte differs.  The final check
  `result == 0` is true only if every byte matched.

- This function is not suitable for comparing binary data with embedded
  NUL bytes, as it relies on `strlen(3)` to determine string length.

- The function uses `unsigned char` casts for the XOR to avoid
  sign-extension issues.
