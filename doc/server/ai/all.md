# bash-server — Complete AI Reference

> This file concatenates all AI-focused reference documentation
> for the bash-server v1/v2 protocol. Individual files:
> protocol.md, api-reference.md, errors.md, integration.md

---

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

---

# API Reference — bash-server

## Constants

| Name | Value | Description |
|------|-------|-------------|
| SERVER_MAX_LINE | 8192 | Maximum line length (v1 protocol) |
| SERVER_MAX_TOKEN | 256 | Maximum token buffer size |
| SERVER_MAX_CMD | 65536 | Maximum command string length |
| SERVER_MAX_OUTPUT | 1048576 | Maximum captured output (1 MB) |
| SERVER_TOKEN_BYTES | 32 | Token entropy bytes |
| SERVER_TOKEN_HEXLEN | 64 | Token hex-encoded length |
| FRAME_HEADER_SIZE | 6 | v2 binary frame header bytes |
| FRAME_MAX_PAYLOAD | 1048576 | v2 maximum frame payload (1 MB) |
| CHAN_CONTROL | 0 | Auth, configure, disconnect |
| CHAN_COMMAND | 1 | Eval, pre-parsed trees, responses |
| CHAN_STATE | 2 | Variable/function get/set |
| CHAN_OBSERVE | 3 | Observability events (server push) |
| CHAN_DEBUG | 4 | Breakpoints, stepping |
| CHAN_PTY | 5 | Terminal I/O |
| CHAN_MAX | 5 | Highest channel ID |
| PROTOCOL_V1 | 1 | Line-oriented text protocol |
| PROTOCOL_V2 | 2 | Length-prefixed JSON frames |
| PROTOCOL_V2_NDJSON | 3 | Newline-delimited JSON |
| WIRE_BINARY | 0 | Binary frame wire format |
| WIRE_NDJSON | 1 | NDJSON wire format |
| OBSERVE_LEVEL_OFF | 0 | No observe events |
| OBSERVE_LEVEL_COMMAND | 1 | Pre/post command events |
| OBSERVE_LEVEL_MAX | 1 | Highest supported observe level |

## Frame Flags (v2 Binary)

| Flag | Value | Description |
|------|-------|-------------|
| FRAME_FLAG_COMPRESSED | 0x01 | Payload is compressed |
| FRAME_FLAG_BINARY | 0x02 | Payload is binary (not JSON) |
| FRAME_FLAG_CONTINUED | 0x04 | Continuation frame |
| FRAME_FLAG_FINAL | 0x08 | Final frame in sequence |

---

## v1 Protocol Commands

All v1 commands are LF-terminated lines. Commands are case-insensitive (uppercased by parser).

### AUTH

| Field | Value |
|-------|-------|
| Request | `AUTH <hex-token>` |
| Success | `OK` or `OK already authenticated` |
| Failure | `ERR token required` or `ERR invalid token` |

### EVAL

| Field | Value |
|-------|-------|
| Request | `EVAL <command-string>` |
| Response sequence | `STDOUT [<base64>]` then `STDERR [<base64>]` then `EXIT <code>` |
| Failure | `ERR not authenticated` or `ERR command required` |

### PING

| Field | Value |
|-------|-------|
| Request | `PING` |
| Response | `PONG` |

### QUIT

| Field | Value |
|-------|-------|
| Request | `QUIT` |
| Response | `BYE` |

### GET-VAR

| Field | Value |
|-------|-------|
| Request | `GET-VAR <name>` |
| Success | `VALUE <name> <base64-value> [attrs]` |
| Failure | `ERR variable name required` or `ERR variable not found: <name>` |

Attributes: comma-separated list of: exported, readonly, integer, local, array, assoc, nameref, uppercase, lowercase, capcase, trace.

### SET-VAR

| Field | Value |
|-------|-------|
| Request | `SET-VAR <name> <value> [--export] [--readonly] [--integer]` |
| Success | `OK` |
| Failure | `ERR variable name required` or `ERR bind_variable failed for <name>` |

### UNSET-VAR

| Field | Value |
|-------|-------|
| Request | `UNSET-VAR <name>` |
| Success | `OK` |
| Failure | `ERR variable name required` or `ERR <name>: readonly variable` |

### GET-FUNC

