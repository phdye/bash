# API Reference — bash-server

## Client Libraries

Each library implements the v2 protocol natively. Use a client library instead of constructing protocol frames directly. See [integration.md](integration.md#client-libraries) for install and usage details.

| Language | Package | Native v2 protocol | All 4 transports | All 6 channels |
|----------|---------|-------------------|-------------------|----------------|
| Python | bashclient | yes | yes | yes |
| TypeScript | bashclient | yes | yes | yes |
| Go | bashclient | yes | yes | yes |
| C# | BashServer.Client | yes | yes | yes |
| C | libbashclient | yes | yes | yes |
| Java | org.gnu.bash.client | yes | yes | yes |

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
