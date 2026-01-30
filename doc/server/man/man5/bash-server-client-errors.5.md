# bash-server-client-errors(5) — client binding error code reference

# DESCRIPTION

All four **bash-server** client bindings (Python, TypeScript, C, Java)
implement a unified error hierarchy for reporting failures during
connection, authentication, command execution, and protocol handling.
This page documents the canonical error types, their meanings, and how
each language maps them.

The error hierarchy is designed so that callers can catch a broad base
class to handle any bash-server failure, or catch specific subclasses
to implement fine-grained recovery logic.

# FORMAT

# Error hierarchy

All client errors derive from a single base:

```
BashClientError
  ├── AuthError
  ├── ProtocolError
  ├── TimeoutError
  ├── TransportError
  └── ServerError
```

In the C binding, errors are integer return codes rather than exception
objects. Two additional C-only codes exist for memory allocation and
parameter validation failures.

# Cross-language mapping

| Error | Python | TypeScript | C | Java |
|-------|--------|------------|---|------|
| Base | `BashClientError` | `BashClientError` | `BC_ERR_GENERIC (-100)` | `BashClientException` |
| Auth | `AuthError` | `AuthError` | `BC_ERR_AUTH (-1)` | `AuthException` |
| Protocol | `ProtocolError` | `ProtocolError` | `BC_ERR_PROTOCOL (-2)` | `ProtocolException` |
| Timeout | `TimeoutError` | `TimeoutError` | `BC_ERR_TIMEOUT (-3)` | `TimeoutException` |
| Transport | `TransportError` | `TransportError` | `BC_ERR_TRANSPORT (-4)` | `TransportException` |
| Server | `ServerError` | `ServerError` | `BC_ERR_SERVER (-5)` | `ServerException` |
| Memory | N/A | N/A | `BC_ERR_NOMEM (-6)` | N/A |
| Parameter | N/A | N/A | `BC_ERR_PARAM (-7)` | N/A |

Python, TypeScript, and Java use exception classes that extend a
language-appropriate base (`Exception`, `Error`, `RuntimeException`
respectively). C uses negative integer return codes and a thread-local
error message string.

# When each error occurs

**AuthError** / `BC_ERR_AUTH`

Raised when authentication with the server fails. Causes include:

- The token provided does not match the server's token.
- The token file is missing or unreadable.
- The server rejected the `AUTH` (v1) or `auth` (v2) message.
- The `SO_PEERCRED` UID check failed (Unix socket transport).

**ProtocolError** / `BC_ERR_PROTOCOL`

Raised when the wire protocol is violated. Causes include:

- An invalid or unrecognized message type was received.
- A v2 frame header has an invalid channel number (outside 0-5).
- A v2 frame exceeds the 1 MB payload limit.
- JSON parsing failed on a received message.
- The server sent a response for a channel the client did not request.
- NDJSON framing received a line that is not valid JSON.

**TimeoutError** / `BC_ERR_TIMEOUT`

Raised when an operation exceeds its time limit. Causes include:

- The connection attempt timed out (TCP or Unix socket connect).
- The authentication handshake did not complete within the timeout.
- An `eval` command did not produce a `complete` response in time.
- A state or debug request received no response within the timeout.

**TransportError** / `BC_ERR_TRANSPORT`

Raised when the underlying transport fails. Causes include:

- The Unix domain socket path does not exist.
- The named pipe could not be opened (Windows Named Pipe transport).
- A `read()` or `write()` returned an unexpected error (e.g., `EPIPE`,
  `ECONNRESET`).
- The connection was closed unexpectedly by the server.
- File descriptor transport (`--fd`) received an invalid fd number.

**ServerError** / `BC_ERR_SERVER`

Raised when the server returns an explicit error response. Causes
include:

- The server sent an `error` message on any v2 channel.
- A v1 `ERROR` response was received.
- The server reported an internal failure (e.g., fork failed).

In Python, TypeScript, and Java, the `ServerError` exception carries
a **channel** attribute indicating which v2 channel originated the
error (see below).

**BC_ERR_NOMEM** (C only)

Returned when a memory allocation (`malloc`, `realloc`, `strdup`)
fails. This is C-specific because Python, TypeScript, and Java handle
memory allocation failures through their own runtime mechanisms
(`MemoryError`, heap exhaustion, `OutOfMemoryError`).

**BC_ERR_PARAM** (C only)

Returned when a function receives an invalid parameter. Examples:

- A `NULL` pointer where a non-null argument is required.
- A negative timeout value.
- A channel number outside the valid range.

This is C-specific because the other languages use their own parameter
validation (Python `TypeError`/`ValueError`, TypeScript compile-time
checks, Java `IllegalArgumentException`).

# ServerError channel attribute

In Python, TypeScript, and Java, `ServerError` (and its language
equivalents) includes a **channel** property identifying which v2
channel the error originated from:

| Channel | Value | Meaning |
|---------|-------|---------|
| CHAN_CONTROL | 0 | Control channel error (e.g., bad configure request) |
| CHAN_COMMAND | 1 | Command execution error (e.g., fork failed) |
| CHAN_STATE | 2 | State operation error (e.g., readonly variable) |
| CHAN_OBSERVE | 3 | Observation error (e.g., invalid level) |
| CHAN_DEBUG | 4 | Debug channel error (e.g., invalid breakpoint) |
| CHAN_PTY | 5 | PTY error (e.g., forkpty failed) |

