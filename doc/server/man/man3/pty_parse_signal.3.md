# pty\_parse\_signal(3) — convert signal name string to signal number

# SYNOPSIS

    #include "server.h"

    int pty_parse_signal(const char *name);

# DESCRIPTION

Converts a signal name string to its numeric signal number.  Accepts
three input formats:

1. **Full name**: `"SIGINT"`, `"SIGTERM"`, `"SIGKILL"`, etc.
2. **Short name**: `"INT"`, `"TERM"`, `"KILL"`, etc. (the `SIG`
   prefix is stripped automatically).
3. **Numeric string**: `"2"`, `"9"`, `"15"`, etc. (converted via
   `atoi()`).

**Supported named signals:**

| Name | Signal |
|------|--------|
| HUP | SIGHUP |
| INT | SIGINT |
| QUIT | SIGQUIT |
| KILL | SIGKILL |
| TERM | SIGTERM |
| STOP | SIGSTOP |
| TSTP | SIGTSTP |
| CONT | SIGCONT |
| WINCH | SIGWINCH |
| USR1 | SIGUSR1 |
| USR2 | SIGUSR2 |
| PIPE | SIGPIPE |
| ALRM | SIGALRM |
| CHLD | SIGCHLD |

# PARAMETERS

- **name** — Signal name string.  Must not be NULL.  Comparison is
  case-sensitive (uppercase expected for named signals).

# RETURN VALUE

Returns the signal number (positive integer) on success.

Returns -1 if the signal name is not recognized.

# SEE ALSO

`pty_handle_spawn`(3), `signal`(7)

# NOTES

- Numeric strings are passed to `atoi()` without range validation.
  The caller should verify the result is a valid signal number for
  the platform.
- The `SIG` prefix is stripped by a simple `strncmp` + pointer
  advance, so `"SIG2"` would also be accepted (falling through
  to the numeric path after prefix stripping).
