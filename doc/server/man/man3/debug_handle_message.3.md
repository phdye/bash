# debug\_handle\_message(3) — handle CHAN\_DEBUG messages from client

# SYNOPSIS

    #include "server.h"

    int debug_handle_message(int rfd, int wfd, const char *payload);

# DESCRIPTION

Dispatches debug control messages received on `CHAN_DEBUG` (channel 4).
Called from `json_session_handle()` whenever a frame arrives on
channel 4.

On the first call (if debug state is not yet initialized), this
function auto-initializes by calling `debug_init(rfd, wfd)`.  The
client file descriptors are updated on every call to handle potential
changes.

The message type is determined by the `"type"` field in the JSON
payload.  Supported message types:

| Type | Action | Response |
|------|--------|----------|
| `enable` | Activates debug mode, registers hook | `enable_ok` |
| `disable` | Calls `debug_cleanup()` | `disable_ok` |
| `break` | Adds breakpoint (parses `kind`, `pattern`, `line`, `condition`) | `break_ok` with ID |
| `delete` | Removes breakpoint by `id` | `delete_ok` with found status |
| `enable_bp` | Enables breakpoint by `id` | `enable_bp_ok` with found status |
| `disable_bp` | Disables breakpoint by `id` | `disable_bp_ok` with found status |
| `list` | Serializes all breakpoints | `breakpoints` with JSON array |
| `step` | Sets step mode to `DBG_STEP` | `step_ok` |
| `continue` | Sets step mode to `DBG_RUN` | `continue_ok` |
| `inspect_ast` | Serializes pending COMMAND to JSON | `ast` with data |
| `status` | Returns debug state summary | `status` with active, mode, count, depth |

# PARAMETERS

- **rfd** — File descriptor for reading frames from the client.

- **wfd** — File descriptor for writing frames to the client.

- **payload** — JSON string containing the debug message.  Must
  include a `"type"` field.  Additional fields depend on the
  message type.

# RETURN VALUE

Returns 0 on success.

Returns -1 on error (missing `type` field or unknown command type).
An error response frame is sent to the client before returning -1.

# SEE ALSO

`debug_init`(3), `debug_cleanup`(3), `debug_add_breakpoint`(3),
`debug_remove_breakpoint`(3), `debug_list_breakpoints`(3),
`cmd_serialize`(3)

# NOTES

- The `step` and `continue` messages set the step mode
  pre-emptively.  They take effect at the next `pre_command_hook`
  invocation, not at an existing breakpoint pause (for that, the
  client sends these types within the `debug_wait_for_resume()`
  loop during a `break_hit` pause).
- The `inspect_ast` message returns `null` if no command is
  currently pending (i.e., execution is not paused at a breakpoint).
- All responses are sent on `CHAN_DEBUG` (channel 4).