| Field | Value |
|-------|-------|
| Request | `GET-FUNC <name>` |
| Success | `FUNC <name> <base64-definition>` |
| Failure | `ERR function name required` or `ERR function not found: <name>` |

### UNSET-FUNC

| Field | Value |
|-------|-------|
| Request | `UNSET-FUNC <name>` |
| Success | `OK` |
| Failure | `ERR function name required` or `ERR function not found: <name>` |

### GET-ALIAS

| Field | Value |
|-------|-------|
| Request | `GET-ALIAS <name>` |
| Success | `ALIAS <name> <base64-value>` |
| Failure | `ERR alias name required` or `ERR alias not found: <name>` |

### SET-ALIAS

| Field | Value |
|-------|-------|
| Request | `SET-ALIAS <name> <value>` |
| Success | `OK` |
| Failure | `ERR alias name required` or `ERR alias value required` |

### UNSET-ALIAS

| Field | Value |
|-------|-------|
| Request | `UNSET-ALIAS <name>` |
| Success | `OK` |
| Failure | `ERR alias name required` or `ERR alias not found: <name>` |

### SET-TRAP

| Field | Value |
|-------|-------|
| Request | `SET-TRAP <signal> <command>` |
| Success | `OK` |
| Failure | `ERR signal name required` or `ERR unknown signal: <name>` or `ERR trap command required` |

### UNSET-TRAP

| Field | Value |
|-------|-------|
| Request | `UNSET-TRAP <signal>` |
| Success | `OK` |
| Failure | `ERR signal name required` or `ERR unknown signal: <name>` |

### INSPECT

| Field | Value |
|-------|-------|
| Request | `INSPECT <target> [pattern]` |
| Targets | vars, functions, aliases, traps |
| Response | Multiple `VALUE`/`FUNC`/`ALIAS`/`TRAP` lines terminated by `INSPECT-END` |
| Failure | `ERR inspect target required (vars, functions, aliases, traps)` or `ERR unknown inspect target: <target>` |

INSPECT response line formats:

| Target | Line format |
|--------|-------------|
| vars | `VALUE <name> <base64-value> [attrs]` |
| functions | `FUNC <name> <base64-definition>` |
| aliases | `ALIAS <name> <base64-value>` |
| traps | `TRAP <signal-name> <base64-command>` |

## v1 Response Codes

| Code | String | Usage |
|------|--------|-------|
| RSP_OK | `OK` | Success |
| RSP_ERR | `ERR` | Error (followed by message) |
| RSP_BYE | `BYE` | Connection closing |
| RSP_PONG | `PONG` | Ping response |
| RSP_STDOUT | `STDOUT` | Captured stdout (followed by optional base64) |
| RSP_STDERR | `STDERR` | Captured stderr (followed by optional base64) |
| RSP_EXIT | `EXIT` | Exit code (followed by integer) |

---

## v2 Channel 0: CONTROL

### auth (request)

```json
{"type": "auth", "token": "<64-hex-chars>"}
```

### auth_ok (response)

```json
{"type": "auth_ok", "capabilities": ["state", "command", "observe", "debug"]}
```

Already authenticated variant:

```json
{"type": "auth_ok", "message": "already authenticated"}
```

### ping (request)

```json
{"type": "ping"}
```

### pong (response)

```json
{"type": "pong"}
```

### disconnect (request)

```json
{"type": "disconnect"}
```

### disconnect_ok (response)

```json
{"type": "disconnect_ok"}
```

### configure (request)

```json
{"type": "configure", "observability": <level>}
```

### configured (response)

```json
{"type": "configured", "observability": <actual-level>}
```

---

## v2 Channel 1: COMMAND

### eval (request)

```json
{"type": "eval", "command": "<string>", "id": "<optional-request-id>"}
```

### stdout (response)

```json
{"type": "stdout", "data": "<base64>", "encoding": "base64", "id": "<if-provided>"}
```

### stderr (response)

```json
{"type": "stderr", "data": "<base64>", "encoding": "base64", "id": "<if-provided>"}
```

### complete (response)

```json
{"type": "complete", "exit_code": <int>, "id": "<if-provided>"}
```

Response sequence for eval: stdout → stderr → complete.

---

## v2 Channel 2: STATE

### get (request)

```json
{"type": "get", "target": "<var|function|alias>", "name": "<string>"}
```

