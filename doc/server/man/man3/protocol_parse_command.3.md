# protocol_parse_command(3) -- Parse a v1 protocol line into command and argument

# SYNOPSIS

    #include "server.h"

    int protocol_parse_command(const char *line, char *cmd, char *arg, size_t argsize);

# DESCRIPTION

Parses a v1 protocol input line into a command verb and its argument.  The
first whitespace-delimited word is extracted, uppercased, and stored in
`cmd`.  The remainder of the line (after skipping whitespace) is copied to
`arg` with trailing whitespace trimmed.

The command word is limited to 31 characters and is always converted to
uppercase using `toupper(3)`.  Leading and trailing whitespace on the entire
line is handled gracefully.

This function is the primary dispatcher for the v1 text protocol.  For v2
JSON message parsing, see `json_get_string(3)`.

# PARAMETERS

| Parameter | Type           | Description                                       |
|-----------|----------------|---------------------------------------------------|
| `line`    | `const char *` | Input line to parse (NUL-terminated, no newline).  |
| `cmd`     | `char *`       | Output buffer for the command verb (uppercased).   |
| `arg`     | `char *`       | Output buffer for the argument portion.            |
| `argsize` | `size_t`       | Size of `arg` buffer in bytes.                     |

The `cmd` buffer should be at least 32 bytes (the command word is capped
at 31 characters plus NUL).  For `arg`, `SERVER_MAX_LINE` (8192) is
recommended.

# RETURN VALUE

Returns **0** on success.

Returns **-1** if the line is empty or contains only whitespace (no command
word found).

# ERRORS

Returns -1 only when the input line is empty after stripping leading
whitespace.  There is no other error condition.

# EXAMPLES

```c
char cmd[64], arg[SERVER_MAX_LINE];

/* Parse "AUTH abc123def456" */
protocol_parse_command("AUTH abc123def456", cmd, arg, sizeof(arg));
/* cmd = "AUTH", arg = "abc123def456" */

/* Parse "eval echo hello world" */
protocol_parse_command("eval echo hello world", cmd, arg, sizeof(arg));
/* cmd = "EVAL", arg = "echo hello world" */

/* Parse "PING" (no argument) */
protocol_parse_command("PING", cmd, arg, sizeof(arg));
/* cmd = "PING", arg = "" */

/* Dispatch */
if (strcmp(cmd, CMD_AUTH) == 0) {
    handle_auth(session, arg);
} else if (strcmp(cmd, CMD_EVAL) == 0) {
    handle_eval(session, arg);
}
```

# SEE ALSO

`protocol_read_line(3)`, `protocol_write_line(3)`,
`json_get_string(3)`, `session_handle(3)`

# NOTES

- The command verb is always uppercased, so `"auth"`, `"Auth"`, and
  `"AUTH"` all produce `"AUTH"` in `cmd`.

- The argument is copied verbatim (except for trailing whitespace trimming).
  No uppercasing or other transformation is applied to the argument.

- If the command word exceeds 31 characters, it is silently truncated.

- If the argument exceeds `argsize - 1` bytes, it is truncated by
  `strncpy(3)` and NUL-terminated.
