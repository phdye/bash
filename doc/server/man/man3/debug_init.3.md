# debug\_init(3) — initialize debugger state for a session

# SYNOPSIS

    #include "server.h"

    void debug_init(int rfd, int wfd);

# DESCRIPTION

Initializes the per-session debugger state.  Zeroes the entire debug
state structure, then sets the client read and write file descriptors,
`step_mode` to `DBG_RUN`, and `next_bp_id` to 1.

This function does **not** activate debug mode or register the
pre-command hook.  The hook is registered lazily when the first
breakpoint is added (via `debug_add_breakpoint()`) or when the client
sends an explicit `enable` message (via `debug_handle_message()`).

# PARAMETERS

- **rfd** — File descriptor for reading frames from the client
  (used by `debug_wait_for_resume()` to receive step/continue
  commands while paused at a breakpoint).

- **wfd** — File descriptor for writing frames to the client
  (used to send `break_hit`, `breakpoints`, `ast`, and other
  debug event frames on `CHAN_DEBUG`).

# RETURN VALUE

None.

# SEE ALSO

`debug_cleanup`(3), `debug_add_breakpoint`(3),
`debug_handle_message`(3)

# NOTES

- Safe to call multiple times; each call resets all state including
  any existing breakpoints (though it does not free them — call
  `debug_cleanup()` first if breakpoints may exist).
- The debug state is process-global, which is safe because
  bash-server uses a fork-per-session model.
- Auto-initialization also occurs in `debug_handle_message()` on
  first call, so explicit calls to `debug_init()` are not strictly
  required for the normal message-driven flow.