### value (response)

```json
{"type": "value", "target": "<target>", "name": "<string>", "value": "<string>", "attributes": ["<attr>", ...]}
```

Attributes array is optional (only present for variables with attributes).

### set (request)

For variables:

```json
{"type": "set", "target": "var", "name": "<string>", "value": "<string>", "attributes": ["exported", "readonly", "integer"]}
```

For aliases:

```json
{"type": "set", "target": "alias", "name": "<string>", "value": "<string>"}
```

### set_ok (response)

```json
{"type": "set_ok", "target": "<target>", "name": "<string>"}
```

### unset (request)

```json
{"type": "unset", "target": "<var|function|alias>", "name": "<string>"}
```

### unset_ok (response)

```json
{"type": "unset_ok", "target": "<target>", "name": "<string>"}
```

### inspect (request)

```json
{"type": "inspect", "query": "<vars|functions|aliases|traps>"}
```

### inspect_result (response)

```json
{"type": "inspect_result", "query": "<query>", "data": [<items>]}
```

Item schemas by query:

| Query | Item schema |
|-------|-------------|
| vars | `{"name": "<string>", "value": "<string>", "attributes": [...]}` |
| functions | `{"name": "<string>", "definition": "<string>"}` |
| aliases | `{"name": "<string>", "value": "<string>"}` |
| traps | `{"signal": "<string>", "command": "<string>"}` |

### Supported targets for get/set/unset

| Target value | get | set | unset |
|-------------|-----|-----|-------|
| var | yes | yes | yes |
| function | yes | no | yes |
| alias | yes | yes | yes |
| trap | no (use inspect) | via v1 SET-TRAP | via v1 UNSET-TRAP |

---

## v2 Channel 3: OBSERVE

### subscribe (request)

```json
{"type": "subscribe", "level": <int>}
```

### subscribed (response)

```json
{"type": "subscribed", "level": <actual-level>}
```

### unsubscribe (request)

```json
{"type": "unsubscribe"}
```

### unsubscribed (response)

```json
{"type": "unsubscribed"}
```

### pre_command (server push event)

```json
{
  "level": 1,
  "type": "pre_command",
  "seq": <uint>,
  "timestamp": <ms-since-epoch>,
  "data": {
    "command": "<string>",
    "cwd": "<string>",
    "line_number": <int>,
    "is_subshell": <bool>,
    "is_async": <bool>
  }
}
```

### post_command (server push event)

```json
{
  "level": 1,
  "type": "post_command",
  "seq": <uint>,
  "timestamp": <ms-since-epoch>,
  "data": {
    "command": "<string>",
    "exit_status": <int>,
    "signal_number": <int>,
    "duration_ms": <int>
  }
}
```

`signal_number` field is only present when command was killed by a signal.

---

## v2 Channel 4: DEBUG

### Breakpoint types

| Constant | Value | Match by |
|----------|-------|----------|
| DBG_BREAK_COMMAND | 1 | Command string pattern (substring match) |
| DBG_BREAK_LINE | 2 | Line number |
| DBG_BREAK_FUNC | 3 | Function name (substring match) |

### Step modes

| Constant | Value | Behavior |
|----------|-------|----------|
| DBG_RUN | 0 | Normal execution |
| DBG_STEP | 1 | Break before every command |
| DBG_NEXT | 2 | Break at same or shallower depth |
| DBG_FINISH | 3 | Break when depth decreases |
| DBG_SKIP | 4 | Skip current command (acknowledged but not enforced) |

### enable (request)

```json
{"type": "enable"}
```

### enable_ok (response)

```json
{"type": "enable_ok"}
```

### disable (request)

```json
{"type": "disable"}
```

### disable_ok (response)

```json
{"type": "disable_ok"}
```

### break (request)

```json
{"type": "break", "kind": "<command|line|function>", "pattern": "<string>", "line": <int>, "condition": "<string>"}
```

All fields except `type` are optional. Default kind: command.

### break_ok (response)

```json
{"type": "break_ok", "id": <int>}
```

### delete (request)

```json
{"type": "delete", "id": <int>}
```

### delete_ok (response)

```json
{"type": "delete_ok", "id": <int>, "found": <bool>}
```

### enable_bp (request)

```json
{"type": "enable_bp", "id": <int>}
```

