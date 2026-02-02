/* server_winpipe.c -- Windows Named Pipes transport for bash-server */

/* Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of GNU Bash, the Bourne Again SHell.

   Bash is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Bash is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Bash.  If not, see <http://www.gnu.org/licenses/>.
*/

/* Windows Named Pipes bypass Cygwin's AF_UNIX-over-TCP-loopback emulation,
   eliminating the SO_PEERCRED handshake race condition.  The pipe is created
   with an owner-only DACL (equivalent to chmod 0600) and uses byte-mode
   I/O compatible with the existing line-oriented protocol.

   After ConnectNamedPipe(), the Win32 HANDLE is converted to a POSIX fd
   via cygwin_attach_handle_to_fd(), so existing protocol_read_line() /
   protocol_write_line() code works unchanged. */

#ifdef __CYGWIN__

#include "server.h"

#include <sys/cygwin.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Win32 API — Cygwin provides these via w32api */
#include <windows.h>
#include <sddl.h>      /* ConvertStringSecurityDescriptorToSecurityDescriptor */
#include <aclapi.h>

/* Build the pipe path: \\.\pipe\bash-server-<name> */
static int
winpipe_build_path(const char *name, wchar_t *buf, size_t buflen)
{
    int n;

    if (!name || !*name) {
        fprintf(stderr, "bash-server: named pipe name is empty\n");
        return -1;
    }

    n = swprintf(buf, buflen, L"\\\\.\\pipe\\bash-server-%hs", name);
    if (n < 0 || (size_t)n >= buflen) {
        fprintf(stderr, "bash-server: pipe name too long\n");
        return -1;
    }

    return 0;
}

/* Create an owner-only security descriptor for the pipe.
   Uses SDDL string: "D:(A;;GA;;;OW)" — grant Generic All to Owner.
   Caller must LocalFree() the returned SECURITY_DESCRIPTOR. */
static PSECURITY_DESCRIPTOR
winpipe_create_owner_dacl(void)
{
    PSECURITY_DESCRIPTOR sd = NULL;

    /* D: = DACL
       (A;;GA;;;OW) = Allow, Generic All, to Owner */
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorA(
            "D:(A;;GA;;;OW)", SDDL_REVISION_1, &sd, NULL)) {
        fprintf(stderr, "bash-server: failed to create security descriptor "
                "(error %lu)\n", (unsigned long)GetLastError());
        return NULL;
    }

    return sd;
}

/* Create a new named pipe instance.  Returns a Win32 HANDLE.
   Each call creates a fresh instance; multiple instances of the same
   pipe name can exist concurrently for parallel client connections.

   The pipe is created WITHOUT FILE_FLAG_OVERLAPPED so that after
   cygwin_attach_handle_to_fd(), POSIX read()/write() work normally.
   Cancellation of ConnectNamedPipe is handled by a helper thread
   in server_winpipe_accept(). */
HANDLE
server_winpipe_create(const char *name)
{
    wchar_t pipe_path[256];
    SECURITY_ATTRIBUTES sa;
    PSECURITY_DESCRIPTOR sd;
    HANDLE h;

    if (winpipe_build_path(name, pipe_path, 256) < 0)
        return INVALID_HANDLE_VALUE;

    sd = winpipe_create_owner_dacl();
    if (!sd)
        return INVALID_HANDLE_VALUE;

    memset(&sa, 0, sizeof(sa));
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;

    h = CreateNamedPipeW(
        pipe_path,
        PIPE_ACCESS_DUPLEX,         /* No FILE_FLAG_OVERLAPPED */
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        PIPE_UNLIMITED_INSTANCES,
        SERVER_MAX_LINE,            /* output buffer size */
        SERVER_MAX_LINE,            /* input buffer size */
        0,                          /* default timeout */
        &sa
    );

    LocalFree(sd);

    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "bash-server: CreateNamedPipe failed (error %lu)\n",
                (unsigned long)GetLastError());
    }

    return h;
}

/* Helper thread context for ConnectNamedPipe.
   A helper thread runs blocking ConnectNamedPipe while the main thread
   polls a "connected" event and checks the running flag.  This avoids
   FILE_FLAG_OVERLAPPED on the pipe handle (which breaks Cygwin's POSIX
   read/write after cygwin_attach_handle_to_fd). */
typedef struct {
    HANDLE pipe_handle;
    HANDLE connected_event;     /* Signalled when ConnectNamedPipe returns */
    BOOL   success;
    DWORD  error;
} connect_ctx_t;

static DWORD WINAPI
connect_thread_func(LPVOID param)
{
    connect_ctx_t *ctx = (connect_ctx_t *)param;

    ctx->success = ConnectNamedPipe(ctx->pipe_handle, NULL);
    ctx->error = GetLastError();
    SetEvent(ctx->connected_event);
    return 0;
}

