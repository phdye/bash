# pty\_handle\_spawn(3) — handle PTY spawn request from CHAN\_PTY

# SYNOPSIS

    #include "server.h"

    int pty_handle_spawn(int client_rfd, int client_wfd, const char *payload);

# DESCRIPTION

Handles a PTY spawn request received on `CHAN_PTY` (channel 5).
This function is called from `json_session_handle()` when a client
sends a `{"type":"spawn",...}` message.  It takes over the session
event loop for the duration of the PTY session.

**Sequence of operations:**

1. Parses spawn parameters from the JSON **payload**: `rows`, `cols`,
   `shell`, and `strip_ansi`.
2. Creates a pseudo-terminal via `forkpty()` and execs the requested
   shell (default: `/bin/bash`) as an interactive login shell in the
   child process.
3. Sends a `spawn_ok` response frame with the allocated rows, cols,
   and child PID.
4. Enters the relay loop (`pty_relay_loop()`), which uses `select()`
   to multiplex between client frames and PTY master fd I/O.
5. On exit (child terminates, client disconnects, or `close` message),
   sends an `exit` frame with the child's exit code.

**Relay loop message types (client to server):**

| Type | Action |
|------|--------|
| `input` | Base64-decoded data written to PTY master |
| `resize` | TIOCSWINSZ ioctl + SIGWINCH to child |
| `signal` | Signal delivered to child process |
| `close` | Terminates relay loop |

**Relay loop output (server to client):**

| Type | Content |
|------|---------|
| `output` | Base64-encoded PTY output bytes |
| `exit` | Child exit code |

If `strip_ansi` is true (string `"true"` or integer `1`), ANSI
escape sequences are removed from PTY output before encoding,
using the stateful `ansi_strip()` filter.

# PARAMETERS

- **client_rfd** — File descriptor for reading frames from the client.

- **client_wfd** — File descriptor for writing frames to the client.

- **payload** — JSON string containing spawn parameters:
  - `"rows"` (int, default 24): Initial terminal rows.
  - `"cols"` (int, default 80): Initial terminal columns.
  - `"shell"` (string, default `"/bin/bash"`): Shell to execute.
  - `"strip_ansi"` (bool/string, default false): Enable ANSI stripping.

# RETURN VALUE

Returns the child process exit code (0-255), or 128+N if the child
was killed by signal N.  Returns -1 if the PTY could not be spawned.

# SEE ALSO

`pty_parse_signal`(3), `ansi_strip`(3), `json_frame_read`(3),
`json_frame_write_fmt`(3)

# NOTES

- The relay loop handles `CHAN_CONTROL` messages (ping, disconnect)
  even while in PTY mode.  All other channels are ignored.
- The child shell has `TERM=xterm-256color` set if `$TERM` is not
  already defined.
- On child exit, remaining PTY output is drained and sent to the
  client before the `exit` frame.
- The child is waited on with `WNOHANG` polling.  If it does not
  exit within 5 seconds after the PTY master closes, it is killed
  with `SIGKILL`.