### enable_bp_ok (response)

```json
{"type": "enable_bp_ok", "id": <int>, "found": <bool>}
```

### disable_bp (request)

```json
{"type": "disable_bp", "id": <int>}
```

### disable_bp_ok (response)

```json
{"type": "disable_bp_ok", "id": <int>, "found": <bool>}
```

### list (request)

```json
{"type": "list"}
```

### breakpoints (response)

```json
{"type": "breakpoints", "data": [<breakpoint-objects>]}
```

Breakpoint object:

```json
{
  "id": <int>,
  "type": "<command|line|function>",
  "enabled": <bool>,
  "hit_count": <int>,
  "pattern": "<string>",
  "line": <int>,
  "condition": "<string>"
}
```

### step (request)

```json
{"type": "step"}
```

### step_ok (response)

```json
{"type": "step_ok", "mode": "step"}
```

### continue (request)

```json
{"type": "continue"}
```

### continue_ok (response)

```json
{"type": "continue_ok"}
```

### status (request)

```json
{"type": "status"}
```

### status (response)

```json
{"type": "status", "active": <bool>, "mode": "<run|step|next|finish>", "breakpoints": <int>, "depth": <int>}
```

### inspect_ast (request)

```json
{"type": "inspect_ast"}
```

### ast (response)

```json
{"type": "ast", "data": <command-json-or-null>}
```

### break_hit (server push event)

```json
{"type": "break_hit", "line": <int>, "command": "<string>", "depth": <int>}
```

Sent when a breakpoint or step condition matches. Server blocks on CHAN_DEBUG until a resume command is received.

### Resume commands (sent while paused at break_hit)

| type | Effect |
|------|--------|
| continue | Resume normal execution |
| step | Step into next command |
| next | Step over (same depth) |
| finish | Step out (shallower depth) |
| skip | Skip current command (not enforced) |
| inspect_ast | Return AST of pending command |
| list | Return breakpoint list |
| break | Add breakpoint while paused |
| delete | Remove breakpoint while paused |

---

## v2 Channel 5: PTY

### spawn (request)

```json
{"type": "spawn", "rows": <int>, "cols": <int>, "shell": "<path>", "strip_ansi": <bool>}
```

All fields optional. Defaults: rows=24, cols=80, shell="/bin/bash", strip_ansi=false.

### spawn_ok (response)

```json
{"type": "spawn_ok", "rows": <int>, "cols": <int>, "pid": <int>, "strip_ansi": <bool>}
```

### output (server push event)

```json
{"type": "output", "data": "<base64>", "encoding": "base64"}
```

### input (request)

```json
{"type": "input", "data": "<base64>"}
```

Input data is base64-decoded and written to PTY master.

### resize (request)

```json
{"type": "resize", "rows": <int>, "cols": <int>}
```

### resize_ok (response)

```json
{"type": "resize_ok", "rows": <int>, "cols": <int>}
```

### signal (request)

```json
{"type": "signal", "signal": "<signal-name>"}
```

### signal_ok (response)

```json
{"type": "signal_ok", "signal": "<signal-name>"}
```

### close (request)

```json
{"type": "close"}
```

### exit (server push event)

```json
{"type": "exit", "exit_code": <int>}
```

### Supported PTY signals

SIGHUP, SIGINT, SIGQUIT, SIGKILL, SIGTERM, SIGSTOP, SIGTSTP, SIGCONT, SIGWINCH, SIGUSR1, SIGUSR2, SIGPIPE, SIGALRM, SIGCHLD

---

## COMMAND AST Serialization

Bidirectional: `cmd_serialize()` (COMMAND → JSON) and `cmd_deserialize()` (JSON → COMMAND).

### Command types

| Type | cm_ constant | JSON "type" value |
|------|-------------|-------------------|
| Simple command | cm_simple | "simple" |
| Connection | cm_connection | "connection" |
| For loop | cm_for | "for" |
| Select | cm_select | "select" |
| If | cm_if | "if" |
| While | cm_while | "while" |
| Until | cm_until | "until" |
| Case | cm_case | "case" |
| Group | cm_group | "group" |
| Subshell | cm_subshell | "subshell" |
| Function def | cm_function_def | "function_def" |
| Arithmetic | cm_arith | "arith" |
| Conditional | cm_cond | "cond" |
| Arithmetic for | cm_arith_for | "arith_for" |
| Coproc | cm_coproc | "coproc" |

