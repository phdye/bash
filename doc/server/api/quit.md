# QUIT(3) — Graceful Session Disconnect

## NAME

QUIT — gracefully terminate the client session

## SYNOPSIS

```
QUIT
```

## DESCRIPTION

The **QUIT** command requests a graceful session termination.  The server
responds with **BYE** and then closes the connection, freeing all session
resources.

**Does not require authentication.**  QUIT is available in any session state.

## ARGUMENTS

None.

## RESPONSES

**`BYE`**
:   Acknowledgment.  The server will close the connection after sending this.

## BEHAVIOR

1. Server sends `BYE\n` to the client.
2. `handle_quit()` returns `1`, setting the `done` flag in the session loop.
3. `session_handle()` exits its read loop.
4. `session_cleanup()` closes the client file descriptor and any open pipes.
5. Control returns to the accept loop in `main()`.

## EXAMPLES

```
→ QUIT
← BYE
(connection closed)
```

Typical full session:
```
→ AUTH <token>
← OK
→ EVAL echo hello
← STDOUT aGVsbG8K
← STDERR
← EXIT 0
→ QUIT
← BYE
```

## NOTES

- If the client disconnects without sending QUIT (TCP RST, process killed),
  the server detects EOF on `protocol_read_line()` and performs the same
  cleanup.  QUIT is courteous but not required.
- After sending BYE, the server does not read further data from the client.

## SOURCE

- Handler: `handle_quit()` in `server_session.c:159`

## SEE ALSO

[auth.md](auth.md), [session_cleanup.md](session_cleanup.md)
