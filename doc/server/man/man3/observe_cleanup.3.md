# observe\_cleanup(3) — unregister observability hooks and reset state

# SYNOPSIS

    #include "server.h"

    void observe_cleanup(void);

# DESCRIPTION

Tears down the observability subsystem for the current session.  If
pre/post command hooks are registered, they are unregistered via
`unregister_pre_command_hook()` and `unregister_post_command_hook()`.
The observe file descriptor is reset to -1 and the level is set to 0.

This function is called during session teardown to ensure no stale
hook callbacks remain.  It is safe to call even if `observe_init()`
was never called or if hooks were never registered.

# PARAMETERS

None.

# RETURN VALUE

None.

# SEE ALSO

`observe_init`(3), `observe_set_level`(3)

# NOTES

- Does **not** close the observe fd; the session handler owns that
  resource.
- After cleanup, the observe module will silently discard any
  hook invocations that may still be in flight (the `observe_fd < 0`
  guard in the hook callbacks handles this).