### Connector values (for cm_connection)

| Connector | Integer value |
|-----------|--------------|
| `;` | 59 |
| `\|` | 124 |
| `&` | 38 |
| `&&` | 288 |
| `\|\|` | 289 |

### WORD schema

```json
{"word": "<string>", "flags": <int>}
```

### WORD_LIST schema

JSON array of WORD objects.

### REDIRECT schema

```json
{
  "instruction": <int>,
  "redirector": <int>,
  "flags": <int>,
  "rflags": <int>,
  "filename": "<string>",
  "dest": <int>,
  "here_doc_eof": "<string>"
}
```

`filename` and `dest` are mutually exclusive. `here_doc_eof` is optional.

### PATTERN_LIST schema (for cm_case)

```json
{"patterns": [<WORD_LIST>], "action": <COMMAND>, "flags": <int>}
```

---

## CLI Options

| Short | Long | Argument | Default | Description |
|-------|------|----------|---------|-------------|
| -s | --socket | PATH | (resolved) | Unix socket path |
| -d | --daemon | — | off | Daemonize (fork to background) |
| -p | --pidfile | PATH | none | Write PID file |
| -m | --max-clients | N | 10 | Maximum concurrent clients |
| -P | --no-peercred | — | off | Disable SO_PEERCRED handshake |
| -l | --login | — | off | Full login shell initialization |
| — | --norc | — | off | Skip ~/.bashrc |
| — | --noprofile | — | off | Skip /etc/profile and ~/.bash_profile |
| -I | --init | SCRIPT | none | Source additional init script |
| -S | --stdio | — | off | Use stdin/stdout transport |
| -f | --fd | N | -1 | Use inherited file descriptor |
| -A | --auth-fd | N | stderr | Write token to this fd |
| -W | --named-pipe | NAME | none | Windows Named Pipe transport (Cygwin only) |
| -v | --verbose | — | off | Verbose logging to stderr |
| -h | --help | — | — | Print usage and exit |
| -V | --version | — | — | Print version and exit |

## Socket Path Resolution Order

1. `--socket` CLI option
2. `$BASH_SERVER_SOCKET` environment variable
3. `~/.bash-serverrc` config file (socket_path key)
4. `$XDG_RUNTIME_DIR/bash-server/sock`
5. `/tmp/bash-server-<uid>/sock`

## Token Delivery

| Transport | Token delivery method |
|-----------|---------------------|
| Unix socket | Written to `.token` file next to socket |
| stdio | Written to `--auth-fd` (default: stderr) as `TOKEN <hex>\n` |
| fd | Written to `--auth-fd` (default: stderr) as `TOKEN <hex>\n` |
| Named pipe | Written to `$XDG_RUNTIME_DIR/bash-server/<name>.token` or `/tmp/bash-server-<uid>/<name>.token` |

---

# Error Catalog — bash-server

Every error string emitted by bash-server, organized by source.

## Connection and Authentication Errors

| Error String | Source | Cause | Recovery |
|-------------|--------|-------|----------|
| `ERR token required` | v1 AUTH | AUTH command sent without token argument | Resend AUTH with hex token |
| `ERR invalid token` | v1 AUTH | Token does not match server token | Use correct 64-char hex token |
| `ERR not authenticated` | v1 session | Command sent before successful AUTH | Send AUTH first |
| `ERR invalid command` | v1 session | Empty or unparseable command line | Send valid command |
| `ERR unknown command: <cmd>` | v1 session | Unrecognized v1 command verb | Use supported command |

## v2 Channel 0: CONTROL Errors

| Error String | Cause | Recovery |
|-------------|-------|----------|
| `missing type field` | JSON object lacks "type" key | Include "type" in request |
| `token required` | auth request lacks "token" key | Include "token" in auth request |
| `invalid token` | Token does not match server token | Use correct 64-char hex token |
| `unknown control message type` | Unrecognized type value on CHAN_CONTROL | Use: auth, ping, disconnect, configure |
| `unknown channel` | Frame received on channel > CHAN_MAX | Use channels 0–5 only |

## v2 Channel 1: COMMAND Errors

