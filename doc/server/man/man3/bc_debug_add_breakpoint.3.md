# bc\_debug\_add\_breakpoint(3) — add a breakpoint to the debugger

# SYNOPSIS

```c
#include <bashclient.h>

int bc_debug_add_breakpoint(bc_client_t *c, const char *kind,
                            const char *pattern, int line,
                            const char *condition);
```

# DESCRIPTION

Adds a breakpoint to the debugger for the current session.  The
function sends an `add_breakpoint` request on `CHAN_DEBUG`
(channel 4) and returns the server-assigned breakpoint ID.

The debugger must be enabled via `bc_debug_enable()` before adding
breakpoints.

**Breakpoint kinds:**

| Kind | Match criterion | Required parameters |
|------|----------------|---------------------|
| `"command"` | Substring match against the command string | `pattern` (non-NULL) |
| `"line"` | Exact match on source line number | `line` (> 0) |
| `"function"` | Substring match against function name | `pattern` (non-NULL) |

When execution reaches a matching command, the server pauses
execution and sends a break-hit event on `CHAN_DEBUG`.  The client
receives this event via `bc_poll()` and the registered
`bc_break_hit_cb` callback.

**Conditional breakpoints:**

The `condition` parameter specifies a bash expression that is
evaluated when the breakpoint matches.  The breakpoint only fires
if the expression evaluates to true (exit status 0).  Pass NULL
for unconditional breakpoints.

# PARAMETERS

- **c** — Pointer to an authenticated `bc_client_t` connection.
  Must not be NULL.

- **kind** — Breakpoint type string.  One of:
  - `"command"`: Match by command string substring.
  - `"line"`: Match by source line number.
  - `"function"`: Match by function name substring.
  Must not be NULL.

- **pattern** — Pattern string for `"command"` and `"function"`
  breakpoints.  Used for substring matching against the command
  text or function name.  Must not be NULL for these kinds.
  Ignored (may be NULL) for `"line"` breakpoints.

- **line** — Line number for `"line"` breakpoints.  Must be
  positive (> 0).  Ignored (use -1) for `"command"` and
  `"function"` breakpoints.

- **condition** — Optional conditional expression in bash syntax.
  The breakpoint fires only when this expression evaluates to
  true.  Pass NULL for unconditional breakpoints.

# RETURN VALUE

Returns the breakpoint ID (a positive integer, starting from 1)
on success.

Returns a negative error code on failure:

| Code | Meaning |
|------|---------|
| `BC_ERR_PARAM` (-7) | NULL client, NULL kind, invalid kind string, or missing required parameter |
| `BC_ERR_AUTH` (-1) | Client not authenticated |
| `BC_ERR_PROTOCOL` (-2) | Unexpected server response |
| `BC_ERR_TIMEOUT` (-3) | Server did not respond in time |
| `BC_ERR_TRANSPORT` (-4) | Connection error |
| `BC_ERR_SERVER` (-5) | Server rejected the breakpoint (e.g., debugger not enabled) |

# ERRORS

- Returns `BC_ERR_PARAM` if `kind` is not one of the recognized
  strings, or if a required parameter is missing (e.g., NULL
  `pattern` for a `"command"` breakpoint).
- Returns `BC_ERR_SERVER` if the debugger has not been enabled.

# EXAMPLES

# Command breakpoint

```c
#include <bashclient.h>
#include <stdio.h>

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");
    bc_debug_enable(c);

    /* Break on any command containing "rm" */
    int bp = bc_debug_add_breakpoint(c, "command", "rm", -1, NULL);
    if (bp < 0) {
        fprintf(stderr, "add_breakpoint failed: %d\n", bp);
    } else {
        printf("Breakpoint %d: command matching 'rm'\n", bp);
    }

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

# Line breakpoint with condition

```c
#include <bashclient.h>
#include <stdio.h>

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");
    bc_debug_enable(c);

    /* Break at line 25 only when $count > 10 */
    int bp = bc_debug_add_breakpoint(c, "line", NULL, 25,
                                     "[ $count -gt 10 ]");
    printf("Conditional breakpoint %d at line 25\n", bp);

    /* Break on function calls matching "cleanup" */
    int bp2 = bc_debug_add_breakpoint(c, "function", "cleanup",
                                      -1, NULL);
    printf("Function breakpoint %d for 'cleanup'\n", bp2);

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

# SEE ALSO

`bc_debug_remove_breakpoint`(3), `bc_debug_list_breakpoints`(3),
`bc_debug_enable`(3), `bc_debug_on_break_hit`(3),
`debug_add_breakpoint`(3)

# NOTES

- Breakpoint IDs are assigned by the server and are monotonically
  increasing within a session.  IDs are never reused, even after
  breakpoints are removed.
- The `"command"` and `"function"` kinds use substring matching
  (`strstr`), not glob or regex patterns.  The pattern `"rm"`
  matches `"rm -rf /tmp"`, `"echo rm"`, etc.
- The `condition` expression is evaluated in the bash interpreter
  context at the time the breakpoint is hit.  Shell variables,
  functions, and built-ins are available.
- Multiple breakpoints can be set on the same line or pattern.
  Each is tracked independently with its own hit count.
- The debugger must be enabled before adding breakpoints.  Calling
  this function with the debugger disabled returns
  `BC_ERR_SERVER`.
- Thread safety: the client library is not thread-safe.
