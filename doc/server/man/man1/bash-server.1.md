# BASH-SERVER(1) -- persistent Bash evaluation daemon

# SYNOPSIS

    bash-server [OPTIONS]

# DESCRIPTION

**bash-server** is a persistent daemon that embeds the GNU Bash 5.1 interpreter
and exposes it over a Unix domain socket (or alternative transports) for
authenticated remote evaluation of shell commands.

The server links against **cygbash-5.1.dll**, providing a full Bash environment
with readline, history, job control, and all standard builtins.  Each connected
client session maintains independent shell state (variables, functions, working
directory).  Command execution is fork-per-command with pipe-captured stdout and
stderr.

On startup the server generates a cryptographically random 256-bit
authentication token, writes it to *\<socket-path\>.token* (mode 0600), and
begins accepting connections.  Clients must present this token via the AUTH
command before any other operations are permitted.

**bash-server** supports three wire protocol versions.  The protocol is
auto-detected from the first byte received on each connection:

- **v1** -- Line-oriented text protocol (LF-terminated).  Simple and
  human-readable.  Commands: AUTH, EVAL, PING, QUIT.
- **v2 (binary)** -- Length-prefixed JSON frames with a 6-byte binary header.
  Supports multiplexed channels.
- **v2 (NDJSON)** -- Newline-delimited JSON.  Same semantics as binary v2 but
  without the binary framing.

Auto-detection rule: if the first byte is `{` or `\n`, the connection uses
NDJSON framing; if the first byte has its high bit set or is in the range 0--5,
binary v2 framing is assumed; otherwise the connection uses v1 text protocol.

The v2 protocol defines six logical channels:

| Channel | ID | Purpose                        |
|---------|----|--------------------------------|
| Control | 0  | Session management, auth       |
| Command | 1  | Command execution (EVAL)       |
| State   | 2  | Variable/environment queries   |
| Observe | 3  | Event observation/subscription |
| Debug   | 4  | Debugger interface             |
| PTY     | 5  | Pseudo-terminal I/O            |

# OPTIONS

**-s**, **--socket** *PATH*
:   Set the Unix domain socket path explicitly.  See **SOCKET PATH RESOLUTION**
    below for the default behavior when this option is omitted.

**-d**, **--daemon**
:   Daemonize the process (double-fork, setsid, redirect stdio to /dev/null).

**-p**, **--pidfile** *PATH*
:   Write the server PID to *PATH* after startup.  The file is removed on clean
    shutdown.

**-m**, **--max-clients** *N*
:   Maximum number of simultaneous client connections.  Default: **10**.
    Minimum: 1.

**-P**, **--no-peercred**
:   Disable the `SO_PEERCRED` credential handshake on accepted connections.
    Required for compatibility with Python and other clients that do not
    support the Cygwin-specific peer credential exchange.

**-l**, **--login**
:   Initialize each session as a login shell.  Sources `/etc/profile`,
    `~/.bash_profile`, `~/.bash_login`, and `~/.profile` in the standard
    Bash login sequence.

**--norc**
:   Skip sourcing `~/.bashrc` during session initialization.

**--noprofile**
:   Skip sourcing `/etc/profile` and `~/.bash_profile` during login shell
    initialization.  Only meaningful when combined with **--login**.

**-I**, **--init** *SCRIPT*
:   Source the file *SCRIPT* as additional initialization for each new session,
    after the standard startup files.

**-S**, **--stdio**
:   Use stdin/stdout for client I/O instead of creating a Unix socket.
    Operates in single-session mode.  Useful for integration with process
    supervisors or SSH forced commands.

**-f**, **--fd** *N*
:   Use the inherited file descriptor *N* for client I/O instead of creating a
    Unix socket.  Operates in single-session mode.  Intended for use with
    socket activation (e.g., systemd).

**-A**, **--auth-fd** *N*
:   Write the authentication token to file descriptor *N* instead of stderr.
    Only applies in **--stdio** and **--fd** modes.  Default: stderr.

**-W**, **--named-pipe** *N*
:   Listen on the Windows Named Pipe `\\.\pipe\bash-server-`*N* instead of a
    Unix domain socket.  Cygwin only.  Allows native Windows processes to
    connect to the server.

**-v**, **--verbose**
:   Print diagnostic messages to stderr, including client connect/disconnect
    events and child process lifecycle.

**-h**, **--help**
:   Print a usage summary and exit.

**-V**, **--version**
:   Print version information and exit.

# SOCKET PATH RESOLUTION

When **--socket** is not specified, the socket path is determined by the first
matching rule:

1. **--socket** *PATH* on the command line.
2. The **$BASH_SERVER_SOCKET** environment variable.
3. A `socket` directive in **~/.bash-serverrc**.
4. **$XDG_RUNTIME_DIR**/bash-server/sock (if `$XDG_RUNTIME_DIR` is set).
5. /tmp/bash-server-*\<uid\>*/sock (fallback).

The directory containing the socket is created automatically (mode 0700) if it
does not exist.

# ENVIRONMENT

**BASH_SERVER_SOCKET**
:   Unix socket path.  Overridden by **--socket**.

**XDG_RUNTIME_DIR**
:   Base directory for runtime files.  Used in socket path resolution (rule 4).

**HOME**
:   User home directory.  Used to locate **~/.bash-serverrc** and startup files.

# FILES

**~/.bash-serverrc**
:   Optional configuration file.  One directive per line; `#` introduces
    comments.  Currently recognized directives:

        socket PATH

*\<socket-path\>*
:   The Unix domain socket file.  Created on startup, removed on shutdown.
    Permissions: 0600.

*\<socket-path\>*.token
:   The authentication token file.  Contains the hex-encoded 256-bit token.
    Created on startup, removed on shutdown.  Permissions: 0600.

*\<pidfile\>*
:   PID file (only when **--pidfile** is used).  Removed on shutdown.

# EXIT STATUS

| Code | Meaning                          |
|------|----------------------------------|
| 0    | Clean shutdown (signal or error) |
| 1    | Startup failure (bad arguments, socket bind error, etc.) |

# SIGNAL HANDLING

**SIGINT**, **SIGTERM**
:   Initiate clean shutdown.  The accept loop exits, the socket file and token
    file are removed, and child processes are reaped.

**SIGCHLD**
:   Reap terminated child processes (fork-per-command model).

**SIGPIPE**
:   Ignored.  Write errors are handled inline.

# EXAMPLES

Start a server with default socket path:

    bash-server

Start as a daemon with explicit socket and PID file:

    bash-server --daemon --socket /run/user/1000/bash-server/sock \
                --pidfile /run/user/1000/bash-server/pid

Start with login shell initialization, no bashrc:

    bash-server --login --norc

Use stdio mode (e.g., over SSH):

    bash-server --stdio

Use a Windows Named Pipe (Cygwin):

    bash-server --named-pipe myapp

Disable peer credential check for Python clients:

    bash-server --no-peercred

# SEE ALSO

**bashclient**(1), **bash**(1), **unix**(7)

Full documentation: `doc/server/` in the bash source tree.

# AUTHORS

Copyright (C) 2026 Free Software Foundation, Inc.

# COPYRIGHT

License GPLv3+: GNU GPL version 3 or later
<https://gnu.org/licenses/gpl.html>.

This is free software: you are free to change and redistribute it.  There is
NO WARRANTY, to the extent permitted by law.
