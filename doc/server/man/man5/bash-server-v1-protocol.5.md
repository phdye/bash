# bash-server-v1-protocol(5) — bash-server v1 text wire protocol

# DESCRIPTION

The **v1 protocol** is a line-oriented, text-based wire format used for
communication between **bash-server**(1) and its clients. It is the
original protocol and remains supported for simplicity and ease of
debugging.

All messages are **LF-terminated** lines. Binary data is encoded using
**base64** to ensure safe transport. The protocol is synchronous:
the client sends a command and waits for the complete response before
sending the next command.

# FORMAT

# Framing

- Each message is a single line terminated by LF (0x0A).
- Maximum line length: **8192 bytes** (including the terminating LF).
- Lines exceeding this limit cause the connection to be dropped.

# Command parsing

Commands are parsed by **protocol_parse_command()**. The first
space-separated word is the command keyword (case-sensitive). Everything
after the first space is the argument. If no space is present, the
argument is empty.

# Client commands (client to server)

| Command | Arguments | Description |
|---------|-----------|-------------|
| **AUTH** | *\<token\>* | Authenticate with the server. The token is the 64-character hex string from the token file. |
| **EVAL** | *\<base64-command\>* | Evaluate a Bash command. The command string is base64-encoded. |
| **PING** | *(none)* | Liveness check. |
| **QUIT** | *(none)* | Disconnect gracefully. |
| **GET-VAR** | *\<name\>* | Retrieve the value of shell variable *name*. |
| **SET-VAR** | *\<name\>* *\<base64-value\>* | Set shell variable *name* to the base64-decoded value. |
| **UNSET-VAR** | *\<name\>* | Unset shell variable *name*. |
| **GET-FUNC** | *\<name\>* | Retrieve the definition of shell function *name*. |
| **UNSET-FUNC** | *\<name\>* | Remove shell function *name*. |
| **GET-ALIAS** | *\<name\>* | Retrieve the expansion of alias *name*. |
| **SET-ALIAS** | *\<name\>* *\<base64-value\>* | Define alias *name* with the base64-decoded expansion. |
| **UNSET-ALIAS** | *\<name\>* | Remove alias *name*. |
| **SET-TRAP** | *\<sigspec\>* *\<base64-action\>* | Set a trap on signal *sigspec* with the base64-decoded action. |
| **UNSET-TRAP** | *\<sigspec\>* | Remove the trap on signal *sigspec*. |
| **INSPECT** | *\<type\>* | List all items of *type* (one of: **vars**, **funcs**, **aliases**, **traps**). |

# Server responses (server to client)

| Response | Arguments | Description |
|----------|-----------|-------------|
| **OK** | *(none)* | Operation succeeded. |
| **ERR** | *\<message\>* | Operation failed. The message is a human-readable error string. |
| **PONG** | *(none)* | Reply to PING. |
| **BYE** | *(none)* | Acknowledgement of QUIT; connection will close. |
| **STDOUT** | *\<base64\>* | Base64-encoded standard output from an EVAL command. |
| **STDERR** | *\<base64\>* | Base64-encoded standard error from an EVAL command. |
| **EXIT** | *\<code\>* | Exit status of the most recent EVAL command (integer). |

# EVAL response sequence

An **EVAL** command produces a multi-line response in the following
order:

1. Zero or more **STDOUT** lines (chunked if output is large).
2. Zero or more **STDERR** lines (chunked if output is large).
3. Exactly one **EXIT** line.

The client must read until the **EXIT** line to know the command has
completed.

# Base64 encoding

Base64 follows RFC 4648 standard encoding (alphabet A-Z, a-z, 0-9,
+, /, with = padding). This ensures binary-safe transport of command
strings and output that may contain newlines, null bytes, or other
control characters.

# EXAMPLES

# Authentication

```
C: AUTH a3f7c9e2b14d08563fa91e7c4b2d60f8e5a3c7d9012b4f68e3a1c5d7f9b20e46
S: OK
```

Failed authentication:

```
C: AUTH 0000000000000000000000000000000000000000000000000000000000000000
S: ERR authentication failed
```

# Evaluating a command

Evaluate `echo hello world` (base64: `ZWNobyBoZWxsbyB3b3JsZA==`):

```
C: EVAL ZWNobyBoZWxsbyB3b3JsZA==
S: STDOUT aGVsbG8gd29ybGQK
S: EXIT 0
```

# Variable operations

```
C: GET-VAR HOME
S: OK /home/alice

C: SET-VAR myvar dGVzdCB2YWx1ZQ==
S: OK

C: UNSET-VAR myvar
S: OK
```

# Liveness check

```
C: PING
S: PONG
```

# Graceful disconnect

```
C: QUIT
S: BYE
```

# NOTES

The v1 protocol does not support multiplexed channels, concurrent
commands, or push notifications. For these features, use the
**v2 binary frame protocol** or the **NDJSON format**.

Protocol version auto-detection examines the first byte of the
connection. If it is a printable ASCII character (as typical for v1
commands), v1 mode is selected. See **bash-server-v2-protocol**(5)
for the auto-detection algorithm.

# SEE ALSO

**bash-server**(1),
**bash-serverrc**(5),
**bash-server-token**(5),
**bash-server-v2-protocol**(5),
**bash-server-ndjson**(5),
**bash-server-json-messages**(5)

# AUTHORS

GNU Bash is written by Brian Fox and Chet Ramey. The bash-server
extension and this documentation were developed as part of the
Cygwin bash-server project.

# COPYRIGHT

Copyright (C) 2024-2025 Free Software Foundation, Inc. License GPLv3+:
GNU GPL version 3 or later <https://www.gnu.org/licenses/gpl.html>.
This is free software; you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
