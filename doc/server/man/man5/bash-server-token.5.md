# bash-server-token(5) — bash-server authentication token file

# DESCRIPTION

The **token file** contains the shared secret used to authenticate
clients connecting to **bash-server**(1). The server generates a
cryptographically random token at startup and writes it to this file.
Clients must present the token via the **AUTH** command (v1) or
**auth** message (v2) before any other operations are permitted.

The token is deleted when the server shuts down cleanly. If the
server terminates abnormally, a stale token file may remain on disk
and must be removed manually before restarting.

# FORMAT

The file contains exactly one line: a **64-character hexadecimal
string** encoding a 256-bit (32-byte) random value, followed by a
newline character (LF, 0x0A).

```
<64 hex digits>\n
```

The hex digits use lowercase **a**-**f**. No whitespace, prefix, or
other content is present.

# File permissions

The token file is created with mode **0600** (owner read/write only).
The server refuses to start if it cannot set these permissions. Clients
running as a different user will be unable to read the file, which is
the intended security boundary.

# File location

The token file path is derived from the socket path by appending
**.token** to it:

| Transport | Token path |
|-----------|------------|
| Unix socket at *PATH* | *PATH***.token** |
| Named pipe with name *NAME* | **$XDG_RUNTIME_DIR/bash-server/***NAME***.token** |
| Named pipe (no XDG) | **/tmp/bash-server-***\<uid\>***/***NAME***.token** |

For example, if the socket is **/run/user/1000/bash-server/sock**,
the token file is **/run/user/1000/bash-server/sock.token**.

# Lifecycle

1. **Startup**: server reads 32 bytes from **/dev/urandom**, hex-encodes
   them, and writes the result to the token file.
2. **Runtime**: clients read the file and send the token with their
   first protocol message.
3. **Shutdown**: server unlinks the token file. If the socket file is
   also being removed, the token file is deleted first.

# EXAMPLES

A typical token file:

```
a3f7c9e2b14d08563fa91e7c4b2d60f8e5a3c7d9012b4f68e3a1c5d7f9b20e46
```

Reading the token in a shell script:

```bash
TOKEN=$(cat /run/user/1000/bash-server/sock.token)
```

Verifying file permissions:

```bash
stat -c '%a' /run/user/1000/bash-server/sock.token
# Expected output: 600
```

# SECURITY CONSIDERATIONS

The token provides the sole authentication mechanism for bash-server.
Anyone who can read the token file can execute arbitrary commands in the
server's Bash session. Therefore:

- The file MUST remain mode **0600**.
- The containing directory SHOULD be mode **0700** or **0750**.
- The token MUST be generated from a cryptographic random source
  (**/dev/urandom**).
- Constant-time comparison (**secure_compare**) is used server-side
  to prevent timing attacks.

# SEE ALSO

**bash-server**(1),
**bash-serverrc**(5),
**bash-server-v1-protocol**(5),
**bash-server-v2-protocol**(5)

# AUTHORS

GNU Bash is written by Brian Fox and Chet Ramey. The bash-server
extension and this documentation were developed as part of the
Cygwin bash-server project.

# COPYRIGHT

Copyright (C) 2024-2025 Free Software Foundation, Inc. License GPLv3+:
GNU GPL version 3 or later <https://www.gnu.org/licenses/gpl.html>.
This is free software; you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
