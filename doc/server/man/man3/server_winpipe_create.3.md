# server\_winpipe\_create(3) — create a Windows Named Pipe for bash-server

# SYNOPSIS

    #include "server.h"

    #ifdef __CYGWIN__
    #include <windows.h>

    HANDLE server_winpipe_create(const char *name);
    #endif

# DESCRIPTION

Creates a Windows Named Pipe instance for use as an alternative
transport to Unix domain sockets.  The pipe path is:

    \\.\pipe\bash-server-<name>

The pipe is created with the following properties:

- **Security**: Owner-only DACL via SDDL string `"D:(A;;GA;;;OW)"`,
  which grants Generic All access exclusively to the pipe owner.
  This is equivalent to Unix socket permissions `0600`.

- **Mode**: `PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT`
  (byte-mode, synchronous).

- **No overlapped I/O**: The pipe is created **without**
  `FILE_FLAG_OVERLAPPED`.  This is intentional: after
  `cygwin_attach_handle_to_fd()` converts the HANDLE to a POSIX fd,
  Cygwin's `read()`/`write()` use synchronous `ReadFile`/`WriteFile`,
  which is compatible with the existing protocol code.

- **Instances**: `PIPE_UNLIMITED_INSTANCES` allows multiple
  concurrent pipe instances for parallel client connections.

- **Buffer sizes**: `SERVER_MAX_LINE` (8192 bytes) for both input
  and output buffers.

Named Pipes bypass Cygwin's AF\_UNIX-over-TCP-loopback emulation,
eliminating the `SO_PEERCRED` handshake race condition that affects
Unix sockets on Cygwin.

# PARAMETERS

- **name** — Pipe name suffix.  Appended to `\\.\pipe\bash-server-`
  to form the full pipe path.  Must not be NULL or empty.

# RETURN VALUE

Returns a Win32 `HANDLE` to the pipe instance on success.

Returns `INVALID_HANDLE_VALUE` on error.  Error details are printed
to stderr.

# SEE ALSO

`server_winpipe_accept`(3), `server_winpipe_token_path`(3)

# NOTES

- **Cygwin only**: This function is compiled only when `__CYGWIN__`
  is defined.
- The `SECURITY_DESCRIPTOR` is allocated by
  `ConvertStringSecurityDescriptorToSecurityDescriptorA()` and freed
  with `LocalFree()` after pipe creation.
- The caller is responsible for calling `server_winpipe_accept()`
  to wait for client connections and convert the HANDLE to a POSIX fd.
- Each call creates a new pipe instance; the caller should create a
  new instance after each client disconnects.
