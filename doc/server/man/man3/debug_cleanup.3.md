# debug\_cleanup(3) — unregister debug hooks and free all breakpoints

# SYNOPSIS

    #include "server.h"

    void debug_cleanup(void);

# DESCRIPTION

Tears down the debugger for the current session.  Performs the
following actions:

1. If the pre-command hook is registered, unregisters it via
   `unregister_pre_command_hook()`.
2. Walks the breakpoint linked list, freeing each breakpoint's
   `pattern`, `condition`, and the breakpoint node itself.
3. Resets `active` to 0 and `pending_cmd` to NULL.

This function is called when the client sends a `disable` debug
message, or during session teardown.

# PARAMETERS

None.

# RETURN VALUE

None.

# SEE ALSO

`debug_init`(3), `debug_add_breakpoint`(3),
`debug_remove_breakpoint`(3)

# NOTES

- Safe to call even if `debug_init()` was never called or if no
  breakpoints exist.
- Does **not** close the client file descriptors; the session
  handler owns those resources.
- After cleanup, `debug_handle_message()` will re-initialize the
  debug state on the next call.
