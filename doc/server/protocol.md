# bash-server Wire Protocol Specification

**Version:** 2.0
**Transport:** Unix domain socket, stdio, fd, Windows Named Pipes
**Protocol versions:** v1 (text), v2 (binary JSON frames), v2/NDJSON

## Protocol Overview

bash-server supports three protocol versions, auto-detected from the first
byte of the connection:

| Version | Wire format | Description |
|---------|------------|-------------|
| v1 | Line-oriented text, LF-terminated | Original protocol. AUTH/EVAL/PING/QUIT plus state commands. |
| v2 (binary) | 6-byte header + JSON payload | Channel-multiplexed JSON frames with flags. |
| v2 (NDJSON) | Newline-delimited JSON | One JSON object per line with `"ch"` field for channel routing. |

Both v2 wire formats share the same channel architecture and message types;
they differ only in framing.

### Auto-Detection Algorithm

The server detects the protocol version by peeking at the first byte of the
connection using `recv(fd, &byte, 1, MSG_PEEK)`:

```
First byte:
  '{' (0x7B)        -> NDJSON (PROTOCOL_V2_NDJSON)
  0x00-0x05         -> Binary v2 (channel ID byte)
  >= 0x20 (other)   -> v1 text (first byte of AUTH/EVAL/etc.)
```

This is unambiguous because:
- No v1 command starts with `{` (they start with `A`, `E`, `P`, `Q`, `G`, `S`, `U`, `I`)
- Channel IDs 0-5 are all below ASCII space (0x20)
- v1 commands are all printable ASCII

Detection occurs once per connection.  The detected version applies for the
entire session lifetime.

## Protocol v1 (Text)

### Encoding

- **Character encoding:** UTF-8 text
- **Line terminator:** `\n` (LF).  `\r` (CR) characters are silently stripped on input.
- **Payload encoding:** Base64 (RFC 4648) for binary data in responses.

### Connection Lifecycle

```
Client                              Server
  |                                    |
  |--- connect() ------------------->  |
  |                                    |  (accept, auto-detect v1)
  |--- AUTH <token>\n -------------->  |
  |<-- OK\n -------------------------  |  (or ERR <reason>\n)
  |                                    |
  |--- EVAL <command>\n ------------>  |
  |<-- STDOUT [<base64>]\n ----------  |
  |<-- STDERR [<base64>]\n ----------  |
  |<-- EXIT <code>\n ----------------  |
  |                                    |
  |--- PING\n ---------------------->  |
  |<-- PONG\n -----------------------  |
  |                                    |
  |--- QUIT\n ---------------------->  |
  |<-- BYE\n ------------------------  |
  |                                    |
  |        (connection closed)         |
```

### Session State Machine

```
          connect()
              |
              v
      +-------------------+
      | UNAUTHENTICATED   |
      +--------+----------+
               |
          AUTH (valid)
               |
               v
      +-------------------+
      | AUTHENTICATED     |<---- EVAL / PING / State ops
      +--------+----------+
               |
             QUIT
               |
               v
      +-------------------+
      |   CLOSED          |
      +-------------------+
```

**State transitions:**

| Current State | Command | Valid | Next State |
|--------------|---------|-------|------------|
| UNAUTHENTICATED | `AUTH` (valid) | Yes | AUTHENTICATED |
| UNAUTHENTICATED | `AUTH` (invalid) | Yes | UNAUTHENTICATED |
| UNAUTHENTICATED | `PING` | Yes | UNAUTHENTICATED |
| UNAUTHENTICATED | `EVAL` | Yes (error) | UNAUTHENTICATED |
| UNAUTHENTICATED | `QUIT` | Yes | CLOSED |
| AUTHENTICATED | `AUTH` | Yes (no-op) | AUTHENTICATED |
| AUTHENTICATED | `EVAL` | Yes | AUTHENTICATED |
| AUTHENTICATED | `PING` | Yes | AUTHENTICATED |
| AUTHENTICATED | State commands | Yes | AUTHENTICATED |
| AUTHENTICATED | `QUIT` | Yes | CLOSED |
| Any | (EOF/disconnect) | -- | CLOSED |
| Any | (unknown command) | Yes (error) | (unchanged) |

