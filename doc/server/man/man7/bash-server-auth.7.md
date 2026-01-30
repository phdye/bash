# BASH-SERVER-AUTH(7) -- Authentication model

# DESCRIPTION

bash-server uses a shared-secret token model for client authentication.
The server generates a cryptographically random token at startup,
writes it to a file with restrictive permissions, and requires every
client to present this token before any other operations are permitted.

# CONCEPTS

# Token generation

At startup, the server reads 32 bytes of entropy from `/dev/urandom`
and hex-encodes them to produce a 64-character token string.  The raw
random bytes are zeroed with `memset()` immediately after encoding.

The token is written to a file at `<socket_path>.token` with
permissions `0600` (owner read/write only).  Only processes running as
the same user can read the token file.

# Token delivery

The token reaches the client through one of these mechanisms:

- **Token file**: The client reads `<socket_path>.token` from the
  filesystem.  This is the default and most common method.
- **Auth-fd mode** (`--auth-fd N`): The server writes the token to file
  descriptor N instead of stderr.  This is designed for parent processes
  that spawn the server and want to receive the token programmatically
  without filesystem round-trips.
- **Stderr** (default when no `--auth-fd`): The server prints the token
  to stderr for manual use or capture by a parent process.

# v1 authentication

In the v1 text protocol, authentication is a single request-response
exchange:

```
Client: AUTH <hex-token>\n
Server: OK\n              (on success)
Server: ERR auth_failed\n (on failure)
```

The token is sent as a plain hex string.  The server compares it using
constant-time comparison.

# v2 authentication

In the v2 protocol (binary or NDJSON), authentication uses JSON
messages on CHAN_CONTROL:

```json
Client: {"type":"auth","token":"<hex-token>"}
Server: {"type":"auth_ok"}
Server: {"type":"auth_fail","reason":"invalid token"}
```

# Constant-time comparison

Token comparison uses `protocol_secure_compare()`, which examines every
byte of both strings regardless of where a mismatch occurs.  This
prevents timing side-channel attacks where an attacker could deduce
token characters by measuring response times.

The implementation XORs corresponding bytes and accumulates the result,
then checks whether the accumulated value is zero.  Both strings must
also be the same length.

# Cygwin SO_PEERCRED handshake

On Cygwin, Unix domain sockets are implemented over TCP loopback.
During `connect()`/`accept()`, Cygwin performs a credential handshake
to exchange `ucred` structures.

This handshake can conflict with non-blocking connect patterns used by
Python and other high-level socket libraries, causing `ECONNABORTED`
(errno 113) on `accept()`.

The `--no-peercred` flag calls:

```c
setsockopt(fd, SOL_SOCKET, SO_PEERCRED, NULL, 0);
```

This disables the credential handshake entirely.  The `NULL, 0`
argument form is required; any non-NULL optval or non-zero optlen
returns `EINVAL`.

**Trade-off**: `getpeereid()` and `getsockopt(SO_PEERCRED)` no longer
return peer credentials.  This is acceptable because bash-server relies
on token-based authentication, not peer credential checks.

# Pre-authentication restrictions

Before successful authentication, the server only accepts:

- `AUTH` (v1) or `auth` on CHAN_CONTROL (v2)

All other commands or channel messages are rejected until the client
has authenticated.

# Session initialization

On the first successful authentication in a server's lifetime, the
server calls `init_bash_for_session()` to initialize the Bash
interpreter: builtins, traps, signals, tilde expansion, shell
variables, job control stubs, input subsystem, and shell options.
This initialization is performed once and is guarded by a
`bash_initialized` flag.

# SEE ALSO

**bash-server**(7),
**bash-server-channels**(7),
**bash-server-transports**(7)

# AUTHORS

GNU Bash is Copyright (C) Free Software Foundation, Inc.
The bash-server extension was developed as part of the Cygwin Bash project.

# COPYRIGHT

This is free software; see the GNU General Public License v3 or later
for copying conditions.  There is NO warranty.
