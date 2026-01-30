# observe\_init(3) — initialize observability for a session

# SYNOPSIS

    #include "server.h"

    void observe_init(int fd, int level);

# DESCRIPTION

Initializes the per-session observability subsystem.  Sets the file
descriptor used for sending observe events to the client, resets the
event sequence counter to zero, and clears all internal state
(timing, hook registration).

If **level** is greater than 0, `observe_init()` calls
`observe_set_level()` to register the appropriate command hooks
immediately.

This function is called after authentication completes in the v2
session handler (`json_session_handle()`).  Because bash-server uses a
fork-per-session model, the module-level globals (`observe_fd`,
`observe_level`, `event_seq`, etc.) are safely per-process.

# PARAMETERS

- **fd** — File descriptor for writing observe frames to the client.
  Must be a valid writable fd (typically the session's write fd).
  Passed directly to `json_frame_write_fmt()` when emitting events.

- **level** — Desired observability level.  Valid values:
  - `0` (`OBSERVE_LEVEL_OFF`): No event hooks; output-only mode.
  - `1` (`OBSERVE_LEVEL_COMMAND`): Pre/post command events with
    command string, cwd, line number, subshell/async flags,
    exit status, signal number, and duration.

  Values above `OBSERVE_LEVEL_MAX` are clamped by
  `observe_set_level()`.

# RETURN VALUE

None.

# SEE ALSO

`observe_cleanup`(3), `observe_set_level`(3),
`json_frame_write_fmt`(3), `json_session_handle`(3)

# NOTES

- The observe fd is **not** closed by `observe_cleanup()`.  The
  caller (session handler) owns the fd lifetime.
- Event sequence numbers (`event_seq`) start at 0 for each session
  and increment monotonically across all event types.
- Calling `observe_init()` a second time within the same process
  resets all state, including unregistering any previously
  registered hooks (implicitly, by resetting `hooks_registered`
  to 0 before `observe_set_level()` re-registers them).
