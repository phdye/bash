# protocol_base64_encode(3) -- Base64-encode binary data

# SYNOPSIS

    #include "server.h"

    char *protocol_base64_encode(const char *data, size_t len);

# DESCRIPTION

Encodes `len` bytes of binary data pointed to by `data` into a
NUL-terminated base64 string using the standard base64 alphabet
(`A-Za-z0-9+/`) with `=` padding.

The returned string is heap-allocated with `malloc(3)`.  The caller is
responsible for freeing it.

The output length is `((len + 2) / 3) * 4` characters plus a NUL
terminator.

Base64 encoding is used throughout the bash-server protocol to transport
binary-safe payloads (command output, variable values, function definitions)
over the text-based v1 protocol and in the `"encoding":"base64"` fields of
v2 JSON messages.

# PARAMETERS

| Parameter | Type           | Description                        |
|-----------|----------------|------------------------------------|
| `data`    | `const char *` | Pointer to the data to encode.     |
| `len`     | `size_t`       | Number of bytes to encode.         |

# RETURN VALUE

Returns a pointer to a `malloc`'d NUL-terminated base64 string.

Returns **NULL** if `malloc(3)` fails.

# ERRORS

The only error condition is memory allocation failure, in which case NULL
is returned.

# EXAMPLES

```c
const char *message = "Hello, World!";
char *encoded = protocol_base64_encode(message, strlen(message));
if (!encoded) {
    perror("malloc");
    return -1;
}

/* encoded = "SGVsbG8sIFdvcmxkIQ==" */
protocol_write_line(fd, "STDOUT %s", encoded);
free(encoded);
```

Round-trip with `protocol_base64_decode(3)`:

```c
char *enc = protocol_base64_encode(data, data_len);
size_t dec_len;
char *dec = protocol_base64_decode(enc, &dec_len);
/* dec_len == data_len, memcmp(data, dec, data_len) == 0 */
free(enc);
free(dec);
```

# SEE ALSO

`protocol_base64_decode(3)`, `protocol_write_line(3)`,
`json_frame_write_fmt(3)`

# NOTES

- The encoding uses the standard base64 alphabet as defined in RFC 4648.
  URL-safe base64 is not supported.

- The output is always padded with `=` characters to a multiple of 4.

- For empty input (`len == 0`), an empty string is returned (not NULL).

- The function processes input in 3-byte groups.  Incomplete final groups
  are padded appropriately.
