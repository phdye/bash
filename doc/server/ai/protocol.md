# bash-server Wire Protocol

## Protocol Versions

| Version | ID | Wire Format | Detection |
|---------|-----|-------------|-----------|
| v1 | `PROTOCOL_V1` (1) | Line-oriented text, LF-terminated | First byte >= 0x20 (printable ASCII, not `{`) |
| v2 binary | `PROTOCOL_V2` (2) | 6-byte header + JSON payload | First byte 0x00–0x05 (channel ID) |
| v2 NDJSON | `PROTOCOL_V2_NDJSON` (3) | One JSON object per line, LF-terminated | First byte 0x7B (`{`) |

## Auto-Detection Algorithm

Detection reads one byte via `recv(fd, &byte, 1, MSG_PEEK)` (non-consuming).

| First Byte | Range | Protocol |
|------------|-------|----------|
| 0x00–0x05 | Channel ID range | v2 binary |
| 0x7B | `{` | v2 NDJSON |
| 0x20–0x7A, 0x7C–0xFF | Other printable | v1 text |

No v1 command starts with `{`, so detection is unambiguous.

## v1 Wire Format

### Line Format

```
COMMAND [ARGUMENT]\n
```

- Lines terminated by LF (`\n`)
- CR (`\r`) silently stripped on read
- Maximum line length: `SERVER_MAX_LINE` (8192 bytes)
- Command parsed case-insensitively (converted to uppercase)
- Leading/trailing whitespace trimmed from arguments

### Request Grammar

```
request     = command SP argument LF
command     = 1*ALPHA
argument    = *OCTET
SP          = %x20
LF          = %x0A
```

### Response Grammar

```
response    = status-code SP body LF
status-code = "OK" / "ERR" / "BYE" / "PONG" / "STDOUT" / "STDERR" / "EXIT"
            / "VALUE" / "FUNC" / "ALIAS" / "TRAP" / "INSPECT-END"
body        = *OCTET
```

### v1 Encoding Rules

- EVAL command argument: raw shell command text (no encoding)
- STDOUT/STDERR response payloads: base64-encoded
- VALUE/FUNC/ALIAS response values: base64-encoded
- SET-VAR value argument: raw text
- All other arguments: raw text

### Base64 Encoding

- Standard base64 alphabet: `A-Za-z0-9+/`
- Padding: `=` characters
- Input must be multiple of 4 bytes for decoding
- Used for: STDOUT payloads, STDERR payloads, variable/function/alias values

## v2 Binary Frame Format

### Header Layout (6 bytes)

| Offset | Size | Type | Field | Description |
|--------|------|------|-------|-------------|
| 0 | 1 | uint8 | channel | Logical channel (0–5) |
| 1 | 1 | uint8 | flags | Bitfield (see below) |
| 2 | 4 | uint32 | length | Payload length, network byte order (big-endian) |

### Payload

- Follows immediately after header
- `length` bytes of JSON string
- NUL-terminated by reader (not on wire)

### Frame Flags

| Flag | Value | Description |
|------|-------|-------------|
| `FRAME_FLAG_COMPRESSED` | 0x01 | Payload is compressed (reserved, not implemented) |
| `FRAME_FLAG_BINARY` | 0x02 | Payload is binary, not JSON (reserved) |
| `FRAME_FLAG_CONTINUED` | 0x04 | Frame is part of a multi-frame message (reserved) |
| `FRAME_FLAG_FINAL` | 0x08 | Last frame of multi-frame message (reserved) |

Current implementation: all frames sent with flags=0.

### Channel IDs

| ID | Name | Direction | Purpose |
|----|------|-----------|---------|
| 0 | `CHAN_CONTROL` | Bidirectional | Auth, ping, disconnect, configure |
| 1 | `CHAN_COMMAND` | Bidirectional | Command eval, stdout/stderr/complete |
| 2 | `CHAN_STATE` | Bidirectional | Variable/function/alias/trap CRUD |
| 3 | `CHAN_OBSERVE` | Server→Client (events), Client→Server (subscribe) | Pre/post command events |
| 4 | `CHAN_DEBUG` | Bidirectional | Breakpoints, stepping, AST inspect |
| 5 | `CHAN_PTY` | Bidirectional | PTY spawn, I/O relay, resize, signal |

`CHAN_MAX` = 5. Frames with channel > `CHAN_MAX` are rejected; payload is skipped.

## v2 NDJSON Frame Format

### Line Format

```
{"ch":N, ...payload fields...}\n
```

- One JSON object per line, LF-terminated
- `"ch"` field carries channel ID (equivalent to binary header channel byte)
- If `"ch"` field missing, defaults to `CHAN_CONTROL` (0)
- Maximum line length: `FRAME_MAX_PAYLOAD` + 64 bytes

### Write Algorithm

1. If payload starts with `{`: inject `"ch":N,` after opening brace, append `\n`
2. If payload is not JSON: wrap as `{"ch":N,"data":"<payload>"}\n`
3. Uses `writev()` for zero-copy assembly

### Read Algorithm

1. Read bytes until `\n`
2. NUL-terminate
3. Extract `"ch"` integer field → channel ID
4. Return full line as payload (including `"ch"` field)

## Limits

| Constant | Value | Description |
|----------|-------|-------------|
| `SERVER_MAX_LINE` | 8192 | Maximum v1 line length (bytes) |
| `SERVER_MAX_TOKEN` | 256 | Maximum token length (bytes) |
| `SERVER_MAX_CMD` | 65536 | Maximum command length (bytes) |
| `SERVER_MAX_OUTPUT` | 1048576 (1 MB) | Maximum captured stdout/stderr (bytes) |
| `FRAME_HEADER_SIZE` | 6 | v2 binary frame header size (bytes) |
| `FRAME_MAX_PAYLOAD` | 1048576 (1 MB) | Maximum v2 frame payload (bytes) |
| `SERVER_TOKEN_BYTES` | 32 | Authentication token entropy (bytes) |
| `SERVER_TOKEN_HEXLEN` | 64 | Authentication token hex string length |

## Wire Format Selection

| Mechanism | Scope | Description |
|-----------|-------|-------------|
| Auto-detection | Per-session | First byte determines v1 vs v2-binary vs v2-NDJSON |
| `json_set_wire_format()` | Process-global | Sets `WIRE_BINARY` (0) or `WIRE_NDJSON` (1) |
| `configure` message | Per-session | Can switch wire format via CHAN_CONTROL |

Wire format is process-global but safe because bash-server uses fork-per-session.
