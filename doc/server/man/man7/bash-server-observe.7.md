# BASH-SERVER-OBSERVE(7) -- Observability system

# DESCRIPTION

bash-server provides an observability system that pushes execution
events to connected clients over CHAN_OBSERVE (channel 3) in the v2
protocol.  The system is opt-in: clients must explicitly enable it by
setting an observe level via the configure message on CHAN_CONTROL.

# CONCEPTS

# Observe levels

Observability is controlled by a per-session integer level:

**Level 0** (default): Observability is off.  The client receives only
explicit command responses (stdout, stderr, exit code) on CHAN_COMMAND.
No events are pushed on CHAN_OBSERVE.  This is the implicit baseline
that all sessions start with.

**Level 1**: Pre-command and post-command events are enabled.  Every
command that Bash executes generates a pair of events with metadata
about the execution context and results.

**Levels 2+**: Reserved for future use.  Potential expansions include
variable assignment events, parameter expansion tracing, and file
descriptor operation tracking.  The current maximum supported level is
defined by `OBSERVE_LEVEL_MAX` (1).

# Enabling observability

The client sends a `configure` message on CHAN_CONTROL:

```json
{"type":"configure","observe_level":1}
```

The server responds with acknowledgment.  The level can be changed at
any time during the session, including setting it back to 0 to disable
events.

# Pre-command event

Emitted before each command executes.  Contains:

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | `"pre_command"` |
| `command` | string | Textual representation of the command |
| `cwd` | string | Current working directory at time of execution |
| `line_number` | integer | Source line number |
| `is_subshell` | boolean | Whether the command runs in a subshell |
| `is_async` | boolean | Whether the command runs asynchronously (background) |
| `timestamp` | string | ISO 8601 timestamp |
| `seq` | integer | Monotonically increasing sequence number |

# Post-command event

Emitted after each command completes.  Contains:

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | `"post_command"` |
| `command` | string | Textual representation of the command |
| `exit_status` | integer | Exit code of the command |
| `signal_number` | integer | Signal that killed the command (0 if not signaled) |
| `duration_ms` | integer | Wall-clock execution time in milliseconds |
| `timestamp` | string | ISO 8601 timestamp |
| `seq` | integer | Monotonically increasing sequence number |

# Sequence numbers

Each event carries a `seq` field that increases monotonically within a
session.  This allows clients to:

- Detect dropped or reordered events
- Correlate pre/post pairs (they share sequential numbers)
- Order events relative to command responses on CHAN_COMMAND

# Hook registration

The observability system registers hooks using Bash's command execution
hook infrastructure:

- `register_pre_command_hook()` -- Called before each command executes
- `register_post_command_hook()` -- Called after each command completes

These are the same hook points used by the debugger.  Both systems
coexist: the observe hooks emit events while the debug hooks check
breakpoints and step conditions.

# Initialization and cleanup

`observe_init(fd, level)` initializes the observability state for a
session, setting the output file descriptor and initial observe level.
`observe_cleanup()` tears down the state.

`observe_set_level()` and `observe_get_level()` control the current
level at runtime.

# JSON escaping

The `json_escape_for_observe()` function escapes command strings for
safe embedding in JSON event payloads.  This handles control characters,
backslashes, quotes, and other characters that would break JSON syntax.

# Per-session isolation

Because each client session runs in a forked child process, observe
state (level, sequence counter, output fd) is per-session.  One
client's observe level does not affect other sessions.

# Relationship to the debugger

The observability system and debugger both use pre/post command hooks
but serve different purposes:

- **Observe**: Passive monitoring.  Events are pushed without pausing
  execution.  Useful for logging, profiling, and real-time monitoring.
- **Debug**: Active control.  Execution can be paused at breakpoints
  for interactive inspection and stepping.

Both can be active simultaneously in the same session.

# SEE ALSO

**bash-server**(7),
**bash-server-channels**(7),
**bash-server-debug**(7)

# AUTHORS

GNU Bash is Copyright (C) Free Software Foundation, Inc.
The bash-server extension was developed as part of the Cygwin Bash project.

# COPYRIGHT

This is free software; see the GNU General Public License v3 or later
for copying conditions.  There is NO warranty.
