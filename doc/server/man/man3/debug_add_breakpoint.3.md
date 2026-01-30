# debug\_add\_breakpoint(3) — add a breakpoint to the debugger

# SYNOPSIS

    #include "server.h"

    #define DBG_BREAK_COMMAND   1
    #define DBG_BREAK_LINE      2
    #define DBG_BREAK_FUNC      3

    int debug_add_breakpoint(int type, const char *pattern,
                             int line, const char *condition);

# DESCRIPTION

Adds a new breakpoint to the debugger's linked list.  The breakpoint
is created with `enabled = 1` and `hit_count = 0`, assigned a unique
ID from the monotonically increasing `next_bp_id` counter, and
prepended to the breakpoint list.

If this is the first breakpoint (debug mode was not previously
active), `debug_add_breakpoint()` activates debug mode and registers
the `pre_command_hook` so that command execution will be intercepted.

**Breakpoint matching (in `debug_pre_hook`):**

| Type | Match criterion |
|------|----------------|
| `DBG_BREAK_COMMAND` (1) | `strstr(command_string, pattern)` |
| `DBG_BREAK_LINE` (2) | `line_number == bp->line` |
| `DBG_BREAK_FUNC` (3) | `strstr(command_string, pattern)` (currently same as COMMAND) |

# PARAMETERS

- **type** — Breakpoint type.  One of:
  - `DBG_BREAK_COMMAND` (1): Match by command string substring.
  - `DBG_BREAK_LINE` (2): Match by source line number.
  - `DBG_BREAK_FUNC` (3): Match by function name (substring match
    against command string in current implementation).

- **pattern** — Pattern string for COMMAND and FUNC breakpoints.
  Copied via `strdup()`.  May be NULL for LINE breakpoints.

- **line** — Line number for LINE breakpoints.  Ignored for other
  types (conventionally 0).

- **condition** — Optional conditional expression string.  Copied
  via `strdup()`.  May be NULL.  Reserved for future use (not
  currently evaluated).

# RETURN VALUE

Returns the breakpoint ID (a positive integer, starting from 1) on
success.

Returns -1 if memory allocation fails.

# SEE ALSO

`debug_remove_breakpoint`(3), `debug_list_breakpoints`(3),
`debug_handle_message`(3), `debug_cleanup`(3)

# NOTES

- Breakpoint IDs are never reused within a session.  The counter
  increments even if breakpoints are removed.
- The `condition` field is stored but not evaluated in the current
  implementation.  It is included in breakpoint listings and
  serialized for client-side evaluation.
- COMMAND and FUNC breakpoints use substring matching (`strstr`),
  not glob or regex.