/* Wait for a client to connect to the pipe instance, then convert
   the Win32 HANDLE to a POSIX file descriptor.

   Uses a helper thread for the blocking ConnectNamedPipe call.  The
   main thread polls the event every 500ms and checks the running flag.
   If the flag becomes 0, CancelSynchronousIo cancels the blocking call
   and -1 is returned with errno=EINTR.

   Returns the POSIX fd on success, -1 on failure.
   On success the caller owns the fd (close() releases the HANDLE). */
int
server_winpipe_accept(HANDLE pipe_handle, volatile sig_atomic_t *running)
{
    connect_ctx_t ctx;
    HANDLE thread;
    DWORD result;
    int fd;

    if (pipe_handle == INVALID_HANDLE_VALUE)
        return -1;

    /* Set up context for helper thread */
    ctx.pipe_handle = pipe_handle;
    ctx.connected_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    ctx.success = FALSE;
    ctx.error = 0;

    if (!ctx.connected_event) {
        fprintf(stderr, "bash-server: CreateEvent failed (error %lu)\n",
                (unsigned long)GetLastError());
        return -1;
    }

    /* Launch helper thread to do the blocking ConnectNamedPipe */
    thread = CreateThread(NULL, 0, connect_thread_func, &ctx, 0, NULL);
    if (!thread) {
        fprintf(stderr, "bash-server: CreateThread failed (error %lu)\n",
                (unsigned long)GetLastError());
        CloseHandle(ctx.connected_event);
        return -1;
    }

    /* Poll: wait 500ms at a time, check running flag between waits */
    for (;;) {
        result = WaitForSingleObject(ctx.connected_event, 500);
        if (result == WAIT_OBJECT_0) {
            /* ConnectNamedPipe returned (client connected or error) */
            break;
        }
        if (result == WAIT_TIMEOUT) {
            if (running && !*running) {
                /* Server shutting down — cancel the blocking ConnectNamedPipe
                   on the helper thread via CancelSynchronousIo. */
                CancelSynchronousIo(thread);
                WaitForSingleObject(thread, 5000);
                CloseHandle(thread);
                CloseHandle(ctx.connected_event);
                errno = EINTR;
                return -1;
            }
            continue;
        }
        /* Unexpected WaitForSingleObject error */
        fprintf(stderr, "bash-server: WaitForSingleObject failed (error %lu)\n",
                (unsigned long)GetLastError());
        CancelSynchronousIo(thread);
        WaitForSingleObject(thread, 5000);
        CloseHandle(thread);
        CloseHandle(ctx.connected_event);
        return -1;
    }

    /* Helper thread finished — clean up thread resources */
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    CloseHandle(ctx.connected_event);

    /* Check ConnectNamedPipe result */
    if (!ctx.success && ctx.error != ERROR_PIPE_CONNECTED) {
        fprintf(stderr, "bash-server: ConnectNamedPipe failed (error %lu)\n",
                (unsigned long)ctx.error);
        return -1;
    }

    /* Convert Win32 HANDLE to a Cygwin POSIX fd.
       The pipe was created WITHOUT FILE_FLAG_OVERLAPPED, so Cygwin's
       read()/write() will use synchronous ReadFile/WriteFile — no issues.
       Parameters: name (NULL ok), fd (-1 = auto-allocate),
       handle, binary mode (TRUE), access flags */
    fd = cygwin_attach_handle_to_fd(NULL, -1, pipe_handle,
                                     TRUE, GENERIC_READ | GENERIC_WRITE);
    if (fd < 0) {
        fprintf(stderr, "bash-server: cygwin_attach_handle_to_fd failed: %s\n",
                strerror(errno));
        DisconnectNamedPipe(pipe_handle);
        CloseHandle(pipe_handle);
        return -1;
    }

    return fd;
}

/* Resolve the token file path for named pipe mode.
   Uses $XDG_RUNTIME_DIR/bash-server/<name>.token
   or /tmp/bash-server-<uid>/<name>.token as fallback. */
int
server_winpipe_token_path(const char *name, char *buf, size_t bufsize)
{
    const char *runtime_dir;
    char dir[PATH_MAX];

    runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (runtime_dir && *runtime_dir) {
        snprintf(dir, sizeof(dir), "%s/bash-server", runtime_dir);
    } else {
        snprintf(dir, sizeof(dir), "/tmp/bash-server-%d", (int)getuid());
    }

    /* Ensure directory exists */
    {
        struct stat st;
        if (stat(dir, &st) < 0) {
            if (mkdir(dir, 0700) < 0 && errno != EEXIST) {
                fprintf(stderr, "bash-server: cannot create %s: %s\n",
                        dir, strerror(errno));
                return -1;
            }
        }
    }

    snprintf(buf, bufsize, "%s/%s.token", dir, name);
    return 0;
}

#endif /* __CYGWIN__ */
