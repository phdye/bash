# debug\_remove\_breakpoint(3) — remove a breakpoint by ID

# SYNOPSIS

    #include "server.h"

    int debug_remove_breakpoint(int id);

# DESCRIPTION

Removes a breakpoint from the debugger's linked list by its numeric
ID.  The breakpoint's `pattern` and `condition` strings are freed,
and the breakpoint node itself is freed.

The linked list is traversed using a pointer-to-pointer technique
for efficient removal without special-casing the head node.

# PARAMETERS

- **id** — The breakpoint ID to remove.  This is the value returned
  by `debug_add_breakpoint()` when the breakpoint was created.

# RETURN VALUE

Returns 0 on success (breakpoint found and removed).

Returns -1 if no breakpoint with the given ID exists.

# SEE ALSO

`debug_add_breakpoint`(3), `debug_list_breakpoints`(3),
`debug_cleanup`(3)

# NOTES

- Removing all breakpoints does **not** automatically deactivate
  debug mode or unregister the hook.  The hook remains registered
  to support step-mode execution even without breakpoints.
  Use `debug_cleanup()` to fully deactivate.
- The breakpoint ID is not recycled; subsequent calls to
  `debug_add_breakpoint()` will use the next sequential ID.