### Message Format

**Request format:**
```
COMMAND [ARGUMENT]\n
```

- **COMMAND**: Case-insensitive ASCII keyword (max 31 characters).
- **ARGUMENT**: Optional.  Everything after the first whitespace through end of line.  Max 65,536 bytes.

**Response format:**
```
STATUS [PAYLOAD]\n
```

- **STATUS**: Uppercase ASCII keyword.
- **PAYLOAD**: Optional, status-specific data.  May be base64-encoded.

### Commands

#### AUTH -- Authenticate

```
AUTH <token>
```

| Response | Condition |
|----------|-----------|
| `OK` | Authentication successful |
| `OK already authenticated` | Session already authenticated (idempotent) |
| `ERR token required` | No token provided |
| `ERR invalid token` | Token does not match |

On first successful AUTH, the server initializes the Bash interpreter.
Token comparison uses a constant-time algorithm.

#### EVAL -- Evaluate Command

```
EVAL <command>
```

Response (three lines):
```
STDOUT [<base64-encoded-stdout>]
STDERR [<base64-encoded-stderr>]
EXIT <exit-code>
```

Each EVAL forks a child process for isolation.  Output per stream is capped
at 1 MB (`SERVER_MAX_OUTPUT`).

#### PING -- Health Check

```
PING
```

Response: `PONG`.  Available in any state.

#### QUIT -- Disconnect

```
QUIT
```

Response: `BYE`.  Server closes the connection.

#### State Commands (v1)

These commands require authentication and operate on shell state directly
(no fork):

| Command | Format | Response |
|---------|--------|----------|
| `GET-VAR <name>` | Get variable | `VALUE <name> <base64> [attrs]` or `ERR` |
| `SET-VAR <name> <value> [--export] [--readonly] [--integer]` | Set variable | `OK` or `ERR` |
| `UNSET-VAR <name>` | Remove variable | `OK` or `ERR` |
| `GET-FUNC <name>` | Get function def | `FUNC <name> <base64>` or `ERR` |
| `UNSET-FUNC <name>` | Remove function | `OK` or `ERR` |
| `GET-ALIAS <name>` | Get alias | `ALIAS <name> <base64>` or `ERR` |
| `SET-ALIAS <name> <value>` | Set alias | `OK` or `ERR` |
| `UNSET-ALIAS <name>` | Remove alias | `OK` or `ERR` |
| `SET-TRAP <signal> <command>` | Set trap | `OK` or `ERR` |
| `UNSET-TRAP <signal>` | Remove trap | `OK` or `ERR` |
| `INSPECT <target> [pattern]` | List items | Multi-line response + `INSPECT-END` |

INSPECT targets: `vars`, `functions`, `aliases`, `traps`.

### Response Codes Summary (v1)

| Code | Meaning | Context |
|------|---------|---------|
| `OK` | Success | AUTH, SET-VAR, SET-ALIAS, etc. |
| `ERR` | Error (followed by reason) | Any command |
| `PONG` | Health check response | PING |
| `BYE` | Session closing | QUIT |
| `STDOUT` | Command stdout output (base64) | EVAL |
| `STDERR` | Command stderr output (base64) | EVAL |
| `EXIT` | Command exit code | EVAL |
| `VALUE` | Variable value (base64) | GET-VAR, INSPECT vars |
| `FUNC` | Function definition (base64) | GET-FUNC, INSPECT functions |
| `ALIAS` | Alias value (base64) | GET-ALIAS, INSPECT aliases |
| `TRAP` | Trap command (base64) | INSPECT traps |
| `INSPECT-END` | End of inspect listing | INSPECT |

### Base64 Encoding

Output payloads use standard Base64 encoding per RFC 4648:

- Alphabet: `A-Za-z0-9+/`
- Padding: `=` (standard padding)
- No line wrapping (single contiguous string)

## Protocol v2 (JSON Frames)

Protocol v2 uses channel-multiplexed JSON messages.  Two wire formats are
supported: binary framing and NDJSON.  Both carry the same JSON message types.

### Binary Wire Format

Each frame consists of a 6-byte header followed by a JSON payload:

