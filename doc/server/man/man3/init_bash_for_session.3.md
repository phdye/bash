# init_bash_for_session(3) -- Initialize the embedded bash interpreter

# SYNOPSIS

    #include "server.h"

    void init_bash_for_session(server_config_t *config);

# DESCRIPTION

Initializes the embedded bash interpreter for a new session.  This
function is called once per session process, typically triggered by a
successful `AUTH` command.  Subsequent calls are no-ops (guarded by an
internal static flag).

The initialization sequence is:

1. Set `shell_name` to `"bash-server"`.
2. Set `interactive_shell` to `0` (non-interactive).
3. Set `login_shell` from `config->login_mode`.
4. Call bash initialization functions in order:
   - `initialize_shell_builtins()`
   - `initialize_traps()`
   - `initialize_signals(0)` (non-privileged mode)
   - `tilde_initialize()`
   - `initialize_shell_variables(shell_environment, 0)`
   - `initialize_job_control(0)`
   - `initialize_bash_input()`
   - `initialize_flags()`
   - `initialize_shell_options(0)`
   - `initialize_bashopts(0)`
5. Set `shell_initialized = 1`.
6. Source startup files based on configuration flags (see below).
7. Source `config->init_file` if specified.

# Startup File Sourcing

The startup file behavior depends on the configuration:

**Login mode** (`config->login_mode` set, `config->noprofile` not set):
- Sources `/etc/profile`.
- Then tries `~/.bash_profile`; if not found, tries `~/.bash_login`;
  if not found, tries `~/.profile`.

**Default non-login mode** (`config->norc` not set):
- Sources `/etc/bash.bashrc`.
- Sources `~/.bashrc`.

**`--norc` flag** (`config->norc` set):
- Skips `~/.bashrc` and `/etc/bash.bashrc`.

**`--noprofile` flag** (`config->noprofile` set):
- Skips `/etc/profile` and `~/.bash_profile` (login mode only).

**`--init` script** (`config->init_file` set):
- Always sourced after standard startup files, regardless of other flags.

Missing startup files are silently skipped.

# PARAMETERS

- **config** -- Pointer to the server configuration structure.  The
  following fields are consulted:

  | Field        | Effect                                          |
  |--------------|-------------------------------------------------|
  | `login_mode` | Enable login shell startup file sequence         |
  | `norc`       | Skip `~/.bashrc` sourcing                        |
  | `noprofile`  | Skip `/etc/profile` and `~/.bash_profile`        |
  | `init_file`  | Path to additional init script to source          |

# RETURN VALUE

None.

# ERRORS

Errors during startup file sourcing are handled internally by bash and
do not cause this function to fail.  Missing files are silently ignored.

# SEE ALSO

`session_handle`(3), `session_execute_command`(3), `shell_initialize`(3)

# NOTES

This function is idempotent -- calling it multiple times has no effect
after the first successful initialization.

The function links against the main bash library (`cygbash-5.1.dll`) to
access all initialization routines.  These are the same routines used by
the interactive `bash` shell during startup.

Startup files are sourced using bash's own `source_file()` function
(from `builtins/evalfile.c`), which properly sets up `return_catch` so
that `return` statements in sourced scripts work correctly.

The `$HOME` environment variable is used to locate user-specific startup
files.  If `$HOME` is not set, user startup files are skipped.
