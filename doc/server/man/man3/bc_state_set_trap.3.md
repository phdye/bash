# bc\_state\_set\_trap(3) — set a signal trap on the server

# SYNOPSIS

```c
#include "bashclient.h"

int bc_state_set_trap(bc_client_t *c, const char *signal,
                      const char *command);
```

# DESCRIPTION

Sets a signal trap on the bash-server session associated with the
client connection **c**.  When the specified signal is received (or
the specified pseudo-signal event occurs), the server session
executes the given command string.

The function sends a `set` request on the STATE channel
(`CHAN_STATE`, channel 2) with namespace `"trap"`, the signal
name, and the trap command.  The server performs the equivalent
of `trap 'command' signal` in the shell session.

If a trap is already set for the given signal, it is replaced.

# Supported signal names

The **signal** parameter accepts the following values:

| Signal | Description |
|--------|-------------|
| `"SIGINT"` | Interrupt (Ctrl-C). |
| `"SIGTERM"` | Termination signal. |
| `"SIGHUP"` | Hangup (terminal closed). |
| `"SIGQUIT"` | Quit signal. |
| `"SIGALRM"` | Alarm timer expired. |
| `"SIGUSR1"` | User-defined signal 1. |
| `"SIGUSR2"` | User-defined signal 2. |
| `"SIGPIPE"` | Broken pipe. |
| `"SIGWINCH"` | Window size changed. |
| `"EXIT"` | Pseudo-signal: executed when the session exits. |
| `"ERR"` | Pseudo-signal: executed when a command returns non-zero. |
| `"DEBUG"` | Pseudo-signal: executed before each simple command. |
| `"RETURN"` | Pseudo-signal: executed when a function or sourced script returns. |

Signal names may be given with or without the `SIG` prefix (e.g.,
both `"SIGINT"` and `"INT"` are accepted).  Names are
case-insensitive.

# PARAMETERS

| Parameter | Type | Description |
|-----------|------|-------------|
| `c` | `bc_client_t *` | Client connection handle. Must not be NULL. |
| `signal` | `const char *` | Signal name (e.g., `"SIGINT"`, `"EXIT"`). Must not be NULL or empty. |
| `command` | `const char *` | Command string to execute when the signal is received. Must not be NULL. May be an empty string (which resets the signal to its default disposition). |

# RETURN VALUE

Returns `BC_OK` (0) on success.

On failure, returns one of the following error codes:

| Code | Value | Meaning |
|------|-------|---------|
| `BC_ERR_PARAM` | -7 | A required parameter is NULL or invalid. |
| `BC_ERR_PROTOCOL` | -2 | The server response could not be parsed. |
| `BC_ERR_TIMEOUT` | -3 | The server did not respond within the configured timeout. |
| `BC_ERR_TRANSPORT` | -4 | The underlying connection failed. |
| `BC_ERR_SERVER` | -5 | The server rejected the operation (e.g., invalid signal name). |
| `BC_ERR_NOMEM` | -6 | Memory allocation failed. |

# ERRORS

- If **c** is NULL, returns `BC_ERR_PARAM`.
- If **signal** is NULL or empty, returns `BC_ERR_PARAM`.
- If **command** is NULL, returns `BC_ERR_PARAM`.
- If the signal name is not recognized by the server, returns
  `BC_ERR_SERVER`.
- If the connection is not authenticated, returns
  `BC_ERR_PROTOCOL`.

# EXAMPLES

# Set an EXIT trap

```c
int rc = bc_state_set_trap(client, "EXIT",
                           "echo 'session ending'");
if (rc != BC_OK)
    fprintf(stderr, "set_trap failed: %d\n", rc);
```

# Set a SIGINT trap

```c
int rc = bc_state_set_trap(client, "SIGINT",
                           "echo 'interrupted'; exit 130");
if (rc != BC_OK)
    fprintf(stderr, "set_trap failed: %d\n", rc);
```

# Set an ERR trap for error logging

```c
int rc = bc_state_set_trap(client, "ERR",
    "echo \"Error in command: $BASH_COMMAND\" >&2");
if (rc != BC_OK)
    fprintf(stderr, "set_trap failed: %d\n", rc);
```

# Replace an existing trap

```c
/* Set initial trap */
bc_state_set_trap(client, "EXIT", "echo 'goodbye'");

/* Replace with a different command */
int rc = bc_state_set_trap(client, "EXIT",
                           "rm -f /tmp/lockfile; echo 'done'");
assert(rc == BC_OK);
```

# Set a DEBUG trap for tracing

```c
int rc = bc_state_set_trap(client, "DEBUG",
    "echo \"+ $BASH_COMMAND\" >&2");
if (rc != BC_OK)
    fprintf(stderr, "set_trap failed: %d\n", rc);
```

# SEE ALSO

`bc_state_unset_trap`(3), `bc_state_inspect`(3),
`bc_state_set_var`(3), `bc_state_set_alias`(3),
`bc_connect`(3), `bash-server-client-c`(7),
`bash-server-channels`(7)

# NOTES

- Setting a trap with an empty command string resets the signal
  to its default disposition (equivalent to `trap '' SIGNAL` in
  bash, which ignores the signal; use `bc_state_unset_trap()` to
  fully reset to the default handler).
- The DEBUG and RETURN pseudo-signal traps are inherited by
  shell functions only if the function has the `trace` attribute
  or the `functrace` shell option is set.
- The ERR trap is not inherited by shell functions by default.
  Set the `errtrace` shell option (`set -E`) for inheritance.
- Trap commands execute in the context of the session's shell
  environment.  They have access to all variables, functions,
  and state of the session.
- The function uses the v2 binary protocol internally.  The STATE
  channel request includes `"op": "set"`, `"ns": "trap"`,
  `"name"` (signal name), and `"value"` (command) fields.
- A single `bc_client_t` must not be used concurrently from
  multiple threads.
