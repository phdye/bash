# bash-server Channel Architecture

**Audience:** Developers integrating with the v2 JSON protocol.

## Overview

The v2 protocol multiplexes communication over 6 channels (0--5).  Each frame
includes a channel byte identifying which subsystem the message belongs to.
This enables concurrent operations---for example, receiving observability
events on channel 3 while executing commands on channel 1, or managing
breakpoints on channel 4 while a PTY session runs on channel 5.

### Frame Structure

Every v2 frame has a 6-byte binary header (or an NDJSON envelope with a
`"channel"` field):

```
Binary:   [channel:1][flags:1][length:4][payload:N]
NDJSON:   {"channel":N, ...payload fields...}\n
```

| Field | Size | Description |
|-------|------|-------------|
| `channel` | 1 byte | Channel ID (0--5) |
| `flags` | 1 byte | Bitfield: compressed, binary, continued, final |
| `length` | 4 bytes | Payload length (network byte order) |
| `payload` | N bytes | JSON message body |

See [transports.md](transports.md) for wire format details.

### Channel Summary

| ID | Name | Direction | Purpose |
|----|------|-----------|---------|
| 0 | `CHAN_CONTROL` | Bidirectional | Session lifecycle: auth, ping, disconnect, configure |
| 1 | `CHAN_COMMAND` | Bidirectional | Command execution: eval, execute, stdout/stderr/complete |
| 2 | `CHAN_STATE` | Bidirectional | Shell state: variables, functions, aliases, traps |
| 3 | `CHAN_OBSERVE` | Server push | Observability events: pre/post command hooks |
| 4 | `CHAN_DEBUG` | Bidirectional | Interactive debugger: breakpoints, stepping, AST inspection |
| 5 | `CHAN_PTY` | Bidirectional | Terminal emulation: spawn, I/O, resize, close |

## CHAN_CONTROL (0) --- Session Lifecycle

The control channel manages session-level operations.  All sessions begin
on this channel with authentication.

### Authentication

**Request:**
```json
{"type":"auth","token":"<64-hex-chars>"}
```

**Responses:**
```json
{"type":"auth_ok"}
{"type":"auth_fail","message":"invalid token"}
{"type":"auth_fail","message":"token required"}
```

Authentication must succeed before any other channel accepts commands
(except `CHAN_CONTROL` ping and disconnect).

### Ping

**Request:**
```json
{"type":"ping"}
```

**Response:**
```json
{"type":"pong"}
```

Available in both authenticated and unauthenticated states.  No side effects.

### Disconnect

**Request:**
```json
{"type":"disconnect"}
```

**Response:**
```json
{"type":"disconnect_ok"}
```

The server sends the response and then closes the connection, freeing all
session resources.

### Configure

**Request:**
```json
{"type":"configure","observe_level":1}
```

**Response:**
```json
{"type":"configure_ok","observe_level":1}
```

Sets session-wide parameters.  Currently the only configurable parameter is
the observability level (see CHAN_OBSERVE below).

| Parameter | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| `observe_level` | int | 0--1 | 0 | Observability event verbosity |

## CHAN_COMMAND (1) --- Command Execution

The command channel handles evaluation of Bash commands and delivery of
their output.  Each command produces a stream of stdout/stderr frames
followed by a completion frame.

### Eval (Text Command)

**Request:**
```json
{"type":"eval","command":"echo hello world"}
```

The server forks a child process, executes the command via
`parse_and_execute()`, and streams output back as it becomes available.

### Execute (Pre-parsed AST)

**Request:**
```json
{"type":"execute","ast":{...}}
```

Accepts a serialized COMMAND tree (as produced by `cmd_serialize()`) and
executes it directly, bypassing the parser.  The response pattern is
identical to eval.

### Output Frames

During command execution, the server sends stdout and stderr data as
separate frames on the command channel:

```json
{"type":"stdout","data":"aGVsbG8gd29ybGQK","encoding":"base64"}
{"type":"stderr","data":"bHM6IG5vdCBmb3VuZA==","encoding":"base64"}
```

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | `"stdout"` or `"stderr"` |
| `data` | string | Base64-encoded output data (RFC 4648) |
| `encoding` | string | Always `"base64"` |

