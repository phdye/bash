# server\_winpipe\_accept(3) — wait for Named Pipe client connection

# SYNOPSIS

    #include "server.h"

    #ifdef __CYGWIN__
    #include <windows.h>

    int server_winpipe_accept(HANDLE pipe_handle,
                              volatile sig_atomic_t *running);
    #endif

# DESCRIPTION

Waits for a client to connect to a Windows Named Pipe instance, then
converts the Win32 HANDLE to a POSIX file descriptor using
`cygwin_attach_handle_to_fd()`.

Because the pipe was created without `FILE_FLAG_OVERLAPPED` (to
maintain POSIX read/write compatibility), the blocking
`ConnectNamedPipe()` call is run in a **helper thread**.  The main
thread polls the completion event every 500 milliseconds and checks
the **running** flag between polls.

**Shutdown sequence:**

When `*running` becomes 0, the function cancels the blocking
`ConnectNamedPipe()` via `CancelSynchronousIo(thread)`, waits up
to 5 seconds for the helper thread to exit, cleans up resources,
and returns -1 with `errno` set to `EINTR`.

**HANDLE-to-fd conversion:**

On successful client connection, `cygwin_attach_handle_to_fd()` is
called with:
- Auto-allocated fd (`-1`)
- Binary mode (`TRUE`)
- `GENERIC_READ | GENERIC_WRITE` access

The resulting POSIX fd is fully compatible with `read()`, `write()`,
`close()`, and `protocol_read_line()` / `protocol_write_line()`.

# PARAMETERS

- **pipe_handle** — Win32 HANDLE returned by
  `server_winpipe_create()`.  Must not be `INVALID_HANDLE_VALUE`.

- **running** — Pointer to a volatile `sig_atomic_t` flag.  When
  the pointed-to value becomes 0, the accept is cancelled and the
  function returns -1.  May be NULL (no shutdown check; the function
  blocks indefinitely until a client connects or an error occurs).

# RETURN VALUE

Returns a POSIX file descriptor (non-negative) on success.  The
caller owns the fd and should `close()` it when done (which also
releases the underlying HANDLE).

Returns -1 on error:
- `errno = EINTR`: Server shutdown (running flag became 0).
- Other errno values: System errors from `CreateEvent`,
  `CreateThread`, `WaitForSingleObject`, `ConnectNamedPipe`,
  or `cygwin_attach_handle_to_fd`.

On failure after `ConnectNamedPipe` succeeds,
`DisconnectNamedPipe()` and `CloseHandle()` are called to clean up.

# SEE ALSO

`server_winpipe_create`(3), `server_winpipe_token_path`(3)

# NOTES

- **Cygwin only**: Compiled only when `__CYGWIN__` is defined.
- The helper thread pattern avoids `FILE_FLAG_OVERLAPPED` on the
  pipe HANDLE, which would break Cygwin's POSIX I/O layer after
  `cygwin_attach_handle_to_fd()`.
- The 500ms poll interval provides a balance between shutdown
  responsiveness and CPU usage.
- `ERROR_PIPE_CONNECTED` from `ConnectNamedPipe()` is treated as
  success (client connected before the call).
