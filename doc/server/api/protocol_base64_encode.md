# PROTOCOL_BASE64_ENCODE(3) — Base64 Encode Binary Data

## NAME

protocol_base64_encode — encode binary data as a Base64 string

## SYNOPSIS

```c
#include "server.h"

char *protocol_base64_encode(const char *data, size_t len);
```

## DESCRIPTION

Encodes **len** bytes of binary **data** into a Base64 string using the
standard alphabet (`A-Za-z0-9+/`) with `=` padding per RFC 4648.

The output is a single contiguous string with no line wrapping.

## PARAMETERS

**data**
:   Pointer to the binary data to encode.  May contain NUL bytes.

**len**
:   Number of bytes to encode.  If 0, returns an empty string.

## RETURN VALUE

Returns a heap-allocated, NUL-terminated string containing the Base64
encoding.  The caller must `free()` the returned pointer.

Returns **NULL** if `malloc()` fails.

## OUTPUT SIZE

The output length is `ceil(len / 3) * 4` bytes, plus one byte for the
NUL terminator.  For example:

| Input bytes | Output characters |
|-------------|-------------------|
| 0 | 0 |
| 1 | 4 (2 data + 2 padding) |
| 2 | 4 (3 data + 1 padding) |
| 3 | 4 |
| 13 ("Hello, World!") | 20 |
| 1,048,576 (1 MB) | 1,398,104 |

## EXAMPLES

```c
char *b64;

b64 = protocol_base64_encode("f", 1);       // "Zg=="
b64 = protocol_base64_encode("fo", 2);      // "Zm8="
b64 = protocol_base64_encode("foo", 3);     // "Zm9v"
b64 = protocol_base64_encode("Hello, World!", 13);  // "SGVsbG8sIFdvcmxkIQ=="

// Binary data with NUL bytes
char bin[] = {0x00, 0x01, 0x02};
b64 = protocol_base64_encode(bin, 3);       // "AAEC"
```

## SOURCE

`server_protocol.c:164`

## SEE ALSO

[protocol_base64_decode.md](protocol_base64_decode.md), [eval.md](eval.md)