Empty output streams are not sent---if a command produces no stderr, no
stderr frame is emitted.

### Completion

After all output frames, a completion frame terminates the command:

```json
{"type":"complete","exit_code":0}
```

| Field | Type | Description |
|-------|------|-------------|
| `exit_code` | int | Exit status (0--255); 128+N for signal N |

### Error Responses

If the command cannot be executed (e.g., fork failure, pipe creation
failure, not authenticated), an error is returned instead:

```json
{"type":"error","message":"not authenticated"}
{"type":"error","message":"fork failed"}
{"type":"error","message":"command required"}
```

### Typical Eval Sequence

```
Client → {"type":"eval","command":"ls /tmp"}            [CHAN_COMMAND]
Server → {"type":"stdout","data":"...","encoding":"base64"}  [CHAN_COMMAND]
Server → {"type":"complete","exit_code":0}               [CHAN_COMMAND]
```

## CHAN_STATE (2) --- Shell State Operations

The state channel provides direct access to shell internals---variables,
functions, aliases, and traps---without executing commands.  All operations
require an authenticated session.

### Variables

**Get variable:**
```json
{"type":"get_var","name":"PATH"}
```
```json
{"type":"var","name":"PATH","value":"/usr/bin:/bin","exported":true,"readonly":false}
```

**Set variable:**
```json
{"type":"set_var","name":"FOO","value":"bar"}
```
```json
{"type":"set_var_ok","name":"FOO"}
```

**Unset variable:**
```json
{"type":"unset_var","name":"FOO"}
```
```json
{"type":"unset_var_ok","name":"FOO"}
```

### Functions

**Get function:**
```json
{"type":"get_func","name":"my_func"}
```
```json
{"type":"func","name":"my_func","body":"echo hello"}
```

**Unset function:**
```json
{"type":"unset_func","name":"my_func"}
```
```json
{"type":"unset_func_ok","name":"my_func"}
```

### Aliases

**Get alias:**
```json
{"type":"get_alias","name":"ll"}
```
```json
{"type":"alias","name":"ll","value":"ls -la"}
```

**Set alias:**
```json
{"type":"set_alias","name":"ll","value":"ls -la"}
```
```json
{"type":"set_alias_ok","name":"ll"}
```

**Unset alias:**
```json
{"type":"unset_alias","name":"ll"}
```
```json
{"type":"unset_alias_ok","name":"ll"}
```

### Traps

**Set trap:**
```json
{"type":"set_trap","signal":"INT","action":"echo interrupted"}
```
```json
{"type":"set_trap_ok","signal":"INT"}
```

**Unset trap:**
```json
{"type":"unset_trap","signal":"INT"}
```
```json
{"type":"unset_trap_ok","signal":"INT"}
```

### Inspect

The inspect operation returns bulk listings of shell state:

**Request:**
```json
{"type":"inspect","target":"vars"}
```

**Response:**
```json
{"type":"inspect_result","target":"vars","data":[{"name":"HOME","value":"/home/user","exported":true,"readonly":false}, ...]}
```

| Target | Data contents |
|--------|---------------|
| `vars` | Array of variable objects (name, value, exported, readonly) |
| `funcs` | Array of function objects (name, body) |
| `aliases` | Array of alias objects (name, value) |
| `traps` | Array of trap objects (signal, action) |

### Error Responses

```json
{"type":"error","message":"not authenticated"}
{"type":"error","message":"variable not found: NOEXIST"}
{"type":"error","message":"unknown inspect target: foo"}
```

## CHAN_OBSERVE (3) --- Observability Events

The observe channel delivers server-push events about command execution.
Events are sent asynchronously---the client does not request them; the
server emits them as commands execute on `CHAN_COMMAND`.

### Observe Levels

| Level | Constant | Events emitted |
|-------|----------|---------------|
| 0 | `OBSERVE_LEVEL_OFF` | None (stdout, stderr, and exit code are delivered on CHAN_COMMAND implicitly) |
| 1 | `OBSERVE_LEVEL_COMMAND` | `pre_command` and `post_command` events for each simple command |

