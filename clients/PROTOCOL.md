# v2 NDJSON Protocol Reference (Client Perspective)

This document specifies the bash-server v2 NDJSON wire protocol from
the client's point of view. It covers frame format, channel routing,
message types, authentication, and implementation guidance.

For the authoritative server-side protocol specification, see
`doc/server/protocol.md`.

---

## Table of Contents

- [Overview](#overview)
- [Frame Format](#frame-format)
- [Channel Routing](#channel-routing)
- [Message Structure](#message-structure)
- [Authentication Flow](#authentication-flow)
- [Channel 0: CONTROL](#channel-0-control)
- [Channel 1: COMMAND](#channel-1-command)
- [Channel 2: STATE](#channel-2-state)
- [Channel 3: OBSERVE](#channel-3-observe)
- [Channel 4: DEBUG](#channel-4-debug)
- [Channel 5: PTY](#channel-5-pty)
- [Base64 Encoding](#base64-encoding)
- [Error Responses](#error-responses)
- [Request/Response vs Server-Push](#requestresponse-vs-server-push)
- [Wire Examples](#wire-examples)
- [Implementation Guide](#implementation-guide)
- [Protocol Negotiation](#protocol-negotiation)
- [Limits and Constraints](#limits-and-constraints)

---

## Overview

bash-server v2 supports two wire formats:

1. **Binary framing**: 6-byte length-prefix header + JSON body
2. **NDJSON**: Newline-Delimited JSON (one JSON object per line)

All client bindings in this repository use **NDJSON exclusively**.
NDJSON is simpler to implement, human-readable, and sufficient for
the typical workload (shell command evaluation).

### Why NDJSON

- Human-readable: inspect traffic with `cat`, `jq`, `socat`
- Simple framing: each line is a complete message
- No binary header parsing or endianness concerns
- Efficient line-based I/O in all languages
- The bottleneck is command execution, not serialization

### Transport Independence

The NDJSON protocol is transport-independent. The same message format
works over:

- **Unix domain sockets**: `connect()` to a socket path
- **stdio**: stdin/stdout of a `bash-server --stdio` subprocess
- **File descriptors**: pre-existing read/write fd pair
- **Windows Named Pipes**: `\\.\pipe\<name>` (Cygwin/Windows)

The transport provides a bidirectional byte stream. The protocol layer
reads/writes NDJSON lines on that stream.

---

## Frame Format

Each frame is a single JSON object on one line, terminated by a
newline character (`\n`, byte 0x0A).

```
{"ch":0,"type":"auth","token":"abc123def456..."}\n
```

### Rules

1. Each frame is exactly one line (no embedded newlines)
2. The line terminator is `\n` (LF, 0x0A), not `\r\n`
3. The JSON must be valid UTF-8
4. No leading or trailing whitespace (except the terminating `\n`)
5. Use compact JSON (no pretty-printing, no extra spaces)
6. Maximum frame size: 1 MB (1,048,576 bytes including the `\n`)

### Encoding

To send a frame:

1. Construct a JSON object with at least `"ch"` and `"type"` fields
2. Serialize to a single-line JSON string (compact, no newlines)
3. Append `\n`
4. Encode as UTF-8 bytes
5. Write to the transport

### Decoding

To receive a frame:

1. Read bytes from the transport until `\n` is encountered
2. Decode UTF-8
3. Parse JSON
4. Extract the `"ch"` field to determine the channel
5. Extract the `"type"` field to determine the message type
6. Process remaining fields based on channel and type

---

## Channel Routing

Every message contains a `"ch"` field (integer 0-5) that identifies
which channel the message belongs to. The client must route incoming
messages to the appropriate handler based on this field.

### Channel Constants

| Channel        | ID | Direction           | Purpose                         |
|----------------|-----|---------------------|---------------------------------|
| `CHAN_CONTROL`  | 0  | Request/Response    | Auth, ping, configure, disconnect |
| `CHAN_COMMAND`  | 1  | Request/Response    | Command evaluation              |
| `CHAN_STATE`    | 2  | Request/Response    | Variable/function/alias/trap ops |
| `CHAN_OBSERVE`  | 3  | Req/Resp + Push     | Command observation events      |
| `CHAN_DEBUG`    | 4  | Req/Resp + Push     | Debug breakpoints and stepping  |
| `CHAN_PTY`      | 5  | Req/Resp + Push     | PTY terminal sessions           |

Channels 0-2 are purely request/response. Channels 3-5 also produce
server-push messages that arrive without a prior request.

---

## Message Structure

Every message has this minimum structure:

```json
{"ch": <int>, "type": "<string>", ...additional fields...}
```

| Field    | Type   | Required | Description                          |
|----------|--------|----------|--------------------------------------|
| `ch`     | int    | Yes      | Channel ID (0-5)                     |
| `type`   | string | Yes      | Message type name                    |
| (varies) | varies | Depends  | Additional fields per message type   |

The `type` field determines what additional fields are present. Each
channel has its own set of valid types.

---

## Authentication Flow

Authentication is the first exchange after connecting. The server will
reject all non-auth messages until authentication succeeds.

### Step 1: Client sends auth request

```json
{"ch":0,"type":"auth","token":"<64-hex-chars>"}
```

The token is a 256-bit (32-byte) value represented as 64 hexadecimal
characters. The token is generated by the server at startup and
printed to stderr (or delivered via `--auth-fd`).

### Step 2: Server responds

**Success**:

```json
{"ch":0,"type":"auth_ok","version":"5.1.16","protocol":2}
```

| Field      | Type   | Description                          |
|------------|--------|--------------------------------------|
| `version`  | string | Bash version (e.g., "5.1.16")       |
| `protocol` | int    | Protocol version (always 2)          |

**Failure**:

```json
{"ch":0,"type":"auth_fail","message":"authentication failed"}
```

| Field     | Type   | Description                           |
|-----------|--------|---------------------------------------|
| `message` | string | Human-readable error description      |

### Token Security

- The token is a shared secret between the server and client
- It is compared using constant-time comparison (no timing leak)
- It must be delivered securely (file permissions, fd inheritance)
- Re-authentication on an already-authenticated session is an error

---

## Channel 0: CONTROL

Control channel handles authentication (above), ping, configuration,
and disconnect.

### ping

**Request**:

```json
{"ch":0,"type":"ping"}
```

**Response**:

```json
{"ch":0,"type":"pong","version":"5.1.16","uptime":3600}
```

| Field     | Type   | Description                            |
|-----------|--------|----------------------------------------|
| `version` | string | Server version                         |
| `uptime`  | int    | Server uptime in seconds               |

### configure

**Request**:

```json
{"ch":0,"type":"configure","observe_level":1,"wire_format":"ndjson"}
```

| Field           | Type   | Optional | Description                     |
|-----------------|--------|----------|---------------------------------|
| `observe_level` | int    | Yes      | Default observe detail level    |
| `wire_format`   | string | Yes      | Wire format ("ndjson" or "binary") |

**Response**:

```json
{"ch":0,"type":"configure_ok"}
```

### disconnect

**Request**:

```json
{"ch":0,"type":"disconnect"}
```

The server closes the connection after receiving this. No response
is sent.

---

## Channel 1: COMMAND

Command channel handles shell command evaluation.

### eval

**Request**:

```json
{"ch":1,"type":"eval","cmd":"echo hello world"}
```

| Field | Type   | Description                              |
|-------|--------|------------------------------------------|
| `cmd` | string | Shell command to evaluate                |

**Response sequence**: The server sends zero or more `stdout`/`stderr`
frames followed by exactly one `complete` or `error` frame.

**stdout** (zero or more):

```json
{"ch":1,"type":"stdout","data":"hello world\n"}
```

| Field  | Type   | Description                              |
|--------|--------|------------------------------------------|
| `data` | string | Chunk of standard output (may be base64) |

**stderr** (zero or more):

```json
{"ch":1,"type":"stderr","data":"ls: cannot access..."}
```

| Field  | Type   | Description                              |
|--------|--------|------------------------------------------|
| `data` | string | Chunk of standard error (may be base64)  |

**complete** (exactly one, terminates the eval):

```json
{"ch":1,"type":"complete","exit_code":0,"duration_ms":42}
```

| Field         | Type | Description                              |
|---------------|------|------------------------------------------|
| `exit_code`   | int  | Exit status of the command               |
| `duration_ms` | int  | Execution time in milliseconds (optional)|

**error** (instead of complete, on failure):

```json
{"ch":1,"type":"error","message":"command execution failed"}
```

### eval_parsed

**Request**:

```json
{"ch":1,"type":"eval_parsed","ast":{...COMMAND JSON...}}
```

| Field | Type   | Description                              |
|-------|--------|------------------------------------------|
| `ast` | object | Pre-parsed COMMAND tree as JSON          |

The response sequence is identical to `eval` (stdout, stderr, complete
or error frames).

The AST format matches the `cmd_serialize.c` JSON representation. See
`doc/server/channels.md` for the full COMMAND JSON schema.

---

## Channel 2: STATE

State channel provides get/set/unset operations on four namespaces:
variables, functions, aliases, and traps.

### get (variable)

**Request**:

```json
{"ch":2,"type":"get","target":"var","name":"PATH"}
```

| Field    | Type   | Description                              |
|----------|--------|------------------------------------------|
| `target` | string | Namespace: "var", "func", "alias", "trap"|
| `name`   | string | Item name                                |

**Response**:

```json
{"ch":2,"type":"result","name":"PATH","value":"/usr/bin:/bin","var_type":"string","attributes":["exported"]}
```

| Field        | Type     | Description                          |
|--------------|----------|--------------------------------------|
| `name`       | string   | Variable name                        |
| `value`      | string   | Variable value                       |
| `var_type`   | string   | Type: "string", "integer", "array", "assoc" |
| `attributes` | string[] | Attributes: "exported", "readonly", etc. |

### get (function)

**Request**:

```json
{"ch":2,"type":"get","target":"func","name":"my_function"}
```

**Response**:

```json
{"ch":2,"type":"result","name":"my_function","body":"echo hello\necho world"}
```

| Field  | Type   | Description                              |
|--------|--------|------------------------------------------|
| `name` | string | Function name                            |
| `body` | string | Function body text                       |

### get (alias)

**Request**:

```json
{"ch":2,"type":"get","target":"alias","name":"ll"}
```

**Response**:

```json
{"ch":2,"type":"result","name":"ll","value":"ls -lah"}
```

### get (trap)

**Request**:

```json
{"ch":2,"type":"get","target":"trap","name":"SIGINT"}
```

**Response**:

```json
{"ch":2,"type":"result","signal":"SIGINT","action":"echo caught"}
```

| Field    | Type   | Description                              |
|----------|--------|------------------------------------------|
| `signal` | string | Signal name                              |
| `action` | string | Trap action command                      |

### set (variable)

**Request**:

```json
{"ch":2,"type":"set","target":"var","name":"MY_VAR","value":"hello","attributes":["exported"]}
```

| Field        | Type     | Description                          |
|--------------|----------|--------------------------------------|
| `target`     | string   | "var"                                |
| `name`       | string   | Variable name                        |
| `value`      | string   | Value to set                         |
| `attributes` | string[] | Optional attributes                  |

**Response**:

```json
{"ch":2,"type":"ok"}
```

### set (function)

**Request**:

```json
{"ch":2,"type":"set","target":"func","name":"greet","body":"echo hello $1"}
```

**Response**:

```json
{"ch":2,"type":"ok"}
```

### set (alias)

**Request**:

```json
{"ch":2,"type":"set","target":"alias","name":"ll","value":"ls -lah"}
```

**Response**:

```json
{"ch":2,"type":"ok"}
```

### set (trap)

**Request**:

```json
{"ch":2,"type":"set","target":"trap","name":"SIGINT","action":"echo caught"}
```

**Response**:

```json
{"ch":2,"type":"ok"}
```

### unset

**Request**:

```json
{"ch":2,"type":"unset","target":"var","name":"MY_VAR"}
```

Works for all targets: "var", "func", "alias", "trap".

**Response**:

```json
{"ch":2,"type":"ok"}
```

### inspect

**Request**:

```json
{"ch":2,"type":"inspect","target":"vars"}
```

| Field    | Type   | Description                              |
|----------|--------|------------------------------------------|
| `target` | string | "vars", "functions", "aliases", "traps"  |

**Response**:

```json
{"ch":2,"type":"result","items":[{"name":"PATH","value":"/usr/bin:/bin","var_type":"string","attributes":["exported"]},{"name":"HOME","value":"/home/user","var_type":"string","attributes":["exported"]}]}
```

| Field   | Type  | Description                               |
|---------|-------|-------------------------------------------|
| `items` | array | Array of item objects (format varies by namespace) |

### State error

If a get operation finds no matching item:

```json
{"ch":2,"type":"error","message":"variable not found: NONEXISTENT"}
```

---

## Channel 3: OBSERVE

Observe channel lets the client subscribe to command execution events.
Once subscribed, the server pushes events as commands execute.

### subscribe

**Request**:

```json
{"ch":3,"type":"start","level":1}
```

| Field   | Type | Description                                 |
|---------|------|---------------------------------------------|
| `level` | int  | Detail level: 0 = basic, 1 = detailed       |

Level 0: command and exit code only.
Level 1: command, exit code, cwd, timing, pipeline info.

**Response**:

```json
{"ch":3,"type":"start_ok"}
```

### unsubscribe

**Request**:

```json
{"ch":3,"type":"stop"}
```

**Response**:

```json
{"ch":3,"type":"stop_ok"}
```

### pre_command (server-push)

Sent by the server before a command executes. Not a response to any
client request.

```json
{"ch":3,"type":"pre_command","command":"echo hello","cwd":"/home/user","timestamp":1706000000.123}
```

| Field       | Type   | Description                            |
|-------------|--------|----------------------------------------|
| `command`   | string | Command text about to execute          |
| `cwd`       | string | Current working directory (level 1)    |
| `timestamp` | float  | Unix timestamp (level 1)               |

### post_command (server-push)

Sent by the server after a command completes.

```json
{"ch":3,"type":"post_command","command":"echo hello","exit_status":0,"cwd":"/home/user","timestamp":1706000000.167,"duration_ms":44}
```

| Field         | Type   | Description                          |
|---------------|--------|--------------------------------------|
| `command`     | string | Command text that executed           |
| `exit_status` | int    | Exit status                          |
| `cwd`         | string | Current working directory (level 1)  |
| `timestamp`   | float  | Unix timestamp (level 1)             |
| `duration_ms` | int    | Execution duration (level 1)         |

---

## Channel 4: DEBUG

Debug channel provides breakpoints, execution stepping, and AST
inspection.

### enable

**Request**:

```json
{"ch":4,"type":"enable"}
```

**Response**:

```json
{"ch":4,"type":"enable_ok"}
```

### disable

**Request**:

```json
{"ch":4,"type":"disable"}
```

**Response**:

```json
{"ch":4,"type":"disable_ok"}
```

### add_breakpoint

**Request**:

```json
{"ch":4,"type":"add_breakpoint","kind":"command","pattern":"echo","line":-1,"file":null}
```

| Field     | Type   | Description                              |
|-----------|--------|------------------------------------------|
| `kind`    | string | "command", "line", or "function"         |
| `pattern` | string | Match pattern (command name, func name)  |
| `line`    | int    | Line number (-1 if not applicable)       |
| `file`    | string | File path (null if not applicable)       |

**Response**:

```json
{"ch":4,"type":"bp_set_ok","id":1}
```

| Field | Type | Description                                |
|-------|------|--------------------------------------------|
| `id`  | int  | Breakpoint ID (for later removal)          |

### remove_breakpoint

**Request**:

```json
{"ch":4,"type":"remove_breakpoint","id":1}
```

**Response**:

```json
{"ch":4,"type":"bp_clear_ok","id":1}
```

### list_breakpoints

**Request**:

```json
{"ch":4,"type":"list_breakpoints"}
```

**Response**:

```json
{"ch":4,"type":"bp_list","breakpoints":[{"id":1,"kind":"command","pattern":"echo","enabled":true}]}
```

### continue

**Request**:

```json
{"ch":4,"type":"continue"}
```

**Response**:

```json
{"ch":4,"type":"continue_ok"}
```

### step / next / finish / skip

**Request** (same format for all four):

```json
{"ch":4,"type":"step"}
{"ch":4,"type":"next"}
{"ch":4,"type":"finish"}
{"ch":4,"type":"skip"}
```

**Response**:

```json
{"ch":4,"type":"step_ok"}
```

(Or `next_ok`, `finish_ok`, `skip_ok` respectively.)

### inspect_ast

**Request**:

```json
{"ch":4,"type":"inspect_ast"}
```

**Response**:

```json
{"ch":4,"type":"ast_result","ast":{...COMMAND JSON...}}
```

The `ast` field contains the COMMAND tree at the current execution
point, serialized as JSON by `cmd_serialize.c`.

### breakpoint_hit (server-push)

Sent when execution hits a breakpoint. The server pauses execution
and waits for a stepping command (continue, step, next, finish, skip).

```json
{"ch":4,"type":"breakpoint_hit","breakpoint_id":1,"command":"echo hello","line":5,"file":"script.sh"}
```

| Field           | Type   | Description                        |
|-----------------|--------|------------------------------------|
| `breakpoint_id` | int    | Which breakpoint was hit           |
| `command`       | string | Command at the break location      |
| `line`          | int    | Line number (if available)         |
| `file`          | string | File path (if available)           |

### step_complete (server-push)

Sent when a step/next/finish operation completes and execution pauses
at the next point.

```json
{"ch":4,"type":"step_complete","command":"ls -la","line":6,"file":"script.sh"}
```

| Field     | Type   | Description                            |
|-----------|--------|----------------------------------------|
| `command` | string | Command at the new location            |
| `line`    | int    | Line number                            |
| `file`    | string | File path                              |

---

## Channel 5: PTY

PTY channel manages pseudo-terminal sessions for interactive shell
access.

### spawn

**Request**:

```json
{"ch":5,"type":"spawn","rows":24,"cols":80,"shell":null,"strip_ansi":false}
```

| Field        | Type   | Description                            |
|--------------|--------|----------------------------------------|
| `rows`       | int    | Terminal rows                          |
| `cols`       | int    | Terminal columns                       |
| `shell`      | string | Shell path (null = default bash)       |
| `strip_ansi` | bool   | Strip ANSI escape sequences from output |

**Response**:

```json
{"ch":5,"type":"spawn_ok","pid":12345}
```

| Field | Type | Description                                |
|-------|------|--------------------------------------------|
| `pid` | int  | PID of the PTY child process               |

### write

**Request**:

```json
{"ch":5,"type":"write","data":"bHMgLWxhCg=="}
```

| Field  | Type   | Description                              |
|--------|--------|------------------------------------------|
| `data` | string | Base64-encoded input data                |

**Response**:

```json
{"ch":5,"type":"write_ok"}
```

### resize

**Request**:

```json
{"ch":5,"type":"resize","rows":48,"cols":120}
```

**Response**:

```json
{"ch":5,"type":"resize_ok"}
```

### signal

**Request**:

```json
{"ch":5,"type":"signal","name":"SIGINT"}
```

| Field  | Type   | Description                              |
|--------|--------|------------------------------------------|
| `name` | string | Signal name (e.g., "SIGINT", "SIGTERM")  |

**Response**:

```json
{"ch":5,"type":"signal_ok"}
```

### close

**Request**:

```json
{"ch":5,"type":"close"}
```

**Response**:

```json
{"ch":5,"type":"close_ok"}
```

### output (server-push)

PTY output data, pushed to the client as it becomes available.

```json
{"ch":5,"type":"output","data":"aGVsbG8gd29ybGQK"}
```

| Field  | Type   | Description                              |
|--------|--------|------------------------------------------|
| `data` | string | Base64-encoded output data               |

Output arrives asynchronously. The client should decode base64 and
deliver to the registered output callback.

### exit (server-push)

Sent when the PTY child process exits.

```json
{"ch":5,"type":"exit","exit_code":0}
```

| Field       | Type | Description                             |
|-------------|------|-----------------------------------------|
| `exit_code` | int  | Exit status of the PTY child process    |

---

## Base64 Encoding

Binary data is encoded as standard base64 (RFC 4648) in JSON string
fields. This applies to:

- **PTY write input** (`ch:5, type:write, data`)
- **PTY output** (`ch:5, type:output, data`)
- **Command stdout/stderr** when containing binary data

### Encoding Rules

- Use standard base64 alphabet (A-Z, a-z, 0-9, +, /)
- Use `=` padding
- No line breaks within the encoded string
- Decode produces raw bytes

### When Is Base64 Used

For COMMAND channel stdout/stderr, the server may send data as either
plain UTF-8 strings or base64-encoded strings. The client should detect
encoding by checking if the data looks like valid base64, or by
checking for a separate `"encoding":"base64"` field if present.

For PTY channel, all `write` and `output` data is always base64-encoded
because PTY I/O is inherently binary.

---

## Error Responses

Any channel can return an error response:

```json
{"ch":<N>,"type":"error","message":"<description>"}
```

| Field     | Type   | Description                              |
|-----------|--------|------------------------------------------|
| `ch`      | int    | Channel that produced the error          |
| `type`    | string | Always "error"                           |
| `message` | string | Human-readable error description         |

### Error Mapping

Clients should map server errors to the appropriate error type:

| Condition                  | Client Error Type  |
|----------------------------|--------------------|
| `ch:0, type:auth_fail`    | AuthError          |
| Malformed JSON frame       | ProtocolError      |
| Unknown channel ID         | ProtocolError      |
| Missing required field     | ProtocolError      |
| No response within timeout | TimeoutError       |
| Socket/IO failure          | TransportError     |
| `type:error` response      | ServerError        |

---

## Request/Response vs Server-Push

Messages fall into two categories:

### Request/Response

The client sends a request and expects one or more response frames.
The client waits (with timeout) for the response.

| Channel | Request Types                                  | Response Types                                  |
|---------|------------------------------------------------|------------------------------------------------|
| 0       | auth, ping, configure, disconnect              | auth_ok, auth_fail, pong, configure_ok         |
| 1       | eval, eval_parsed                              | stdout, stderr, complete, error                |
| 2       | get, set, unset, inspect                       | result, ok, error                              |
| 3       | start, stop                                    | start_ok, stop_ok                              |
| 4       | enable, disable, add_breakpoint, remove_breakpoint, list_breakpoints, continue, step, next, finish, skip, inspect_ast | enable_ok, disable_ok, bp_set_ok, bp_clear_ok, bp_list, continue_ok, step_ok, next_ok, finish_ok, skip_ok, ast_result |
| 5       | spawn, write, resize, signal, close            | spawn_ok, write_ok, resize_ok, signal_ok, close_ok |

### Server-Push

The server sends messages without a prior request. These arrive
asynchronously and must be dispatched to callbacks or event handlers.

| Channel | Push Event Types                    | When                                    |
|---------|-------------------------------------|-----------------------------------------|
| 3       | pre_command, post_command           | During command execution (while subscribed) |
| 4       | breakpoint_hit, step_complete       | When breakpoint hit or step completes   |
| 5       | output, exit                        | PTY output data, PTY process exit       |

### Routing Implementation

The client's message reader must distinguish push events from responses.
The simplest approach is a lookup table:

```
PUSH_EVENTS = {
    3: {"pre_command", "post_command"},
    4: {"breakpoint_hit", "step_complete"},
    5: {"output", "exit"},
}

for each incoming message (ch, type, data):
    if type in PUSH_EVENTS.get(ch, {}):
        dispatch to callbacks
    else:
        put in channel response queue
```

---

## Wire Examples

### Complete Session

A full session from connect to disconnect:

```
CLIENT: {"ch":0,"type":"auth","token":"a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2c3d4e5f6a1b2"}
SERVER: {"ch":0,"type":"auth_ok","version":"5.1.16","protocol":2}
CLIENT: {"ch":1,"type":"eval","cmd":"echo hello"}
SERVER: {"ch":1,"type":"stdout","data":"hello\n"}
SERVER: {"ch":1,"type":"complete","exit_code":0,"duration_ms":3}
CLIENT: {"ch":2,"type":"get","target":"var","name":"PWD"}
SERVER: {"ch":2,"type":"result","name":"PWD","value":"/home/user","var_type":"string","attributes":["exported"]}
CLIENT: {"ch":0,"type":"ping"}
SERVER: {"ch":0,"type":"pong","version":"5.1.16","uptime":120}
CLIENT: {"ch":0,"type":"disconnect"}
```

### Eval with stderr

```
CLIENT: {"ch":1,"type":"eval","cmd":"ls /nonexistent && echo done"}
SERVER: {"ch":1,"type":"stderr","data":"ls: cannot access '/nonexistent': No such file or directory\n"}
SERVER: {"ch":1,"type":"complete","exit_code":2,"duration_ms":5}
```

### Observe Session

```
CLIENT: {"ch":3,"type":"start","level":1}
SERVER: {"ch":3,"type":"start_ok"}
CLIENT: {"ch":1,"type":"eval","cmd":"echo hello"}
SERVER: {"ch":3,"type":"pre_command","command":"echo hello","cwd":"/home/user","timestamp":1706000000.100}
SERVER: {"ch":1,"type":"stdout","data":"hello\n"}
SERVER: {"ch":3,"type":"post_command","command":"echo hello","exit_status":0,"cwd":"/home/user","timestamp":1706000000.105,"duration_ms":5}
SERVER: {"ch":1,"type":"complete","exit_code":0,"duration_ms":5}
CLIENT: {"ch":3,"type":"stop"}
SERVER: {"ch":3,"type":"stop_ok"}
```

Note: observe events and eval responses are interleaved. The client
must route by channel ID, not by arrival order.

### Debug Session

```
CLIENT: {"ch":4,"type":"enable"}
SERVER: {"ch":4,"type":"enable_ok"}
CLIENT: {"ch":4,"type":"add_breakpoint","kind":"command","pattern":"echo","line":-1,"file":null}
SERVER: {"ch":4,"type":"bp_set_ok","id":1}
CLIENT: {"ch":1,"type":"eval","cmd":"echo hello; echo world"}
SERVER: {"ch":4,"type":"breakpoint_hit","breakpoint_id":1,"command":"echo hello","line":1,"file":null}
CLIENT: {"ch":4,"type":"inspect_ast"}
SERVER: {"ch":4,"type":"ast_result","ast":{"type":"simple","words":["echo","hello"]}}
CLIENT: {"ch":4,"type":"continue"}
SERVER: {"ch":4,"type":"continue_ok"}
SERVER: {"ch":1,"type":"stdout","data":"hello\n"}
SERVER: {"ch":4,"type":"breakpoint_hit","breakpoint_id":1,"command":"echo world","line":1,"file":null}
CLIENT: {"ch":4,"type":"continue"}
SERVER: {"ch":4,"type":"continue_ok"}
SERVER: {"ch":1,"type":"stdout","data":"world\n"}
SERVER: {"ch":1,"type":"complete","exit_code":0}
CLIENT: {"ch":4,"type":"remove_breakpoint","id":1}
SERVER: {"ch":4,"type":"bp_clear_ok","id":1}
CLIENT: {"ch":4,"type":"disable"}
SERVER: {"ch":4,"type":"disable_ok"}
```

### PTY Session

```
CLIENT: {"ch":5,"type":"spawn","rows":24,"cols":80,"shell":null,"strip_ansi":true}
SERVER: {"ch":5,"type":"spawn_ok","pid":12345}
SERVER: {"ch":5,"type":"output","data":"dXNlckBob3N0On4kIA=="}
CLIENT: {"ch":5,"type":"write","data":"ZWNobyBoZWxsbwo="}
SERVER: {"ch":5,"type":"write_ok"}
SERVER: {"ch":5,"type":"output","data":"ZWNobyBoZWxsbwpoZWxsbwp1c2VyQGhvc3Q6fiQg"}
CLIENT: {"ch":5,"type":"resize","rows":48,"cols":120}
SERVER: {"ch":5,"type":"resize_ok"}
CLIENT: {"ch":5,"type":"write","data":"ZXhpdAo="}
SERVER: {"ch":5,"type":"write_ok"}
SERVER: {"ch":5,"type":"output","data":"ZXhpdAo="}
SERVER: {"ch":5,"type":"exit","exit_code":0}
```

---

## Implementation Guide

Guidelines for implementing a new client binding.

### 1. Transport Layer

Start with the transport layer. Implement at minimum:

- `connect(path)` for Unix socket
- `read_line()` that reads until `\n`
- `write(data)` that sends bytes
- `close()` that shuts down the connection
- `is_connected` property

Add stdio, fd, and Named Pipe transports after the socket transport
works.

### 2. Protocol Layer

Implement NDJSON encoding and decoding:

- `encode_frame(channel, data)` -> bytes with trailing `\n`
- `decode_frame(line)` -> (channel, data dict)
- `encode_base64(bytes)` -> string
- `decode_base64(string)` -> bytes

This layer should be stateless and easy to unit test.

### 3. Message Reader

Implement a background reader that continuously reads frames and
routes them. The reader must:

- Run concurrently with the main application (task, thread, or poll)
- Decode each frame to get channel ID and message type
- Route push events to callbacks
- Route responses to per-channel queues or pending request slots

### 4. Channel Methods

Implement the public API methods for each channel. Each method:

1. Constructs a request message
2. Sends it via the protocol layer
3. Waits for the response (with timeout)
4. Parses the response into a typed result
5. Returns the result or raises an error

### 5. Error Handling

Map server errors and transport failures to the 5 error types.
Every operation that can fail should report errors through the
language's idiomatic mechanism (exceptions, return codes, Result types).

### 6. Testing

Write tests at all three levels (unit, channel, integration) as
described in [TESTING.md](TESTING.md).

---

## Protocol Negotiation

### NDJSON Auto-Detection

bash-server v2 auto-detects the wire format from the first byte of
the first message:

| First Byte         | Detected Format      |
|--------------------|----------------------|
| `{` (0x7B)         | NDJSON               |
| `\n` (0x0A)        | NDJSON               |
| 0x00 - 0x05        | Binary v2 (channel ID as first byte) |
| Any other byte     | v1 text protocol     |

Since all NDJSON messages start with `{`, the client does not need to
send any negotiation handshake. Simply send the auth message as NDJSON
and the server will respond in kind.

### v1 Compatibility

Client bindings in this repository do NOT implement the v1 text
protocol. If you need v1 support, use the `bashclient` CLI tool in
`bashclient/` (which is a separate C program, not part of these
bindings).

---

## Limits and Constraints

| Limit                | Value           | Description                      |
|----------------------|-----------------|----------------------------------|
| Max frame size       | 1 MB            | 1,048,576 bytes including `\n`   |
| Max token length     | 64 chars        | 256-bit hex-encoded              |
| Max concurrent evals | 1               | One eval at a time per session   |
| Max channels         | 6               | Channels 0-5 only               |
| Max breakpoints      | Implementation-dependent | Server limit, not protocol limit |
| Max PTY sessions     | 1               | One PTY per client session       |
| Line terminator      | LF only         | `\n` (0x0A), not `\r\n`         |
| Character encoding   | UTF-8           | All JSON must be valid UTF-8     |

### Concurrent Channel Use

While only one eval can be in-flight at a time, different channels can
operate concurrently. For example, you can subscribe to observe events
(channel 3) while running an eval (channel 1) and inspecting state
(channel 2). The per-channel queue design supports this naturally.

### Ordering Guarantees

- Messages within a single channel are ordered (FIFO)
- Messages across channels have no ordering guarantee
- Server-push events may be interleaved with responses
- Stdout/stderr chunks for an eval arrive in order, but may be
  interleaved with push events from other channels
