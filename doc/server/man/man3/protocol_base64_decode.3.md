# protocol_base64_decode(3) -- Decode a base64-encoded string

# SYNOPSIS

    #include "server.h"

    char *protocol_base64_decode(const char *data, size_t *outlen);

# DESCRIPTION

Decodes a NUL-terminated base64 string into binary data.  The returned
buffer is heap-allocated with `malloc(3)` and NUL-terminated (the NUL is
not counted in `*outlen`).  The caller is responsible for freeing the
returned buffer.

The input must use the standard base64 alphabet (`A-Za-z0-9+/`) with `=`
padding.  The input length must be a multiple of 4; otherwise, decoding
fails.

Invalid characters (bytes not in the base64 alphabet and not `=`) in the
first two positions of any 4-byte group cause the function to fail and
return NULL.

A static 256-byte lookup table is lazily initialized on the first call.

# PARAMETERS

| Parameter | Type      | Description                                        |
|-----------|-----------|----------------------------------------------------|
| `data`    | `const char *` | NUL-terminated base64 string to decode.       |
| `outlen`  | `size_t *`     | Set to the number of decoded bytes on success, or 0 on error. |

# RETURN VALUE

Returns a pointer to a `malloc`'d buffer containing the decoded data.
The buffer is NUL-terminated for convenience, but may contain embedded
NUL bytes; use `*outlen` for the true length.

Returns **NULL** on error, with `*outlen` set to 0.

# ERRORS

Returns NULL when:

- The input length is not a multiple of 4.
- An invalid character is found in the first two bytes of any 4-byte group.
- `malloc(3)` fails.

# EXAMPLES

```c
const char *encoded = "SGVsbG8sIFdvcmxkIQ==";
size_t decoded_len;
char *decoded = protocol_base64_decode(encoded, &decoded_len);
if (!decoded) {
    fprintf(stderr, "decode failed\n");
    return -1;
}

/* decoded = "Hello, World!", decoded_len = 13 */
printf("decoded (%zu bytes): %s\n", decoded_len, decoded);
free(decoded);
```

Handling binary data (may contain NUL bytes):

```c
size_t len;
char *bin = protocol_base64_decode(b64_string, &len);
if (bin) {
    write(output_fd, bin, len);  /* use len, not strlen */
    free(bin);
}
```

# SEE ALSO

`protocol_base64_encode(3)`, `protocol_read_line(3)`,
`json_get_string(3)`

# NOTES

- The decode table is initialized once (lazily on first call) and reused
  for all subsequent calls.  This initialization is not thread-safe, but
  bash-server is single-threaded per process (fork-per-session).

- The function allocates `(len / 4) * 3 + 1` bytes, adjusted downward for
  padding characters.

- The returned buffer is always NUL-terminated for convenience with string
  functions, but callers handling binary data must use `*outlen` to
  determine the actual data length.