Set the level via `CHAN_CONTROL` configure:

```json
{"type":"configure","observe_level":1}
```

### Pre-command Event

Emitted before a simple command begins execution:

```json
{
  "type": "pre_command",
  "seq": 1,
  "command": "ls -la /tmp",
  "cwd": "/home/user",
  "line_number": 1,
  "is_subshell": false,
  "is_async": false,
  "timestamp": 1706600000.123
}
```

| Field | Type | Description |
|-------|------|-------------|
| `seq` | int | Monotonically increasing sequence number for ordering |
| `command` | string | The command text about to execute |
| `cwd` | string | Current working directory at time of execution |
| `line_number` | int | Source line number (if available) |
| `is_subshell` | bool | True if executing in a subshell |
| `is_async` | bool | True if the command was launched asynchronously (`&`) |
| `timestamp` | float | Unix epoch timestamp (seconds with millisecond precision) |

### Post-command Event

Emitted after a simple command completes:

```json
{
  "type": "post_command",
  "seq": 2,
  "command": "ls -la /tmp",
  "exit_status": 0,
  "signal_number": 0,
  "duration_ms": 12,
  "timestamp": 1706600000.135
}
```

| Field | Type | Description |
|-------|------|-------------|
| `seq` | int | Sequence number (follows the corresponding pre_command) |
| `command` | string | The command text that executed |
| `exit_status` | int | Exit code (0--255) |
| `signal_number` | int | Signal number if terminated by signal, 0 otherwise |
| `duration_ms` | int | Execution duration in milliseconds |
| `timestamp` | float | Unix epoch timestamp at completion |

### Interleaving with Command Channel

Observe events on `CHAN_OBSERVE` are interleaved with output frames on
`CHAN_COMMAND`.  A typical sequence at observe level 1:

```
Server → {"type":"pre_command","seq":1,"command":"echo hi",...}   [CHAN_OBSERVE]
Server → {"type":"stdout","data":"aGkK","encoding":"base64"}     [CHAN_COMMAND]
Server → {"type":"post_command","seq":2,"command":"echo hi",...}  [CHAN_OBSERVE]
Server → {"type":"complete","exit_code":0}                       [CHAN_COMMAND]
```

## CHAN_DEBUG (4) --- Interactive Debugger

The debug channel provides an interactive debugging interface with
breakpoints, stepping, and AST inspection.  When a breakpoint is hit,
command execution pauses and the server sends a `break_hit` event,
waiting for the client to issue a continuation command.

### Enable / Disable Debugger

**Enable:**
```json
{"type":"enable"}
```
```json
{"type":"enable_ok"}
```

**Disable:**
```json
{"type":"disable"}
```
```json
{"type":"disable_ok"}
```

### Breakpoint Management

**Add breakpoint:**
```json
{"type":"break","bp_type":"command","pattern":"ls*"}
{"type":"break","bp_type":"line","line":10}
{"type":"break","bp_type":"func","pattern":"my_func"}
```
```json
{"type":"break_ok","id":1}
```

| `bp_type` | Constant | Match target |
|-----------|----------|-------------|
| `command` | `DBG_BREAK_COMMAND` | Command text (glob pattern) |
| `line` | `DBG_BREAK_LINE` | Source line number |
| `func` | `DBG_BREAK_FUNC` | Function name (glob pattern) |

**Delete breakpoint:**
```json
{"type":"delete","id":1}
```
```json
{"type":"delete_ok","id":1}
```

**Enable/disable breakpoint:**
```json
{"type":"enable_bp","id":1}
{"type":"disable_bp","id":1}
```
```json
{"type":"enable_bp_ok","id":1}
{"type":"disable_bp_ok","id":1}
```

**List breakpoints:**
```json
{"type":"list"}
```
```json
{"type":"breakpoints","data":[{"id":1,"type":"command","pattern":"ls*","enabled":true,"hit_count":3}, ...]}
```

### Execution Control

When execution is paused at a breakpoint, the following commands control
how execution resumes:

| Command | Description |
|---------|-------------|
| `{"type":"continue"}` | Resume normal execution until next breakpoint |
| `{"type":"step"}` | Execute one statement, then pause |
| `{"type":"next"}` | Execute one statement at current depth, stepping over calls |
| `{"type":"finish"}` | Run until the current function returns |
| `{"type":"skip"}` | Skip the current command without executing it |

Each responds with:
```json
{"type":"continue_ok"}
{"type":"step_ok"}
{"type":"next_ok"}
{"type":"finish_ok"}
{"type":"skip_ok"}
```

### AST Inspection

While paused at a breakpoint, the client can inspect the pending command's
AST:

**Request:**
```json
{"type":"inspect_ast"}
```

**Response:**
```json
{"type":"ast","data":{...}}
```

The `data` field contains the serialized COMMAND tree as produced by
`cmd_serialize()`.  The tree structure follows the Bash internal
representation: simple commands, pipelines, connections, for/while/if/case
constructs, function definitions, and redirections.

### Status Query

**Request:**
```json
{"type":"status"}
```

**Response:**
```json
{"type":"status","active":true,"mode":"step","breakpoint_count":3,"depth":2}
```

| Field | Type | Description |
|-------|------|-------------|
| `active` | bool | Whether the debugger is enabled |
| `mode` | string | Current step mode: `"run"`, `"step"`, `"next"`, `"finish"` |
| `breakpoint_count` | int | Number of registered breakpoints |
| `depth` | int | Current call/subshell nesting depth |

### Break Event (Server Push)

When execution hits a breakpoint, the server sends:

```json
{
  "type": "break_hit",
  "id": 1,
  "line": 15,
  "command": "echo $result",
  "depth": 1
}
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | int | Breakpoint ID that triggered the pause |
| `line` | int | Source line number |
| `command` | string | The command about to execute |
| `depth` | int | Current nesting depth |

Execution remains paused until the client sends a continuation command
(continue, step, next, finish, or skip).

### Debug Session Flow

```
Client → {"type":"enable"}                              [CHAN_DEBUG]
Server → {"type":"enable_ok"}                            [CHAN_DEBUG]
Client → {"type":"break","bp_type":"command","pattern":"echo*"} [CHAN_DEBUG]
Server → {"type":"break_ok","id":1}                      [CHAN_DEBUG]

Client → {"type":"eval","command":"echo hello; echo world"} [CHAN_COMMAND]

Server → {"type":"break_hit","id":1,"line":1,"command":"echo hello","depth":0} [CHAN_DEBUG]
Client → {"type":"inspect_ast"}                          [CHAN_DEBUG]
Server → {"type":"ast","data":{...}}                     [CHAN_DEBUG]
Client → {"type":"step"}                                 [CHAN_DEBUG]
Server → {"type":"step_ok"}                              [CHAN_DEBUG]
Server → {"type":"stdout","data":"aGVsbG8K","encoding":"base64"} [CHAN_COMMAND]