| Error String | Cause | Recovery |
|-------------|-------|----------|
| `not authenticated` | Command sent before auth_ok | Authenticate on CHAN_CONTROL first |
| `missing type field` | JSON object lacks "type" key | Include "type" in request |
| `command required` | eval request lacks "command" key | Include "command" in eval request |
| `temp file creation failed` | mkstemp() failed for output capture | Check /tmp permissions and disk space |
| `dup failed` | dup() failed during output capture | Check file descriptor limits |
| `out of memory` | strdup() failed for command copy | Reduce memory usage |
| `unknown command type` | Unrecognized type value on CHAN_COMMAND | Use: eval |

## v2 Channel 2: STATE Errors

| Error String | Cause | Recovery |
|-------------|-------|----------|
| `not authenticated` | State request before auth_ok | Authenticate first |
| `missing type field` | JSON object lacks "type" key | Include "type" in request |
| `missing target field` | get/set/unset lacks "target" key | Include "target": var, function, or alias |
| `missing name field` | get/set/unset lacks "name" key | Include "name" of target |
| `missing value field` | set request lacks "value" key | Include "value" for set operations |
| `missing query field` | inspect request lacks "query" key | Include "query": vars, functions, aliases, or traps |
| `unknown target` | get target is not var, function, or alias | Use supported target value |
| `unknown target for set` | set target is not var or alias | Functions cannot be set via CHAN_STATE |
| `unknown target for unset` | unset target is not var, function, or alias | Use supported target value |
| `pipe creation failed` | pipe() failed for v1 handler capture | Check file descriptor limits |
| `internal error reading response` | protocol_read_line from pipe failed | Retry or reconnect |
| `unexpected response format` | v1 handler returned unrecognized format | Internal error; report bug |
| `set failed` | v1 handler returned non-OK, non-ERR | Check variable name/value validity |
| `unset failed` | v1 unset handler returned non-OK | Check target exists |
| `out of memory` | malloc failed during inspect | Reduce memory usage |
| `unknown state message type` | Unrecognized type on CHAN_STATE | Use: get, set, unset, inspect |

## v1 State Operation Errors

| Error String | Command | Cause | Recovery |
|-------------|---------|-------|----------|
| `ERR variable name required` | GET-VAR, SET-VAR, UNSET-VAR | Missing argument | Provide variable name |
| `ERR variable not found: <name>` | GET-VAR | Variable does not exist | Check variable name |
| `ERR <name>: readonly variable` | UNSET-VAR | Cannot unset readonly variable | Variable is immutable |
| `ERR bind_variable failed for <name>` | SET-VAR | Internal bind failure | Check name validity |
| `ERR encoding failed` | GET-VAR, GET-FUNC, GET-ALIAS | base64 encoding returned NULL | Internal error; report bug |
| `ERR function name required` | GET-FUNC, UNSET-FUNC | Missing argument | Provide function name |
| `ERR function not found: <name>` | GET-FUNC, UNSET-FUNC | Function does not exist | Check function name |
| `ERR cannot get function definition: <name>` | GET-FUNC | named_function_string returned NULL | Internal error |
| `ERR alias name required` | GET-ALIAS, SET-ALIAS, UNSET-ALIAS | Missing argument | Provide alias name |
| `ERR alias not found: <name>` | GET-ALIAS, UNSET-ALIAS | Alias does not exist | Check alias name |
| `ERR alias value required` | SET-ALIAS | Missing value after name | Provide alias value |
| `ERR signal name required` | SET-TRAP, UNSET-TRAP | Missing argument | Provide signal name |
| `ERR unknown signal: <name>` | SET-TRAP, UNSET-TRAP | decode_signal returned NO_SIG | Use valid signal name (e.g., SIGINT, INT, EXIT) |
| `ERR trap command required` | SET-TRAP | Missing command after signal | Provide trap command |
| `ERR inspect target required (vars, functions, aliases, traps)` | INSPECT | Missing argument | Provide inspect target |
| `ERR unknown inspect target: <target>` | INSPECT | Target not in supported set | Use: vars, functions, aliases, traps |

## v2 Channel 3: OBSERVE Errors

| Error String | Cause | Recovery |
|-------------|-------|----------|
| `not authenticated` | Subscribe before auth_ok | Authenticate first |
| `unknown observe message type` | Unrecognized type on CHAN_OBSERVE | Use: subscribe, unsubscribe |

