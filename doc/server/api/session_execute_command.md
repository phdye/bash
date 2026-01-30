# SESSION_EXECUTE_COMMAND(3) — Execute a Command (Internal)

## NAME

session_execute_command — execute a Bash command in-process without output capture

## SYNOPSIS

```c
#include "server.h"

int session_execute_command(client_session_t *session, const char *command);
```

## DESCRIPTION

Executes a Bash command string in the current process (no fork) using
`parse_and_execute()`.  The command's stdout and stderr go to the
server's own file descriptors — they are **not** captured or returned
to the client.

This function is provided for internal use cases where the server needs
to execute a command for its own purposes (e.g., initialization scripts,
internal state changes).

**Requires authentication.**  Returns -1 if the session is not authenticated.

## PARAMETERS

**session**
:   Client session (must be authenticated).

**command**
:   Bash command string to execute.

## RETURN VALUE

Returns the result of `parse_and_execute()`:
- **0** on success (parser completed without error).
- Non-zero on parse error.

Returns **-1** if:
- Session is not authenticated.
- `strdup()` fails (memory allocation error).

## NOTES

- The command string is `strdup()`'d because `parse_and_execute()` frees
  its first argument.
- Unlike `handle_eval()` / `capture_output()`, this function does **not**
  fork, does **not** capture output, and does **not** send protocol
  responses.  It is a raw execution primitive.
- Currently not invoked by any protocol command handler.  Available for
  future extensions.

## SOURCE

`server_session.c:380`

## SEE ALSO

[eval.md](eval.md), [session_handle.md](session_handle.md)