For v1 protocol errors, **channel** is `None` / `null` / `-1` since
v1 has no channel concept.

# C error message retrieval

The C binding provides a function to retrieve a human-readable
description of the last error:

```c
const char *bc_error(bc_client_t *client);
```

This returns a pointer to a thread-local string buffer containing the
error message from the most recent failed operation. The string is
valid until the next API call on the same client handle. Returns
`"no error"` if the last operation succeeded.

Example:

```c
int rc = bc_eval(client, "exit 1", &result);
if (rc < 0) {
    fprintf(stderr, "eval failed: %s (code %d)\n",
            bc_error(client), rc);
}
```

# EXAMPLES

# Python

```python
from bashclient import BashClient, AuthError, TimeoutError, ServerError

try:
    client = BashClient.connect(timeout=5.0)
    result = client.eval("echo hello", timeout=10.0)
    print(result.stdout)
except AuthError as e:
    print(f"Authentication failed: {e}")
except TimeoutError as e:
    print(f"Operation timed out: {e}")
except ServerError as e:
    print(f"Server error on channel {e.channel}: {e}")
finally:
    client.close()
```

# TypeScript

```typescript
import { BashClient, AuthError, TimeoutError, ServerError } from '@bashclient/core';

try {
    const client = await BashClient.connect({ timeout: 5000 });
    const result = await client.eval('echo hello', { timeout: 10000 });
    console.log(result.stdout);
} catch (e) {
    if (e instanceof AuthError) {
        console.error(`Authentication failed: ${e.message}`);
    } else if (e instanceof TimeoutError) {
        console.error(`Operation timed out: ${e.message}`);
    } else if (e instanceof ServerError) {
        console.error(`Server error on channel ${e.channel}: ${e.message}`);
    }
} finally {
    await client.close();
}
```

# C

```c
#include <bashclient.h>
#include <stdio.h>

int main(void) {
    bc_client_t *client = NULL;
    bc_result_t result = {0};
    int rc;

    rc = bc_connect(&client, NULL, 5000);
    if (rc == BC_ERR_TRANSPORT) {
        fprintf(stderr, "Cannot connect: %s\n", bc_error(client));
        return 1;
    }
    if (rc == BC_ERR_AUTH) {
        fprintf(stderr, "Auth failed: %s\n", bc_error(client));
        bc_close(client);
        return 1;
    }

    rc = bc_eval(client, "echo hello", &result);
    if (rc == BC_ERR_TIMEOUT) {
        fprintf(stderr, "Eval timed out\n");
    } else if (rc == BC_ERR_SERVER) {
        fprintf(stderr, "Server error: %s\n", bc_error(client));
    } else if (rc == 0) {
        printf("%s", result.stdout);
    }

    bc_result_free(&result);
    bc_close(client);
    return 0;
}
```

# Java

```java
import com.bashserver.client.*;

public class Example {
    public static void main(String[] args) {
        try (BashClient client = BashClient.connect(
                BashClient.options().timeout(5000))) {
            EvalResult result = client.eval("echo hello",
                    EvalOptions.builder().timeout(10000).build());
            System.out.println(result.getStdout());
        } catch (AuthException e) {
            System.err.println("Auth failed: " + e.getMessage());
        } catch (TimeoutException e) {
            System.err.println("Timed out: " + e.getMessage());
        } catch (ServerException e) {
            System.err.printf("Server error on channel %d: %s%n",
                    e.getChannel(), e.getMessage());
        } catch (BashClientException e) {
            System.err.println("Client error: " + e.getMessage());
        }
    }
}
```

# NOTES

The C binding has two additional error codes (`BC_ERR_NOMEM` and
`BC_ERR_PARAM`) that do not appear in the other languages. These cover
failure modes that the managed-language runtimes handle through their
own exception hierarchies (Python `MemoryError` / `TypeError`,
TypeScript compile-time checks, Java `OutOfMemoryError` /
`IllegalArgumentException`).

All error classes in Python, TypeScript, and Java carry a **message**
attribute (accessed via `str(e)`, `e.message`, or `e.getMessage()`
respectively) with a human-readable description. The C equivalent is
the `bc_error()` function.

Error codes in C are guaranteed to be negative integers. Success is
always indicated by `0`. Positive return values are reserved for
future use as informational status codes.

When catching errors, prefer catching the most specific subclass
first. In Java, note that `BashClientException` extends
`RuntimeException` (unchecked) — it is not required to declare it in
`throws` clauses, but doing so is recommended for documentation
purposes.

# SEE ALSO

**bash-server-client-api**(7),
**bashclient-api-metadata**(5),
**bash-server-json-messages**(5),
**bash-server**(1),
**bashclient**(1)

# AUTHORS

GNU Bash is written by Brian Fox and Chet Ramey. The bash-server
extension and this documentation were developed as part of the
Cygwin bash-server project.

# COPYRIGHT

Copyright (C) 2024-2025 Free Software Foundation, Inc. License GPLv3+:
GNU GPL version 3 or later <https://www.gnu.org/licenses/gpl.html>.
This is free software; you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
