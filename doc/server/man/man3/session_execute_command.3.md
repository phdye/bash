# session_execute_command(3) -- Execute a bash command with output capture

# SYNOPSIS

    #include "server.h"

    int session_execute_command(client_session_t *session, const char *command);

# DESCRIPTION

Executes a bash command string using the embedded interpreter and returns
the exit code.  This is the low-level execution interface used for internal
purposes where protocol-level output framing is not needed.

The function verifies that the session is authenticated, duplicates the
command string (since `parse_and_execute()` may free it), and calls
`parse_and_execute()` with the flags `SEVAL_NONINT | SEVAL_NOHIST` to
execute in non-interactive mode without history recording.

Unlike the EVAL protocol command path (which goes through
`capture_output()`), this function does **not** redirect stdout/stderr
to temporary files, does **not** base64-encode output, and does **not**
send protocol response lines.  Output goes to whatever stdout/stderr are
currently connected to.

# PARAMETERS

- **session** -- Pointer to an initialized and authenticated
  `client_session_t`.  If `session->authenticated` is `0`, the function
  returns immediately with `-1`.

- **command** -- Null-terminated bash command string to execute.  The
  string is duplicated internally before execution, so the caller's copy
  is not modified.

# RETURN VALUE

Returns the exit code from `parse_and_execute()` on success.  This
corresponds to `$?` in bash -- `0` for success, non-zero for failure.

Returns `-1` if:
- The session is not authenticated.
- Memory allocation for the command copy failed (`strdup()` returned NULL).

# ERRORS

- **ENOMEM** -- `strdup()` failed to allocate memory for the command copy
  (returns `-1`).

# SEE ALSO

`session_handle`(3), `init_bash_for_session`(3), `parse_and_execute`(3)

# NOTES

The `parse_and_execute()` function is part of the core bash interpreter
(defined in `evalstring.c`).  It parses the command string into an AST
and executes it.  The function may free the passed string, which is why
`session_execute_command()` always passes a `strdup()` copy.

The execution flags used are:

- `SEVAL_NONINT` -- Non-interactive evaluation (no prompts, no job control
  notifications).
- `SEVAL_NOHIST` -- Do not add the command to the history list.

Shell state modifications (variable assignments, directory changes,
function definitions) persist in the session process after this call
returns.
