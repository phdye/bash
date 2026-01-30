# debug\_list\_breakpoints(3) — serialize all breakpoints to JSON

# SYNOPSIS

    #include "server.h"

    char *debug_list_breakpoints(void);

# DESCRIPTION

Serializes all breakpoints in the debugger's linked list to a JSON
array string.  Each breakpoint is represented as a JSON object with
the following fields:

| Field | Type | Description |
|-------|------|-------------|
| `id` | int | Breakpoint ID |
| `type` | string | `"command"`, `"line"`, or `"function"` |
| `enabled` | bool | Whether the breakpoint is active |
| `hit_count` | int | Number of times this breakpoint has been hit |
| `pattern` | string | Match pattern (present if non-NULL) |
| `line` | int | Line number (present for LINE breakpoints) |
| `condition` | string | Condition expression (present if non-NULL) |

**Example output:**

    [{"id":1,"type":"command","enabled":true,"hit_count":3,"pattern":"echo"},
     {"id":2,"type":"line","enabled":true,"hit_count":0,"line":42}]

# PARAMETERS

None.

# RETURN VALUE

Returns a `malloc()`'d string containing the JSON array.  The caller
must `free()` the returned string.

Returns an empty array `"[]"` if no breakpoints exist.

The internal buffer is approximately 4 KB; breakpoint lists that
exceed this size are truncated.

# SEE ALSO

`debug_add_breakpoint`(3), `debug_remove_breakpoint`(3),
`debug_handle_message`(3)

# NOTES

- The serialization uses `snprintf()` into a fixed 4096-byte stack
  buffer, then `strdup()`'s the result.  Very long patterns or
  conditions may cause truncation.
- Breakpoints are listed in reverse insertion order (most recently
  added first), because new breakpoints are prepended to the linked
  list.
- Pattern strings are not JSON-escaped in the current implementation.
  Patterns containing double quotes or backslashes may produce
  malformed JSON.