```
+----------+----------+-------------------+------------------+
| channel  |  flags   |     length        |     payload      |
| (1 byte) | (1 byte) |    (4 bytes)      |    (N bytes)     |
+----------+----------+-------------------+------------------+
```

| Field | Size | Description |
|-------|------|-------------|
| `channel` | uint8 | Logical channel ID (0-5) |
| `flags` | uint8 | Frame flags (see below) |
| `length` | uint32 | Payload length in network byte order (big-endian) |
| `payload` | N bytes | UTF-8 JSON string |

**Frame flags:**

| Flag | Value | Description |
|------|-------|-------------|
| `COMPRESSED` | 0x01 | Payload is compressed (reserved, not implemented) |
| `BINARY` | 0x02 | Payload is binary, not JSON (reserved) |
| `CONTINUED` | 0x04 | More fragments follow (reserved) |
| `FINAL` | 0x08 | Last fragment (reserved) |

Maximum payload size: 1 MB (`FRAME_MAX_PAYLOAD`).

### NDJSON Wire Format

Each message is a single JSON line terminated by `\n`.  The channel is
carried inside the JSON object as the `"ch"` field:

```
{"ch":0,"type":"auth","token":"abc123..."}\n
{"ch":1,"type":"eval","command":"echo hello"}\n
```

The `"ch"` field maps to the channel IDs used in binary framing:

| ch | Channel |
|----|---------|
| 0 | CONTROL |
| 1 | COMMAND |
| 2 | STATE |
| 3 | OBSERVE |
| 4 | DEBUG |
| 5 | PTY |

If `"ch"` is omitted, CHAN_CONTROL (0) is assumed.

### Channel Architecture

Protocol v2 multiplexes messages across six logical channels:

| Channel | ID | Direction | Purpose |
|---------|----|-----------|---------|
| CONTROL | 0 | Bidirectional | Auth, configure, disconnect, ping |
| COMMAND | 1 | Bidirectional | EVAL execution, stdout/stderr/exit responses |
| STATE | 2 | Bidirectional | Variable/function/alias get/set/inspect |
| OBSERVE | 3 | Server-push | Pre/post command events (observability) |
| DEBUG | 4 | Bidirectional | Breakpoints, stepping, AST inspection |
| PTY | 5 | Bidirectional | Terminal I/O, resize, signal injection |

### Channel 0: CONTROL Messages

**Client to Server:**

| type | Fields | Description |
|------|--------|-------------|
| `auth` | `token` (string) | Authenticate with server token |
| `ping` | -- | Health check |
| `disconnect` | -- | Graceful disconnect |
| `configure` | `observability` (int) | Set observability level |

**Server to Client:**

| type | Fields | Description |
|------|--------|-------------|
| `auth_ok` | `capabilities` (array), `message` (optional) | Authentication successful |
| `pong` | -- | Ping response |
| `disconnect_ok` | -- | Disconnect acknowledged |
| `configured` | `observability` (int) | Configuration applied |
| `error` | `message` (string) | Error response |

**Auth response example:**
```json
{"type":"auth_ok","capabilities":["state","command","observe","debug"]}
```

### Channel 1: COMMAND Messages

**Client to Server:**

| type | Fields | Description |
|------|--------|-------------|
| `eval` | `command` (string), `id` (optional string) | Execute bash command |

**Server to Client:**

| type | Fields | Description |
|------|--------|-------------|
| `stdout` | `data` (base64 string), `encoding`, `id` (optional) | Command stdout |
| `stderr` | `data` (base64 string), `encoding`, `id` (optional) | Command stderr |
| `complete` | `exit_code` (int), `id` (optional) | Command completion |
| `error` | `message` (string) | Error response |

The optional `id` field correlates responses with requests when multiple
commands are in flight.

**Example exchange:**
```json
-> {"ch":1,"type":"eval","command":"echo hello","id":"req-1"}
<- {"ch":1,"type":"stdout","id":"req-1","data":"aGVsbG8K","encoding":"base64"}
<- {"ch":1,"type":"stderr","id":"req-1","data":"","encoding":"base64"}
<- {"ch":1,"type":"complete","id":"req-1","exit_code":0}
```

### Channel 2: STATE Messages

**Client to Server:**

