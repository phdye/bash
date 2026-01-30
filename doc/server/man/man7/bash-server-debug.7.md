# BASH-SERVER-DEBUG(7) -- Interactive debugger concepts

# DESCRIPTION

bash-server includes an interactive debugger that operates over
CHAN_DEBUG (channel 4) in the v2 protocol.  The debugger allows clients
to set breakpoints, control execution flow, and inspect the abstract
syntax tree (AST) of pending commands.  All debugger interaction occurs
within a single forked session process, so pausing execution is safe
and does not block the server or other sessions.

# CONCEPTS

# Breakpoint types

Three breakpoint types are supported, each matching commands in a
different way:

**Command breakpoints** (`DBG_BREAK_COMMAND`): Match against the
command string using pattern matching.  The pattern is compared to the
textual representation of each command before execution.

**Line breakpoints** (`DBG_BREAK_LINE`): Match against the source line
number.  Useful when evaluating scripts or multi-line command strings
where the line number is meaningful.

**Function breakpoints** (`DBG_BREAK_FUNC`): Match against the function
name when a shell function is invoked.

# Breakpoint properties

Each breakpoint has the following properties:

| Property | Description |
|----------|-------------|
| `id` | Unique integer identifier, assigned sequentially |
| `type` | `DBG_BREAK_COMMAND`, `DBG_BREAK_LINE`, or `DBG_BREAK_FUNC` |
| `enabled` | Boolean; disabled breakpoints are skipped during matching |
| `hit_count` | Number of times this breakpoint has been triggered |
| `pattern` | Match string for command and function breakpoints |
| `line` | Line number for line breakpoints |
| `condition` | Conditional expression (field exists but not yet evaluated) |

# Step modes

The debugger supports five execution modes, controlled by the client:

**run**: Normal execution.  No single-stepping; only breakpoints can
pause execution.

**step** (step into): Execute one command and pause.  If the command
contains nested commands (e.g., a pipeline or compound command), the
debugger pauses at the first inner command.

**next** (step over): Execute one command at the current depth and
pause.  Nested commands at deeper depths execute without pausing.  The
debugger records the current command nesting depth and only triggers
when execution returns to the same or shallower depth.

**finish** (step out): Continue execution until the current nesting
level completes.  The debugger pauses when execution returns one level
above the depth at which `finish` was issued.

**skip**: The client acknowledges the breakpoint but allows the command
to execute without further debugger interaction.  Note: due to the
hook-based architecture, the command still executes even when "skipped"
-- the limitation is that skip does not suppress execution, it only
resumes without pausing.

# Pre-command hook

The debugger registers a pre-command hook via the
`register_pre_command_hook()` infrastructure.  Before each command
executes, the hook:

1. Checks whether any enabled breakpoint matches the pending command
2. Checks whether the current step mode requires a pause
3. If either condition is true, sends a `break_hit` event to the client
   on CHAN_DEBUG and enters a blocking read loop waiting for a resume
   command

# break_hit event

When execution pauses, the server sends a `break_hit` message
containing:

- `line` -- Source line number of the paused command
- `command` -- String representation of the command
- `depth` -- Current command nesting depth
- `breakpoint_id` -- ID of the triggered breakpoint (if applicable)
- `reason` -- Why execution paused (`"breakpoint"`, `"step"`, `"next"`,
  `"finish"`)

# Client interaction while paused

While execution is paused at a `break_hit`, the client can send the
following messages on CHAN_DEBUG:

- **set_breakpoint**: Add a new breakpoint
- **delete_breakpoint**: Remove a breakpoint by ID
- **enable_breakpoint** / **disable_breakpoint**: Toggle breakpoint state
- **list_breakpoints**: Enumerate all breakpoints with their properties
  and hit counts
- **inspect_ast**: Serialize the pending COMMAND structure to JSON via
  `cmd_serialize()`; returns the full AST of the command about to execute
- **step** / **next** / **finish** / **continue**: Set the step mode
  and resume execution

# AST inspection

The `inspect_ast` command serializes the pending COMMAND tree to JSON
using `cmd_serialize()` from `cmd_serialize.c`.  This provides a
structured view of the parsed command, including:

- Command type (simple, pipeline, connection, group, for, while, if,
  case, function_def, etc.)
- Word lists (arguments, patterns)
- Redirections
- Connector types (`;`, `&&`, `||`, `|`)
- Nested command structures

The pending command is set via `debug_set_pending_cmd()` before the
pre-command hook fires.

# Conditional breakpoints

The breakpoint structure includes a `condition` field intended for
conditional expressions that must evaluate to true for the breakpoint
to fire.  This field exists in the data structure but condition
evaluation is not yet implemented; all breakpoints with a condition
currently fire unconditionally.

# Safety model

Because each client session runs in a forked child process, pausing
execution in the debugger blocks only that session.  The server's
accept loop and all other sessions continue operating normally.  There
is no risk of deadlocking the server by leaving a debugger session
paused.

# SEE ALSO

**bash-server**(7),
**bash-server-channels**(7),
**bash-server-observe**(7)

# AUTHORS

GNU Bash is Copyright (C) Free Software Foundation, Inc.
The bash-server extension was developed as part of the Cygwin Bash project.

# COPYRIGHT

This is free software; see the GNU General Public License v3 or later
for copying conditions.  There is NO warranty.
