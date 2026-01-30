# observe\_set\_level(3) — change observability level at runtime

# SYNOPSIS

    #include "server.h"

    int observe_set_level(int level);

# DESCRIPTION

Changes the observability level for the current session.  The level
controls which hooks are active and what events are emitted on
`CHAN_OBSERVE` (channel 3).

The requested level is clamped to the range
\[0, `OBSERVE_LEVEL_MAX`\].

**Level transitions:**

| From | To | Action |
|------|----|--------|
| 0 | 1 | Registers `pre_command_hook` and `post_command_hook` |
| 1 | 0 | Unregisters both hooks |
| 1 | 1 | No change (hooks remain registered) |

If hook registration fails (e.g., the hook table is full), the hooks
are not registered and the level remains unchanged.

# PARAMETERS

- **level** — Desired observability level.
  - `0` (`OBSERVE_LEVEL_OFF`): Disable command event hooks.
  - `1` (`OBSERVE_LEVEL_COMMAND`): Enable pre/post command events.
  - Values < 0 are clamped to 0.
  - Values > `OBSERVE_LEVEL_MAX` are clamped to `OBSERVE_LEVEL_MAX`.

# RETURN VALUE

Returns the level actually set after clamping.  This is always in
the range \[0, `OBSERVE_LEVEL_MAX`\].

# SEE ALSO

`observe_init`(3), `observe_cleanup`(3)

# NOTES

- Can be called at any time during a session, not just during
  initialization.  The v2 session handler calls this when the
  client sends an `observe` control message to change the level
  dynamically.
- Hook registration is idempotent; calling with the same level
  that is already active is a no-op.
