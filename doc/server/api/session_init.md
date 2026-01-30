# SESSION_INIT(3) — Initialize a Client Session

## NAME

session_init — initialize a client session structure

## SYNOPSIS

```c
#include "server.h"

int session_init(client_session_t *session, int client_fd);
```

## DESCRIPTION

Initializes a `client_session_t` structure for a new client connection.
Sets all fields to their default values:

- `fd` = **client_fd**
- `authenticated` = 0 (not authenticated)
- `pid` = current process PID
- `stdout_pipe[0..1]` = -1 (no pipes open)
- `stderr_pipe[0..1]` = -1 (no pipes open)

## PARAMETERS

**session**
:   Pointer to the session structure to initialize.  Typically stack-allocated
    in `handle_client()`.

**client_fd**
:   Connected client socket file descriptor.

## RETURN VALUE

Always returns **0** (success).

## SOURCE

`server_session.c:46`

## SEE ALSO

[session_handle.md](session_handle.md), [session_cleanup.md](session_cleanup.md),
[server_accept_client.md](server_accept_client.md)
