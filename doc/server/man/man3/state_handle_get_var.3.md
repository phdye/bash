# state\_handle\_get\_var(3) — handle GET-VAR state command

# SYNOPSIS

    #include "server.h"

    int state_handle_get_var(int wfd, const char *arg);

# DESCRIPTION

Handles the `GET-VAR` protocol command.  Looks up a shell variable
by name using `find_variable()`, then sends the variable's value
(base64-encoded) and attributes back to the client via the v1
line-oriented protocol.

**Response format:**

    VALUE <name> <base64-value> [<attrs>]

Where `<attrs>` is a comma-separated list of attribute names (e.g.,
`exported,readonly,integer`).  The attrs field is omitted if the
variable has no special attributes.

If the variable is not found:

    ERR variable not found: <name>

If the argument is empty:

    ERR variable name required

# PARAMETERS

- **wfd** — File descriptor for writing the response via
  `protocol_write_line()`.

- **arg** — Variable name to look up.  Must not be NULL or empty.

# RETURN VALUE

Returns 0 in all cases (errors are reported as protocol responses,
not as return values).

# SEE ALSO

`state_operations`(3), `protocol_write_line`(3),
`protocol_base64_encode`(3)

# NOTES

- Uses the v1 line-oriented protocol (`protocol_write_line`), not
  v2 JSON frames.
- The variable value is taken from `var->value`.  If the value is
  NULL (variable exists but has no value), an empty string is
  encoded.
- Supported attribute flags: `exported`, `readonly`, `integer`,
  `local`, `array`, `assoc`, `nameref`, `uppercase`, `lowercase`,
  `capcase`, `trace`.
