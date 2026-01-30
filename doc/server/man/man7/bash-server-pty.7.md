# BASH-SERVER-PTY(7) -- PTY mode and ANSI escape stripping

# DESCRIPTION

bash-server supports a PTY (pseudo-terminal) mode that spawns an
interactive Bash session and relays terminal I/O between the client
and the PTY over CHAN_PTY (channel 5).  This allows clients to drive
a full interactive shell, complete with job control, prompts, and
terminal-aware programs, through the server's protocol.

# CONCEPTS

# Spawning a PTY session

The client sends a `spawn` message on CHAN_PTY:

```json
{
  "type": "spawn",
  "rows": 24,
  "cols": 80,
  "shell": "/bin/bash",
  "strip_ansi": false
}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `rows` | integer | 24 | Terminal row count |
| `cols` | integer | 80 | Terminal column count |
| `shell` | string | (bash) | Path to shell to exec in PTY |
| `strip_ansi` | boolean | false | Enable ANSI escape sequence stripping |

The server calls `forkpty()` to create a pseudo-terminal and `exec()`
the specified shell in the child process.  The parent retains the PTY
master file descriptor for I/O relay.

# I/O relay loop

After spawning, the server enters a `select()`-based relay loop that
multiplexes between two sources:

1. **Client socket** -- Messages from the client (input data, resize,
   signal, close)
2. **PTY master fd** -- Output from the shell process

Data flow:

```
Client --> CHAN_PTY:input --> write() --> PTY master --> bash
bash --> PTY master --> read() --> CHAN_PTY:output --> Client
```

All data payloads (input and output) are base64-encoded for binary
safety, since terminal I/O can contain arbitrary bytes including NUL,
control characters, and escape sequences.

# Resize

The client sends a `resize` message with new `rows` and `cols` values.
The server applies the new dimensions using:

1. `TIOCSWINSZ` ioctl on the PTY master fd to update the terminal size
2. `SIGWINCH` sent to the child process group, notifying the shell and
   any foreground programs of the size change

# Signal injection

The client can inject signals into the PTY child process by sending a
`signal` message with a signal name string:

```json
{"type": "signal", "name": "SIGINT"}
```

The server parses the signal name via `pty_parse_signal()`, which
accepts standard signal names (SIGINT, SIGTERM, SIGKILL, SIGTSTP,
SIGCONT, SIGHUP, SIGQUIT, SIGUSR1, SIGUSR2, etc.) and sends the
signal to the child process.

# Session termination

A PTY session ends when:

- The client sends a `close` message
- The child process exits (detected by `read()` returning 0 or error
  on the PTY master fd)

On child exit, the server drains any remaining output from the PTY
master fd, sends it to the client, and then sends an `exit` message
with the child's exit code:

- Normal exit: `WEXITSTATUS(status)` (the exit code)
- Signal death: `128 + signal_number` (following shell convention)

# Control messages during PTY mode

Even while the PTY relay loop is active, the server continues to handle
control messages on other channels.  Specifically, `ping` and
`disconnect` messages on CHAN_CONTROL are processed during the relay
loop, allowing keepalive and graceful shutdown.

# ANSI escape stripping

When `strip_ansi` is set to true in the spawn message, the server
applies a stateful filter to all output read from the PTY master before
forwarding it to the client.  This removes terminal escape sequences,
producing clean text suitable for programmatic consumption.

The filter is implemented as a state machine (`ansi_strip_state_t`)
with the following states:

**NORMAL**: Default state.  Non-escape bytes pass through.  Transition
to ESC on seeing byte 0x1B, or to CSI on seeing byte 0x9B (8-bit CSI).

**ESC**: Entered after 0x1B.  Dispatches based on the next byte:
- `[` (0x5B) transitions to CSI
- `]` (0x5D) transitions to OSC
- `(`, `)`, `*`, `+` transitions to CHARSET (two-char escape)
- Any other byte completes a two-character escape sequence

**CSI** (Control Sequence Introducer): Entered after `ESC [` or 0x9B.
Consumes parameter bytes (0x30-0x3F), intermediate bytes (0x20-0x2F),
and the final byte (0x40-0x7E).  The entire sequence is discarded.

**OSC** (Operating System Command): Entered after `ESC ]`.  Consumes
all bytes until terminated by:
- BEL (0x07)
- ST (String Terminator): ESC followed by `\` (0x5C)

OSC sequences typically carry window titles, color definitions, and
other metadata.

**OSC_ESC**: Intermediate state within OSC when 0x1B is seen, waiting
for `\` to complete the ST terminator.

**CHARSET**: Entered after `ESC (`, `ESC )`, etc.  Consumes exactly
one more byte (the charset designator) and returns to NORMAL.

# ANSI stripping properties

- **In-place safe**: Output length is always less than or equal to input
  length, so the filter can operate in-place on a buffer.
- **Persistent across reads**: The state machine persists across
  `read()` boundaries, correctly handling escape sequences that are
  split across multiple reads.
- **Handles 8-bit CSI**: Recognizes the single-byte 0x9B as equivalent
  to the two-byte ESC [ sequence.

# SEE ALSO

**bash-server**(7),
**bash-server-channels**(7)

# AUTHORS

GNU Bash is Copyright (C) Free Software Foundation, Inc.
The bash-server extension was developed as part of the Cygwin Bash project.

# COPYRIGHT

This is free software; see the GNU General Public License v3 or later
for copying conditions.  There is NO warranty.
