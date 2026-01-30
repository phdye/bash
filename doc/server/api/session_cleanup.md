# SESSION_CLEANUP(3) — Clean Up Session Resources

## NAME

session_cleanup — close all file descriptors associated with a client session

## SYNOPSIS

```c
#include "server.h"

void session_cleanup(client_session_t *session);
```

## DESCRIPTION

Closes the client socket file descriptor and any open pipe file descriptors
in the session structure.  Sets all closed descriptors to -1 to prevent
double-close.

## PARAMETERS

**session**
:   Client session to clean up.

## RETURN VALUE

None (void).

## RESOURCES FREED

| Field | Action |
|-------|--------|
| `session->fd` | `close()`, set to -1 |
| `session->stdout_pipe[0]` | `close()` if >= 0 |
| `session->stdout_pipe[1]` | `close()` if >= 0 |
| `session->stderr_pipe[0]` | `close()` if >= 0 |
| `session->stderr_pipe[1]` | `close()` if >= 0 |

## NOTES

- Called after `session_handle()` returns, regardless of how the session
  ended (QUIT, EOF, or error).
- The pipe fields in `client_session_t` are initialized to -1 by
  `session_init()` and are not currently used directly (per-EVAL pipes
  are created and cleaned up within `capture_output()`).

## SOURCE

`server_session.c:62`

## SEE ALSO

[session_init.md](session_init.md), [session_handle.md](session_handle.md)