| type | Fields | Description |
|------|--------|-------------|
| `get` | `target` (var/function/alias), `name` | Get state item |
| `set` | `target` (var/alias), `name`, `value`, `attributes` (optional array) | Set state item |
| `unset` | `target` (var/function/alias), `name` | Remove state item |
| `inspect` | `query` (vars/functions/aliases/traps) | List all items of a type |

**Server to Client:**

| type | Fields | Description |
|------|--------|-------------|
| `value` | `target`, `name`, `value`, `attributes` (optional array) | State item value |
| `set_ok` | `target`, `name` | Set succeeded |
| `unset_ok` | `target`, `name` | Unset succeeded |
| `inspect_result` | `query`, `data` (array of items) | Inspect listing |
| `error` | `message` | Error response |

Variable attributes: `exported`, `readonly`, `integer`, `local`, `array`,
`assoc`, `nameref`, `uppercase`, `lowercase`, `capcase`, `trace`.

**Example:**
```json
-> {"ch":2,"type":"get","target":"var","name":"PATH"}
<- {"ch":2,"type":"value","target":"var","name":"PATH","value":"/usr/bin:/bin","attributes":["exported"]}
```

### Channel 3: OBSERVE Messages

**Client to Server:**

| type | Fields | Description |
|------|--------|-------------|
| `subscribe` | `level` (int, default 1) | Subscribe to events |
| `unsubscribe` | -- | Stop receiving events |

**Server to Client (push events):**

| type | Fields | Description |
|------|--------|-------------|
| `subscribed` | `level` (int) | Subscription confirmed |
| `unsubscribed` | -- | Unsubscription confirmed |
| `pre_command` | `seq`, `timestamp`, `data` (see below) | Before command executes |
| `post_command` | `seq`, `timestamp`, `data` (see below) | After command completes |

**Observability levels:**

| Level | Events |
|-------|--------|
| 0 | None (output only) |
| 1 | Pre/post command events with cwd, line, duration |

**pre_command data fields:** `command`, `cwd`, `line_number`, `is_subshell`, `is_async`

**post_command data fields:** `command`, `exit_status`, `signal_number` (if signaled), `duration_ms`

**Example:**
```json
<- {"ch":3,"level":1,"type":"pre_command","seq":0,"timestamp":1706644800000,
    "data":{"command":"echo hello","cwd":"/home/user","line_number":1,
            "is_subshell":false,"is_async":false}}
<- {"ch":3,"level":1,"type":"post_command","seq":1,"timestamp":1706644800005,
    "data":{"command":"echo hello","exit_status":0,"duration_ms":5}}
```

### Channel 4: DEBUG Messages

**Client to Server:**

| type | Fields | Description |
|------|--------|-------------|
| `enable` | -- | Enable debug mode |
| `disable` | -- | Disable debug mode (clears breakpoints) |
| `break` | `kind` (command/line/function), `pattern`, `line`, `condition` | Set breakpoint |
| `delete` | `id` (int) | Remove breakpoint |
| `enable_bp` | `id` (int) | Enable breakpoint |
| `disable_bp` | `id` (int) | Disable breakpoint |
| `list` | -- | List all breakpoints |
| `status` | -- | Query debug state |
| `step` | -- | Step into next command |
| `continue` | -- | Resume execution |
| `next` | -- | Step over (same depth) |
| `finish` | -- | Step out (one level up) |
| `skip` | -- | Skip current command (acknowledged but not enforced) |
| `inspect_ast` | -- | Serialize pending command AST to JSON |

**Server to Client:**

| type | Fields | Description |
|------|--------|-------------|
| `enable_ok` | -- | Debug mode enabled |
| `disable_ok` | -- | Debug mode disabled |
| `break_ok` | `id` (int) | Breakpoint set |
| `delete_ok` | `id`, `found` (bool) | Breakpoint deleted |
| `enable_bp_ok` | `id`, `found` (bool) | Breakpoint enabled |
| `disable_bp_ok` | `id`, `found` (bool) | Breakpoint disabled |
| `breakpoints` | `data` (array) | Breakpoint listing |
| `status` | `active`, `mode`, `breakpoints` (count), `depth` | Debug state |
| `step_ok` | `mode` | Step mode set |
| `continue_ok` | -- | Continuing |
| `break_hit` | `line`, `command`, `depth` | Breakpoint/step hit (paused) |
| `ast` | `data` (JSON object or null) | Serialized COMMAND tree |
| `error` | `message` | Error |

