# BASH-SERVER-TRANSPORTS(7) -- Transport layer overview

# DESCRIPTION

bash-server supports four transport mechanisms for client connections.
Each transport provides a bidirectional byte stream over which the v1
or v2 protocol operates.  The transport is selected at server startup
via command-line flags and cannot be changed at runtime.

# CONCEPTS

# Unix domain socket (default)

The default transport creates an `AF_UNIX` `SOCK_STREAM` socket, binds
it to a filesystem path, and listens for incoming connections.

**Socket path resolution** follows a priority chain:

1. CLI argument (`--socket PATH`)
2. `$BASH_SERVER_SOCKET` environment variable
3. `socket_path` directive in `~/.bash-serverrc`
4. `$XDG_RUNTIME_DIR/bash-server/sock`
5. `/tmp/bash-server-<uid>/sock`

**Permissions**: The socket file is created with `chmod 0600` (owner
read/write only).  The containing directory should also be restricted.

**Stale socket handling**: If a socket file already exists at the
resolved path, the server attempts to `unlink()` it before binding.

**Multi-client support**: The server runs an accept loop, forking a
child process for each accepted connection.  The `max_clients` field
exists in configuration but is not currently enforced.

**Cygwin implementation note**: Cygwin implements `AF_UNIX` sockets
over TCP loopback (`127.0.0.1`).  See **bash-server-auth**(7) for
details on the `--no-peercred` flag.

# stdio mode (--stdio)

When invoked with `--stdio`, the server uses file descriptors 0 (stdin)
and 1 (stdout) for a single client session.  There is no accept loop;
the server handles exactly one session and exits.

This mode is designed for parent processes that spawn `bash-server` as
a child and communicate over the child's stdin/stdout pipes.  It
eliminates the need for filesystem-based socket paths.

The server still generates an authentication token.  The token is
delivered via `--auth-fd` or stderr.

# fd mode (--fd N)

The server uses inherited file descriptor N for both reading and
writing.  This is typically a pre-created `socketpair()` from a parent
process.

When the `client_session_t` has `write_fd != -1`, separate file
descriptors are used for reading (`fd`) and writing (`write_fd`).
Otherwise, the single fd serves both directions.

Like stdio mode, fd mode handles exactly one session with no accept
loop.

# Named Pipes (--named-pipe NAME, Cygwin only)

Creates a Windows Named Pipe at `\\.\pipe\bash-server-NAME`.

**Security**: The pipe is created with an owner-only DACL (Discretionary
Access Control List), restricting access to the creating user.

**Accept mechanism**: `ConnectNamedPipe()` blocks waiting for a client.
Because Cygwin does not natively integrate Windows Named Pipe handles
with POSIX file descriptors, the server uses `cygwin_attach_handle_to_fd()`
to convert the Windows `HANDLE` to a POSIX fd after the client connects.

A helper thread is used for the blocking `ConnectNamedPipe()` call so
that signal handling (shutdown) remains responsive.  The `running`
flag (a `volatile sig_atomic_t` pointer) is checked to allow clean
shutdown.

**Token file**: The authentication token is written to
`$XDG_RUNTIME_DIR/bash-server/NAME.token` (or the equivalent fallback
path).

**Platform restriction**: Named Pipe transport is only available on
Cygwin builds (guarded by `#ifdef __CYGWIN__`).

# Auth-fd (--auth-fd N)

This is not a transport but a token delivery mechanism that works with
any transport.  Instead of writing the authentication token to stderr,
the server writes it to file descriptor N.

This is particularly useful with stdio and fd modes, where the parent
process can open a dedicated pipe for token delivery, keeping it
separate from the data stream.

When `auth_fd` is -1 (the default), the token is written to stderr.

# TRANSPORT SELECTION MATRIX

| Flag | Transport | Sessions | Accept loop | Socket path needed |
|------|-----------|----------|-------------|--------------------|
| (none) | Unix socket | Multiple | Yes | Yes |
| `--stdio` | stdin/stdout | Single | No | No |
| `--fd N` | Inherited fd | Single | No | No |
| `--named-pipe NAME` | Windows Named Pipe | Multiple | Yes | No (pipe name) |

# SEE ALSO

**bash-server**(7),
**bash-server-auth**(7),
**bash-server-channels**(7)

# AUTHORS

GNU Bash is Copyright (C) Free Software Foundation, Inc.
The bash-server extension was developed as part of the Cygwin Bash project.

# COPYRIGHT

This is free software; see the GNU General Public License v3 or later
for copying conditions.  There is NO warranty.
