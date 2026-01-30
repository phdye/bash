# SESSION_HANDLE(3) — Main Session Command Loop

## NAME

session_handle — read and dispatch protocol commands for a client session

## SYNOPSIS

```c
#include "server.h"

int session_handle(client_session_t *session, server_config_t *config);
```

## DESCRIPTION

Enters the main command-processing loop for a client session.  Reads
protocol lines, parses them into command + argument, and dispatches
to the appropriate handler:

| Command | Handler | Auth required |
|---------|---------|---------------|
| `AUTH` | `handle_auth()` | No |
| `EVAL` | `handle_eval()` | Yes |
| `PING` | `handle_ping()` | No |
| `QUIT` | `handle_quit()` | No |
| (other) | Error response | No |

The loop continues until:
- `handle_quit()` returns 1 (QUIT command received), or
- `protocol_read_line()` returns -1 (EOF or read error).

## PARAMETERS

**session**
:   Initialized client session (from `session_init()`).

**config**
:   Server configuration, including the authentication token.

## RETURN VALUE

Always returns **0**.

## COMMAND DISPATCH

```
while (!done) {
    line = protocol_read_line(session->fd)
    if (line < 0) break              // EOF or error

    (cmd, arg) = protocol_parse_command(line)
    if (parse error) → ERR response

    switch (cmd):
        AUTH → handle_auth(session, arg, config)
        EVAL → handle_eval(session, arg)
        PING → handle_ping(session)
        QUIT → done = handle_quit(session)
        else → ERR unknown command
}
```

## SOURCE

`server_session.c:342`

## SEE ALSO

[session_init.md](session_init.md), [session_cleanup.md](session_cleanup.md),
[auth.md](auth.md), [eval.md](eval.md), [ping.md](ping.md), [quit.md](quit.md)