**Breakpoint kinds:**

| Kind | Matching |
|------|----------|
| `command` | Substring match on command string |
| `line` | Exact line number match |
| `function` | Substring match on function name |

When a breakpoint or step fires, the server sends `break_hit` and blocks
the execution thread until the client sends a resume command (`continue`,
`step`, `next`, `finish`, or `skip`).  While paused, the client can also
send `inspect_ast`, `list`, `break`, and `delete` messages.

### Channel 5: PTY Messages

**Client to Server:**

| type | Fields | Description |
|------|--------|-------------|
| `spawn` | `rows`, `cols`, `shell`, `strip_ansi` (bool) | Start PTY session |
| `input` | `data` (base64 string) | Send keyboard input |
| `resize` | `rows`, `cols` | Resize terminal |
| `signal` | `signal` (name or number) | Send signal to PTY child |
| `close` | -- | Close PTY session |

**Server to Client:**

| type | Fields | Description |
|------|--------|-------------|
| `spawn_ok` | `rows`, `cols`, `pid`, `strip_ansi` (optional) | PTY spawned |
| `output` | `data` (base64 string), `encoding` | Terminal output |
| `resize_ok` | `rows`, `cols` | Resize applied |
| `signal_ok` | `signal` | Signal sent |
| `exit` | `exit_code` | PTY child exited |
| `error` | `message` | Error |

After `spawn`, the server enters a relay loop: PTY output is sent as `output`
frames, and client `input` frames are forwarded to the PTY master fd.  The
relay uses `select()` for bidirectional multiplexing.

The `strip_ansi` option enables server-side stripping of ANSI escape sequences
from PTY output, using a stateful finite-state machine that handles CSI, OSC,
two-character escapes, charset designators, and 8-bit CSI (0x9B).

Signal names: `SIGHUP`, `SIGINT`, `SIGQUIT`, `SIGKILL`, `SIGTERM`, `SIGSTOP`,
`SIGTSTP`, `SIGCONT`, `SIGWINCH`, `SIGUSR1`, `SIGUSR2`, `SIGPIPE`, `SIGALRM`,
`SIGCHLD`.  Numeric values also accepted.

## Buffer Limits

| Constant | Value | Purpose |
|----------|-------|---------|
| `SERVER_MAX_LINE` | 8,192 bytes | Maximum v1 protocol line length |
| `SERVER_MAX_TOKEN` | 256 bytes | Maximum token buffer |
| `SERVER_MAX_CMD` | 65,536 bytes | Maximum command string |
| `SERVER_MAX_OUTPUT` | 1,048,576 bytes (1 MB) | Maximum captured output per stream |
| `FRAME_HEADER_SIZE` | 6 bytes | v2 binary frame header |
| `FRAME_MAX_PAYLOAD` | 1,048,576 bytes (1 MB) | Maximum v2 frame payload |

## Error Handling

### Connection Errors
- If the client disconnects mid-session, `protocol_read_line()` (v1) or
  `json_frame_read()` (v2) returns -1 (EOF), causing the session handler
  to exit its command loop and clean up.
- Write errors (broken pipe) are detected and propagated as -1 returns.
  SIGPIPE is ignored at the process level.

### Protocol Errors
- Unknown commands (v1): `ERR unknown command: <CMD>`
- Empty lines (v1): `ERR invalid command`
- Unknown channel (v2): error frame on CHAN_CONTROL
- Missing `type` field (v2): error frame on the relevant channel
- Unknown message type (v2): error frame on the relevant channel

### Concurrency
- The server is single-threaded per session (fork-per-session model).
- Multiple concurrent sessions run in separate child processes.
- The `max_clients` limit (default 10) caps concurrent sessions.
- Named pipe transport handles clients sequentially (no fork).

## Wire Examples

### v1: Successful Command Execution

```
-> AUTH a1b2c3d4...  (64 hex chars)
<- OK
-> EVAL echo hello
<- STDOUT aGVsbG8K
<- STDERR
<- EXIT 0
-> QUIT
<- BYE
```

