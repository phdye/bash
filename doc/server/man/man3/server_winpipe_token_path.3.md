# server\_winpipe\_token\_path(3) — resolve token file path for Named Pipe mode

# SYNOPSIS

    #include "server.h"

    #ifdef __CYGWIN__
    int server_winpipe_token_path(const char *name, char *buf, size_t bufsize);
    #endif

# DESCRIPTION

Resolves the filesystem path for the authentication token file used
in Named Pipe transport mode.  The token file stores the randomly
generated authentication token that clients must present to
authenticate.

**Path resolution order:**

1. If `$XDG_RUNTIME_DIR` is set and non-empty:

       $XDG_RUNTIME_DIR/bash-server/<name>.token

2. Otherwise (fallback):

       /tmp/bash-server-<uid>/<name>.token

   Where `<uid>` is the numeric UID from `getuid()`.

The parent directory (`bash-server/` or `bash-server-<uid>/`) is
created with mode `0700` if it does not already exist.  The function
handles `EEXIST` gracefully (directory already exists).

# PARAMETERS

- **name** — Pipe name (same name passed to `server_winpipe_create()`).
  Used as the token filename stem.

- **buf** — Output buffer for the resolved path string.

- **bufsize** — Size of the output buffer in bytes.

# RETURN VALUE

Returns 0 on success (path written to **buf**).

Returns -1 on error:
- Directory creation failed (neither exists nor can be created).
  Error details are printed to stderr.

# SEE ALSO

`server_winpipe_create`(3), `server_winpipe_accept`(3)

# NOTES

- **Cygwin only**: Compiled only when `__CYGWIN__` is defined.
- The token file itself is not created by this function; it only
  resolves the path.  The server main loop writes the token to
  this path after generating it.
- The fallback path uses `/tmp/`, which on Cygwin maps to the
  Windows temp directory.  The `0700` permissions ensure only
  the owning user can access the token.
- `PATH_MAX` is used internally for the directory path buffer.