Server → {"type":"break_hit","id":1,"line":1,"command":"echo world","depth":0} [CHAN_DEBUG]
Client → {"type":"continue"}                             [CHAN_DEBUG]
Server → {"type":"continue_ok"}                          [CHAN_DEBUG]
Server → {"type":"stdout","data":"d29ybGQK","encoding":"base64"} [CHAN_COMMAND]
Server → {"type":"complete","exit_code":0}               [CHAN_COMMAND]
```

## CHAN_PTY (5) --- Terminal Emulation

The PTY channel provides full terminal emulation by allocating a
pseudo-terminal and running a shell (or specified command) inside it.
Once a PTY session is spawned, the channel takes over the session loop
for the duration of the PTY lifetime.

### Spawn

**Request:**
```json
{"type":"spawn","rows":24,"cols":80,"shell":"/bin/bash","strip_ansi":false}
```

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `rows` | int | 24 | Initial terminal row count |
| `cols` | int | 80 | Initial terminal column count |
| `shell` | string | `"/bin/bash"` | Command to execute in the PTY |
| `strip_ansi` | bool | false | Strip ANSI escape sequences from output |

**Response:**
```json
{"type":"spawn_ok","pid":12345}
```

### Input (Client to Server)

```json
{"type":"input","data":"bHMgLWxhCg=="}
```

The `data` field is base64-encoded terminal input.  The server writes it
to the PTY master file descriptor.

### Output (Server to Client)

```json
{"type":"output","data":"dG90YWwgNDgK..."}
```

The `data` field is base64-encoded terminal output.  If `strip_ansi` was
set to true on spawn, ANSI escape sequences are removed before encoding.

### Resize

```json
{"type":"resize","rows":40,"cols":120}
```
```json
{"type":"resize_ok"}
```

Sends `TIOCSWINSZ` ioctl to the PTY to update terminal dimensions and
delivers `SIGWINCH` to the child process group.

### Signal

```json
{"type":"signal","name":"INT"}
```
```json
{"type":"signal_ok"}
```

Sends the named signal to the PTY child process.  Signal names follow
POSIX conventions (without the `SIG` prefix): `INT`, `TERM`, `KILL`,
`HUP`, `QUIT`, etc.

### Close

```json
{"type":"close"}
```
```json
{"type":"close_ok"}
```

Closes the PTY master, which sends `SIGHUP` to the child process.
The server waits for the child to exit and then emits an exit event.

### Exit Event (Server Push)

When the PTY child process exits (whether from close, signal, or normal
termination):

```json
{"type":"exit","exit_code":0}
```

After this event, the PTY session is over and the channel returns to
idle.  The session loop resumes normal frame dispatch.

### PTY Session Flow

```
Client → {"type":"spawn","rows":24,"cols":80}           [CHAN_PTY]
Server → {"type":"spawn_ok","pid":12345}                 [CHAN_PTY]

Client → {"type":"input","data":"bHMK"}                  [CHAN_PTY]
Server → {"type":"output","data":"..."}                  [CHAN_PTY]

Client → {"type":"resize","rows":40,"cols":120}          [CHAN_PTY]
Server → {"type":"resize_ok"}                            [CHAN_PTY]

Client → {"type":"close"}                               [CHAN_PTY]
Server → {"type":"close_ok"}                             [CHAN_PTY]
Server → {"type":"exit","exit_code":0}                   [CHAN_PTY]
```

## Cross-Channel Interactions

### Command Execution with Observability and Debugging

All three channels---CHAN_COMMAND, CHAN_OBSERVE, and CHAN_DEBUG---interact
during command execution.  A client may receive frames on any of these
channels interleaved:

```
Client → {"type":"configure","observe_level":1}          [CHAN_CONTROL]
Client → {"type":"enable"}                               [CHAN_DEBUG]
Client → {"type":"break","bp_type":"line","line":5}      [CHAN_DEBUG]
Client → {"type":"eval","command":"...multi-line..."}    [CHAN_COMMAND]

Server → {"type":"pre_command",...}                       [CHAN_OBSERVE]
Server → {"type":"break_hit",...}                         [CHAN_DEBUG]
Client → {"type":"continue"}                             [CHAN_DEBUG]
Server → {"type":"stdout",...}                            [CHAN_COMMAND]
Server → {"type":"post_command",...}                      [CHAN_OBSERVE]
Server → {"type":"complete","exit_code":0}               [CHAN_COMMAND]
```

### PTY Exclusivity

While a PTY session is active on CHAN_PTY, the session loop is dedicated
to PTY I/O.  Other channels are not processed until the PTY exits.
Clients should complete or close PTY sessions before issuing commands
on other channels.

## Constants Reference

Defined in `server.h`:

```c
#define CHAN_CONTROL  0
#define CHAN_COMMAND  1
#define CHAN_STATE    2
#define CHAN_OBSERVE  3
#define CHAN_DEBUG    4
#define CHAN_PTY      5
#define CHAN_MAX      5

#define OBSERVE_LEVEL_OFF      0
#define OBSERVE_LEVEL_COMMAND  1
#define OBSERVE_LEVEL_MAX      1
```

## See Also

- [protocol.md](protocol.md) --- v1 line-oriented text protocol specification
- [transports.md](transports.md) --- transport layer and wire format details
- [architecture.md](architecture.md) --- internal design and code structure
- [developer.md](developer.md) --- contributor guide: building, testing, extending