## v2 Channel 4: DEBUG Errors

| Error String | Cause | Recovery |
|-------------|-------|----------|
| `missing type field` | JSON object lacks "type" key | Include "type" in request |
| `failed to add breakpoint` | calloc or strdup failed | Reduce memory usage |
| `unknown debug command` | Unrecognized type on CHAN_DEBUG | Use supported debug command type |

## v2 Channel 5: PTY Errors

| Error String | Cause | Recovery |
|-------------|-------|----------|
| `not authenticated` | PTY request before auth_ok | Authenticate first |
| `send 'spawn' first to start PTY session` | Non-spawn message on CHAN_PTY without active PTY | Send spawn request first |
| `forkpty failed` | forkpty() system call failed | Check PTY availability |
| `unknown signal: <name>` | Signal name not in supported set | Use supported signal name |
| `no active pty` | Signal/resize/close sent without active PTY | Spawn PTY first |

## Transport Errors

### Unix Socket

| Error String | Cause | Recovery |
|-------------|-------|----------|
| `CreateNamedPipe failed (error <N>)` | Win32 CreateNamedPipe failure | Check pipe name and permissions |
| `ConnectNamedPipe failed (error <N>)` | Client connection failed | Retry connection |
| `cygwin_attach_handle_to_fd failed: <msg>` | HANDLE-to-fd conversion failed | Check Cygwin compatibility |
| `CreateEvent failed (error <N>)` | Win32 event creation failed | System resource exhaustion |
| `CreateThread failed (error <N>)` | Helper thread creation failed | System resource exhaustion |
| `failed to create security descriptor (error <N>)` | SDDL parsing failed | Internal error |

### Named Pipe Token

| Error String | Cause | Recovery |
|-------------|-------|----------|
| `cannot create <dir>: <msg>` | Token directory creation failed | Check parent directory permissions |

## Execution Errors

| Error String | Cause | Recovery |
|-------------|-------|----------|
| `ERR temp file creation failed: <msg>` | mkstemp failed | Check /tmp permissions |
| `ERR dup failed: <msg>` | dup() failed | Check file descriptor limits |
| `ERR redirect failed: <msg>` | dup2() failed during capture | Check file descriptor limits |
| `ERR out of memory` | strdup/malloc failed | Reduce memory usage |

---

# Integration Patterns — bash-server

Step-by-step sequences for common integration tasks.

## Transport Selection

| Transport | Flag | Connection method | Auth token source | Use case |
|-----------|------|-------------------|-------------------|----------|
| Unix socket | (default) | connect() to socket path | `.token` file next to socket | Multi-client daemon |
| stdio | `--stdio` | stdin/stdout of spawned process | `--auth-fd` (default stderr) reads `TOKEN <hex>\n` | Subprocess embedding |
| fd | `--fd N` | Inherited file descriptor N | `--auth-fd` (default stderr) reads `TOKEN <hex>\n` | Pre-connected fd passing |
| Named pipe | `--named-pipe NAME` | CreateFile on `\\.\pipe\bash-server-<NAME>` | Token file at `$XDG_RUNTIME_DIR/bash-server/<NAME>.token` or `/tmp/bash-server-<uid>/<NAME>.token` | Cygwin/Windows native |

## Token Acquisition

### Unix socket mode

1. Read socket path (see socket path resolution in api-reference.md)
2. Read token from `<socket-dir>/.token` file
3. Token is 64 hex characters (no newline)

### stdio / fd mode

1. Spawn bash-server with `--stdio` or `--fd N`
2. Read line from auth-fd (default: stderr)
3. Parse: `TOKEN <64-hex-chars>\n`
4. Strip prefix and newline to get token

### Named pipe mode (Cygwin)

1. Read token file at `$XDG_RUNTIME_DIR/bash-server/<name>.token` or `/tmp/bash-server-<uid>/<name>.token`
2. Token is 64 hex characters

## v1 Authentication Sequence

1. Connect to transport
2. Send: `AUTH <64-hex-token>\n`
3. Read line
4. If starts with `OK`: authenticated
5. If starts with `ERR`: authentication failed

## v2 Authentication Sequence (Binary)

