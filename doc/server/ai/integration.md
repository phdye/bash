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
| Go | [clients/go/README.md](../../../clients/go/README.md) |
| C# | [clients/csharp/README.md](../../../clients/csharp/README.md) |
| Python | [clients/python/README.md](../../../clients/python/README.md) |
| TypeScript | [clients/typescript/README.md](../../../clients/typescript/README.md) |
| C | [clients/c/README.md](../../../clients/c/README.md) |
| Java | [clients/java/README.md](../../../clients/java/README.md) |
