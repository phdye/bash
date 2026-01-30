# bash-server-v2-protocol(5) — bash-server v2 binary frame wire protocol

# DESCRIPTION

The **v2 protocol** is a binary-framed, channel-multiplexed wire
format for communication between **bash-server**(1) and its clients.
It supports concurrent operations across independent channels, each
carrying JSON-encoded payloads.

The v2 protocol may use either **binary framing** (described here) or
**NDJSON framing** (see **bash-server-ndjson**(5)). Both carry the
same JSON message schemas (see **bash-server-json-messages**(5)).

# FORMAT

# Frame structure

Every v2 message is wrapped in a 6-byte header followed by a
variable-length payload:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    channel    |     flags     |         payload_length        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+                               +
|                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                         payload ...                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| Offset | Size | Field | Description |
|--------|------|-------|-------------|
| 0 | 1 byte | **channel** | Channel identifier (0-5). |
| 1 | 1 byte | **flags** | Bitfield of frame flags. |
| 2 | 4 bytes | **payload_length** | Length of payload in bytes, network byte order (big-endian). |
| 6 | *N* bytes | **payload** | JSON text (UTF-8 encoded). |

# Maximum payload size

The maximum payload length is **1,048,576 bytes** (1 MB). Frames
exceeding this limit cause the connection to be dropped with an error.

# Channel identifiers

| ID | Name | Direction | Purpose |
|----|------|-----------|---------|
| 0 | **CHAN_CONTROL** | Bidirectional | Authentication, ping, configuration |
| 1 | **CHAN_COMMAND** | Bidirectional | Command evaluation and output |
| 2 | **CHAN_STATE** | Bidirectional | Variable, function, alias, trap operations |
| 3 | **CHAN_OBSERVE** | Server to client | Event observation push notifications |
| 4 | **CHAN_DEBUG** | Bidirectional | Debugger control and breakpoints |
| 5 | **CHAN_PTY** | Bidirectional | Pseudo-terminal I/O |

# Frame flags

| Bit | Value | Name | Description |
|-----|-------|------|-------------|
| 0 | 0x01 | **COMPRESSED** | Payload is compressed (reserved, not yet implemented). |
| 1 | 0x02 | **BINARY** | Payload is binary data, not JSON text. |
| 2 | 0x04 | **CONTINUED** | This frame continues a multi-frame message. |
| 3 | 0x08 | **FINAL** | This is the final frame of a multi-frame message. |

For single-frame messages (the common case), flags is typically
**0x00**. The CONTINUED and FINAL flags enable streaming large
payloads across multiple frames.

# Payload format

Unless the **BINARY** flag is set, the payload is a UTF-8 encoded JSON
object. The JSON schemas for each channel are documented in
**bash-server-json-messages**(5).

# AUTO-DETECTION

The server supports automatic protocol version detection on each new
connection. The algorithm examines the **first byte** received:

| First byte | Protocol selected |
|------------|-------------------|
| 0x00-0x05 | **v2 binary** (byte is a valid channel ID) |
| **{** (0x7B) or **\n** (0x0A) | **v2 NDJSON** (see **bash-server-ndjson**(5)) |
| Any other printable ASCII | **v1 text** (see **bash-server-v1-protocol**(5)) |

The first byte is consumed as part of the first message and is not
discarded. Auto-detection occurs exactly once per connection; the
protocol cannot be changed mid-session.

# EXAMPLES

# Binary frame: AUTH message on CHAN_CONTROL

JSON payload: `{"type":"auth","token":"a3f7...0e46"}`

Assuming the payload is 78 bytes:

```
Offset  Hex
00      00              channel = 0 (CHAN_CONTROL)
01      00              flags = 0x00
02      00 00 00 4E     payload_length = 78 (network byte order)
06      7B 22 74 79 ... payload (78 bytes of UTF-8 JSON)
```

# Binary frame: EVAL command on CHAN_COMMAND

JSON payload: `{"type":"eval","command":"echo hello"}`

Assuming the payload is 37 bytes:

```
Offset  Hex
00      01              channel = 1 (CHAN_COMMAND)
01      00              flags = 0x00
02      00 00 00 25     payload_length = 37
06      7B 22 74 79 ... payload (37 bytes of UTF-8 JSON)
```

# Multi-frame message

A large stdout response split across two frames:

Frame 1 (continued):
```
01 04 00 10 00 00  ...payload (1 MB)...
```
- channel=1, flags=0x04 (CONTINUED), length=1048576

Frame 2 (final):
```
01 08 00 00 01 00  ...payload (256 bytes)...
```
- channel=1, flags=0x08 (FINAL), length=256

# SEE ALSO

**bash-server**(1),
**bash-server-v1-protocol**(5),
**bash-server-ndjson**(5),
**bash-server-json-messages**(5),
**bash-server-cmd-json**(5)

# AUTHORS

GNU Bash is written by Brian Fox and Chet Ramey. The bash-server
extension and this documentation were developed as part of the
Cygwin bash-server project.

# COPYRIGHT

Copyright (C) 2024-2025 Free Software Foundation, Inc. License GPLv3+:
GNU GPL version 3 or later <https://www.gnu.org/licenses/gpl.html>.
This is free software; you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
