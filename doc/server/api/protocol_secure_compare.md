# PROTOCOL_SECURE_COMPARE(3) — Constant-Time String Comparison

## NAME

protocol_secure_compare — compare two strings in constant time to prevent timing attacks

## SYNOPSIS

```c
#include "server.h"

int protocol_secure_compare(const char *a, const char *b);
```

## DESCRIPTION

Compares two NUL-terminated strings for equality using a constant-time
algorithm.  This prevents timing side-channel attacks where an attacker
measures response time to deduce how many leading characters match.

The comparison first checks string lengths.  If lengths differ, returns 0
immediately (this leaks length information, which is acceptable since the
token length is fixed and publicly known).

If lengths match, XORs each byte pair and ORs the result into an
accumulator.  The comparison takes the same time regardless of where
the strings differ.

## PARAMETERS

**a**
:   First NUL-terminated string.

**b**
:   Second NUL-terminated string.

## RETURN VALUE

Returns **1** (true) if the strings are identical.

Returns **0** (false) if the strings differ in length or content.

## SECURITY PROPERTIES

- **Constant-time body:**  The XOR loop always processes all characters.
  No early exit on mismatch.
- **Accumulator pattern:**  Uses `result |= a[i] ^ b[i]` to avoid
  branch-dependent timing.
- **Length leak:**  The length comparison is not constant-time, but this
  is acceptable because the authentication token is always exactly 64
  characters.

## EXAMPLES

```c
protocol_secure_compare("hello", "hello");   // 1 (match)
protocol_secure_compare("hello", "world");   // 0 (differ)
protocol_secure_compare("hello", "hell");    // 0 (length mismatch)
protocol_secure_compare("", "");             // 1 (both empty)
protocol_secure_compare("", "x");            // 0 (length mismatch)
```

## NOTES

- Used exclusively by `handle_auth()` to compare the client-provided
  token against the server's generated token.
- The return value convention (1=match, 0=no match) is the opposite
  of `strcmp()` (0=match).  This follows the convention of security
  comparison functions (e.g., OpenSSL's `CRYPTO_memcmp` returns 0 for
  match, but many secure compare wrappers return boolean true for match).

## SOURCE

`server_protocol.c:250`

## SEE ALSO

[auth.md](auth.md), [security.md](../security.md)
