# state\_operations(3) — shell state manipulation functions

# SYNOPSIS

    #include "server.h"

    /* Variable operations */
    int state_handle_get_var(int wfd, const char *arg);
    int state_handle_set_var(int wfd, const char *arg);
    int state_handle_unset_var(int wfd, const char *arg);

    /* Function operations */
    int state_handle_get_func(int wfd, const char *arg);
    int state_handle_unset_func(int wfd, const char *arg);

    /* Alias operations */
    int state_handle_get_alias(int wfd, const char *arg);
    int state_handle_set_alias(int wfd, const char *arg);
    int state_handle_unset_alias(int wfd, const char *arg);

    /* Trap operations */
    int state_handle_set_trap(int wfd, const char *arg);
    int state_handle_unset_trap(int wfd, const char *arg);

    /* Inspection */
    int state_handle_inspect(int wfd, const char *arg);

# DESCRIPTION

These functions implement Phase 2 state operations for the bash-server
v1 protocol.  Each function handles one protocol command, performs the
requested operation on the bash interpreter state, and writes a
response back to the client via `protocol_write_line()`.

All functions share the same signature: `int func(int wfd, const char *arg)`.

# Variable Operations

**state\_handle\_get\_var** — Looks up a variable with `find_variable()`.
Returns `VALUE <name> <base64> [attrs]` or `ERR`.

**state\_handle\_set\_var** — Parses `<name> <value> [--export] [--readonly] [--integer]`
from **arg**.  Calls `bind_variable()` and optionally sets attribute flags.
Returns `OK` or `ERR`.

**state\_handle\_unset\_var** — Checks if the variable is readonly (returns
`ERR` if so), then calls `unbind_variable()`.  Returns `OK` or `ERR`.

# Function Operations

**state\_handle\_get\_func** — Looks up a function with `find_function()`.
Serializes the definition with `named_function_string()` and returns
`FUNC <name> <base64>` or `ERR`.

**state\_handle\_unset\_func** — Verifies function exists, then calls
`parse_and_execute("unset -f <name>")` to remove it.  Returns `OK` or `ERR`.

# Alias Operations

**state\_handle\_get\_alias** — Looks up alias value with `get_alias_value()`.
Returns `ALIAS <name> <base64>` or `ERR`.

**state\_handle\_set\_alias** — Parses `<name> <value>` from **arg**.
Calls `add_alias()`.  Returns `OK` or `ERR`.

**state\_handle\_unset\_alias** — Calls `remove_alias()`.
Returns `OK` or `ERR alias not found`.

# Trap Operations

**state\_handle\_set\_trap** — Parses `<signal> <command>` from **arg**.
Decodes signal name with `decode_signal()`, then calls `set_signal()`.
Returns `OK` or `ERR`.

**state\_handle\_unset\_trap** — Decodes signal name, calls
`restore_default_signal()`.  Returns `OK` or `ERR`.

# Inspection

**state\_handle\_inspect** — Parses `<target> [pattern]` from **arg**.
Enumerates shell state and sends multiple response lines followed
by `INSPECT-END`.

| Target | Response lines | Source |
|--------|---------------|--------|
| `vars` | `VALUE <name> <base64> [attrs]` | `all_visible_variables()` |
| `functions` | `FUNC <name> <base64>` | `all_shell_functions()` |
| `aliases` | `ALIAS <name> <base64>` | `all_aliases()` |
| `traps` | `TRAP <signal> <base64>` | `trap_list[]` iteration |

If **pattern** is provided, only entries whose name starts with
the pattern string are included (prefix match via `strncmp`).

# PARAMETERS

- **wfd** — File descriptor for writing protocol responses.

- **arg** — Command argument string.  Format varies by command:
  - GET-VAR, UNSET-VAR, GET-FUNC, UNSET-FUNC, GET-ALIAS,
    UNSET-ALIAS, UNSET-TRAP: single name
  - SET-VAR: `<name> <value> [--export] [--readonly] [--integer]`
  - SET-ALIAS: `<name> <value>`
  - SET-TRAP: `<signal> <command>`
  - INSPECT: `<target> [pattern]`

# RETURN VALUE

All functions return 0 in all cases.  Protocol errors are
communicated as `ERR` response lines, not as return values.

Returns -1 only on write failures (not currently implemented;
`protocol_write_line` errors are ignored).

# SEE ALSO

`state_handle_get_var`(3), `protocol_write_line`(3),
`protocol_base64_encode`(3), `session_handle`(3)

# NOTES

- All state operations use the v1 line-oriented protocol.  The v2
  JSON session handler dispatches state operations on `CHAN_STATE`
  (channel 2) but the underlying functions still use
  `protocol_write_line()`.
- Variable values are base64-encoded to safely transmit binary
  data and special characters over the line-oriented protocol.
- SET-VAR parses flags (`--export`, `--readonly`, `--integer`)
  that may appear after the value.  The value itself extends from
  after the name to the first flag or end of string.
- UNSET-VAR checks for readonly before unsetting and returns an
  error rather than allowing bash to print its own error message.
- UNSET-FUNC uses `parse_and_execute("unset -f name")` because
  there is no direct C API equivalent to the `unset -f` builtin.
- Signal names for traps are decoded case-insensitively via
  `decode_signal()` with `DSIG_NOCASE`.
