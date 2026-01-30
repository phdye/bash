# PROTOCOL_BASE64_DECODE(3) — Base64 Decode a String

## NAME

protocol_base64_decode — decode a Base64 string to binary data

## SYNOPSIS

```c
#include "server.h"

char *protocol_base64_decode(const char *data, size_t *outlen);
```

## DESCRIPTION

Decodes a Base64-encoded string into binary data.  The input must use
the standard Base64 alphabet (`A-Za-z0-9+/`) with `=` padding.

The input length must be a multiple of 4.

## PARAMETERS

**data**
:   NUL-terminated Base64-encoded string.  Must be a multiple of 4
    characters in length.

**outlen**
:   Output parameter.  On success, set to the number of decoded bytes.
    On error, set to 0.

## RETURN VALUE

Returns a heap-allocated buffer containing the decoded binary data.
The buffer is NUL-terminated for convenience, but the true length
is in `*outlen` (the data may contain embedded NUL bytes).

The caller must `free()` the returned pointer.

Returns **NULL** on error:
- Input length is not a multiple of 4.
- Input contains invalid Base64 characters.
- `malloc()` failure.

On error, `*outlen` is set to 0.

## EXAMPLES

```c
char *decoded;
size_t len;

decoded = protocol_base64_decode("Zg==", &len);     // "f", len=1
decoded = protocol_base64_decode("Zm9v", &len);     // "foo", len=3
decoded = protocol_base64_decode("SGVsbG8sIFdvcmxkIQ==", &len);  // "Hello, World!", len=13

// Error cases
decoded = protocol_base64_decode("abc", &len);       // NULL, len=0 (not multiple of 4)
decoded = protocol_base64_decode("!!!!", &len);      // NULL, len=0 (invalid chars)
```

## NOTES

- The decode table is lazily initialized on first call
  (`base64_table_initialized` flag).
- Empty input (`""`) returns an allocated empty string with `*outlen = 0`.
- The NUL terminator is appended for convenience but is not counted in
  `*outlen`.

## SOURCE

`server_protocol.c:195`

## SEE ALSO

[protocol_base64_encode.md](protocol_base64_encode.md), [eval.md](eval.md)
