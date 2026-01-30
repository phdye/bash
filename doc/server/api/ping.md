# PING(3) — Health Check

## NAME

PING — test server connectivity and responsiveness

## SYNOPSIS

```
PING
```

## DESCRIPTION

The **PING** command tests that the server is alive and processing commands.
It returns **PONG** immediately with no side effects.

**Does not require authentication.**  PING is available in both authenticated
and unauthenticated session states.

## ARGUMENTS

None.  Any argument after PING is ignored (the command parser extracts
only the command keyword).

## RESPONSES

**`PONG`**
:   Server is alive and responsive.

## EXAMPLES

```
→ PING
← PONG
```

## USE CASES

- **Health monitoring:**  External monitoring systems can connect, send PING,
  verify PONG, and disconnect to confirm the server is running.
- **Connection keepalive:**  Long-lived clients can periodically send PING
  to detect stale connections.
- **Latency measurement:**  Round-trip time of PING/PONG measures socket
  and server processing latency.

## SOURCE

- Handler: `handle_ping()` in `server_session.c:150`

## SEE ALSO

[quit.md](quit.md), [auth.md](auth.md)
