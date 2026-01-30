# bash-server-ndjson(5) — bash-server NDJSON wire format

# DESCRIPTION

The **NDJSON (Newline-Delimited JSON)** format is an alternative wire
framing for the **bash-server**(1) v2 protocol. It carries the same
JSON message schemas and channel semantics as the binary v2 framing
(see **bash-server-v2-protocol**(5)), but uses a text-based,
line-oriented envelope that is easier to debug and generate from
scripting languages.

# FORMAT

# Framing

Each message is a single JSON object serialized on one line and
terminated by LF (0x0A):

```
{"channel":N, ...message fields...}\n
```

- Each line MUST be valid JSON.
- Each JSON object MUST include a **"channel"** field (integer, 0-5).
- No blank lines or comments are permitted between messages.
- There is no explicit maximum line length, but the same 1 MB payload
  limit from the binary v2 protocol applies to the serialized JSON.

# Channel field

The **"channel"** field replaces the channel byte from the binary
frame header. It takes the same integer values:

| Value | Channel |
|-------|---------|
| 0 | CHAN_CONTROL |
| 1 | CHAN_COMMAND |
| 2 | CHAN_STATE |
| 3 | CHAN_OBSERVE |
| 4 | CHAN_DEBUG |
| 5 | CHAN_PTY |

All remaining fields in the JSON object are the message payload,
identical to the schemas documented in
**bash-server-json-messages**(5).

# Wire format selection

The wire format is determined once per session during auto-detection
and cannot change mid-connection. Two internal functions manage the
format:

- **json_set_wire_format(format)** — set the format for the current
  session. *format* is **WIRE_BINARY** (0) or **WIRE_NDJSON** (1).
- **json_get_wire_format()** — return the current format.

# Auto-detection

If the first byte received on a new connection is **{** (0x7B) or
**\n** (0x0A), NDJSON mode is selected. Otherwise, the byte is
evaluated against the binary v2 and v1 text rules. See
**bash-server-v2-protocol**(5) for the complete auto-detection
algorithm.

# EXAMPLES

# Authentication handshake

Client sends:

```json
{"channel":0,"type":"auth","token":"a3f7c9e2b14d08563fa91e7c4b2d60f8e5a3c7d9012b4f68e3a1c5d7f9b20e46"}
```

Server responds:

```json
{"channel":0,"type":"auth_ok"}
```

# Evaluating a command

Client:

```json
{"channel":1,"type":"eval","command":"echo hello world"}
```

Server (multiple lines):

```json
{"channel":1,"type":"stdout","data":"aGVsbG8gd29ybGQK","encoding":"base64"}
{"channel":1,"type":"complete","exit_code":0}
```

# Variable operations

```json
{"channel":2,"type":"get_var","name":"HOME"}
{"channel":2,"type":"var","name":"HOME","value":"/home/alice"}
```

# Ping

```json
{"channel":0,"type":"ping"}
{"channel":0,"type":"pong"}
```

# Observation event (server push)

```json
{"channel":3,"level":1,"type":"pre_command","seq":42,"timestamp":1706640000,"data":{"command":"ls -la","cwd":"/home/alice","line_number":1,"is_subshell":false,"is_async":false}}
```

# NOTES

NDJSON is particularly convenient for:

- **Shell scripts** using tools like **jq**(1) and **socat**(1).
- **Python** clients using the **json** standard library.
- **Debugging** with **nc**(1) or **ncat**(1).

The trade-off compared to binary framing is slightly higher bandwidth
usage due to the JSON envelope and text encoding of the channel field.

# SEE ALSO

**bash-server**(1),
**bash-server-v2-protocol**(5),
**bash-server-json-messages**(5),
**bash-server-v1-protocol**(5)

# AUTHORS

GNU Bash is written by Brian Fox and Chet Ramey. The bash-server
extension and this documentation were developed as part of the
Cygwin bash-server project.

# COPYRIGHT

Copyright (C) 2024-2025 Free Software Foundation, Inc. License GPLv3+:
GNU GPL version 3 or later <https://www.gnu.org/licenses/gpl.html>.
This is free software; you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