1. Connect to transport
2. Build JSON payload: `{"type":"auth","token":"<64-hex-token>"}`
3. Build 6-byte header: channel=0, flags=0, length=payload.length (network byte order)
4. Write header + payload
5. Read 6-byte response header
6. Read response payload (length from header)
7. Parse JSON: check `type` field
8. If `type` is `auth_ok`: authenticated; `capabilities` array lists available features
9. If `type` is `error`: authentication failed; `message` field has reason

## v2 Authentication Sequence (NDJSON)

1. Connect to transport
2. Send: `{"ch":0,"type":"auth","token":"<64-hex-token>"}\n`
3. Read line (until `\n`)
4. Parse JSON: extract `ch` and `type` fields
5. If `ch`=0 and `type`=`auth_ok`: authenticated
6. If `ch`=0 and `type`=`error`: authentication failed

## v2 Command Execution Sequence

1. Authenticate (see above)
2. Send on CHAN_COMMAND (channel 1): `{"type":"eval","command":"<shell-command>","id":"<optional-id>"}`
3. Read frames until `type`=`complete`:
   - `type`=`stdout`: base64-decode `data` field for stdout
   - `type`=`stderr`: base64-decode `data` field for stderr
   - `type`=`complete`: `exit_code` field has the exit status
4. If `id` was provided, match responses by `id` field

## v2 Channel Multiplexing

1. Authenticate on CHAN_CONTROL (channel 0)
2. All subsequent frames are routed by channel ID
3. Binary: channel is byte 0 of 6-byte header
4. NDJSON: channel is `ch` field in JSON
5. Multiple channels can be active concurrently
6. Server pushes events on CHAN_OBSERVE (3) and CHAN_PTY (5) without client requests

## v2 Observe Subscription Sequence

1. Authenticate
2. Send on CHAN_OBSERVE (channel 3): `{"type":"subscribe","level":1}`
3. Read response: `{"type":"subscribed","level":1}`
4. Server pushes `pre_command` and `post_command` events on CHAN_OBSERVE during command execution
5. Events arrive interleaved with other channel traffic
6. To stop: send `{"type":"unsubscribe"}` on CHAN_OBSERVE
7. Alternative: send configure on CHAN_CONTROL: `{"type":"configure","observability":1}`

## v2 Debug Session Sequence

1. Authenticate
2. Send on CHAN_DEBUG (channel 4): `{"type":"enable"}`
3. Set breakpoints: `{"type":"break","kind":"command","pattern":"<substring>"}`
4. Execute commands on CHAN_COMMAND as normal
5. When breakpoint hits, server sends `break_hit` on CHAN_DEBUG and blocks
6. While paused, send on CHAN_DEBUG:
   - `{"type":"inspect_ast"}` to get AST of current command
   - `{"type":"list"}` to list breakpoints
   - `{"type":"continue"}` to resume
   - `{"type":"step"}` to step into
   - `{"type":"next"}` to step over
   - `{"type":"finish"}` to step out
7. To disable: `{"type":"disable"}` (removes all breakpoints)

## v2 PTY Session Sequence

1. Authenticate
2. Send on CHAN_PTY (channel 5): `{"type":"spawn","rows":24,"cols":80}`
3. Read response: `{"type":"spawn_ok","pid":<N>,"rows":24,"cols":80}`
4. PTY relay loop:
   - Read `output` events from CHAN_PTY: base64-decode `data` for terminal output
   - Send `input` messages on CHAN_PTY: base64-encode keyboard input in `data`
5. Resize: send `{"type":"resize","rows":<N>,"cols":<N>}` on CHAN_PTY
6. Signal: send `{"type":"signal","signal":"SIGINT"}` on CHAN_PTY
7. Close: send `{"type":"close"}` or wait for `exit` event
8. After PTY exits, normal frame loop resumes on all channels

## Client Libraries

| Language | Path |
|----------|------|
| Go | [clients/go/README.md](../../../bash-server/clients/go/README.md) |
| C# | [clients/csharp/README.md](../../../bash-server/clients/csharp/README.md) |
| Python | [clients/python/README.md](../../../bash-server/clients/python/README.md) |
| TypeScript | [clients/typescript/README.md](../../../bash-server/clients/typescript/README.md) |
| C | [clients/c/README.md](../../../bash-server/clients/c/README.md) |
| Java | [clients/java/README.md](../../../bash-server/clients/java/README.md) |