### v1: State Operations

```
-> AUTH a1b2c3d4...
<- OK
-> SET-VAR FOO bar --export
<- OK
-> GET-VAR FOO
<- VALUE FOO YmFy exported
-> INSPECT vars FO
<- VALUE FOO YmFy exported
<- INSPECT-END
-> QUIT
<- BYE
```

### v2 Binary: Auth and Eval

```
-> [ch=0][flags=0][len=43] {"type":"auth","token":"a1b2c3d4..."}
<- [ch=0][flags=0][len=68] {"type":"auth_ok","capabilities":["state","command","observe","debug"]}
-> [ch=1][flags=0][len=38] {"type":"eval","command":"echo hello"}
<- [ch=1][flags=0][len=54] {"type":"stdout","data":"aGVsbG8K","encoding":"base64"}
<- [ch=1][flags=0][len=46] {"type":"stderr","data":"","encoding":"base64"}
<- [ch=1][flags=0][len=30] {"type":"complete","exit_code":0}
-> [ch=0][flags=0][len=22] {"type":"disconnect"}
<- [ch=0][flags=0][len=24] {"type":"disconnect_ok"}
```

### v2 NDJSON: Full Session

```
-> {"ch":0,"type":"auth","token":"a1b2c3d4..."}\n
<- {"ch":0,"type":"auth_ok","capabilities":["state","command","observe","debug"]}\n
-> {"ch":2,"type":"get","target":"var","name":"HOME"}\n
<- {"ch":2,"type":"value","target":"var","name":"HOME","value":"/home/user"}\n
-> {"ch":1,"type":"eval","command":"ls /tmp","id":"cmd-1"}\n
<- {"ch":1,"type":"stdout","id":"cmd-1","data":"...","encoding":"base64"}\n
<- {"ch":1,"type":"stderr","id":"cmd-1","data":"","encoding":"base64"}\n
<- {"ch":1,"type":"complete","id":"cmd-1","exit_code":0}\n
-> {"ch":0,"type":"disconnect"}\n
<- {"ch":0,"type":"disconnect_ok"}\n
```

### v2 NDJSON: Debug Session

```
-> {"ch":4,"type":"enable"}\n
<- {"ch":4,"type":"enable_ok"}\n
-> {"ch":4,"type":"break","kind":"command","pattern":"echo"}\n
<- {"ch":4,"type":"break_ok","id":1}\n
-> {"ch":1,"type":"eval","command":"echo hello; echo world"}\n
<- {"ch":4,"type":"break_hit","line":1,"command":"echo hello","depth":0}\n
-> {"ch":4,"type":"inspect_ast"}\n
<- {"ch":4,"type":"ast","data":{...}}\n
-> {"ch":4,"type":"continue"}\n
<- {"ch":4,"type":"break_hit","line":1,"command":"echo world","depth":0}\n
-> {"ch":4,"type":"continue"}\n
<- {"ch":1,"type":"stdout","data":"aGVsbG8Kd29ybGQK","encoding":"base64"}\n
<- {"ch":1,"type":"stderr","data":"","encoding":"base64"}\n
<- {"ch":1,"type":"complete","exit_code":0}\n
```

### v2 NDJSON: PTY Session

```
-> {"ch":5,"type":"spawn","rows":24,"cols":80,"strip_ansi":true}\n
<- {"ch":5,"type":"spawn_ok","rows":24,"cols":80,"pid":12345,"strip_ansi":true}\n
<- {"ch":5,"type":"output","data":"...base64 prompt...","encoding":"base64"}\n
-> {"ch":5,"type":"input","data":"bHMK"}\n
<- {"ch":5,"type":"output","data":"...base64 ls output...","encoding":"base64"}\n
-> {"ch":5,"type":"resize","rows":30,"cols":120}\n
<- {"ch":5,"type":"resize_ok","rows":30,"cols":120}\n
-> {"ch":5,"type":"signal","signal":"SIGINT"}\n
<- {"ch":5,"type":"signal_ok","signal":"SIGINT"}\n
-> {"ch":5,"type":"close"}\n
<- {"ch":5,"type":"exit","exit_code":0}\n
```
