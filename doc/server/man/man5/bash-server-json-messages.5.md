# bash-server-json-messages(5) — bash-server JSON message schemas

# DESCRIPTION

This page documents the JSON message schemas used by the
**bash-server**(1) v2 protocol across all channels. These schemas
apply to both binary framing (see **bash-server-v2-protocol**(5))
and NDJSON framing (see **bash-server-ndjson**(5)).

Every message is a JSON object containing at least a **"type"** field
that identifies the message kind. Messages are organized by channel.

# CHAN_CONTROL (0)

The control channel handles authentication, liveness checks, and
session configuration.

# Client messages

**auth** — Authenticate with the server.

```json
{"type": "auth", "token": "<64-char hex string>"}
```

**ping** — Liveness check.

```json
{"type": "ping"}
```

**disconnect** — Request graceful disconnect.

```json
{"type": "disconnect"}
```

**configure** — Set session parameters.

```json
{"type": "configure", "observe_level": N}
```

- **observe_level** (integer): observation verbosity level for
  CHAN_OBSERVE push events. 0 disables observation.

# Server messages

**auth_ok** — Authentication succeeded.

```json
{"type": "auth_ok"}
```

**auth_fail** — Authentication failed.

```json
{"type": "auth_fail", "message": "<reason>"}
```

**pong** — Reply to ping.

```json
{"type": "pong"}
```

**disconnect_ok** — Disconnect acknowledged; connection will close.

```json
{"type": "disconnect_ok"}
```

**configure_ok** — Configuration applied.

```json
{"type": "configure_ok", "observe_level": N}
```

# CHAN_COMMAND (1)

The command channel handles Bash command evaluation and output
streaming.

# Client messages

**eval** — Evaluate a Bash command string.

```json
{"type": "eval", "command": "<bash command>"}
```

**execute** — Execute a pre-parsed COMMAND AST tree. See
**bash-server-cmd-json**(5) for the AST schema.

```json
{"type": "execute", "ast": { ... }}
```

# Server messages

**stdout** — Standard output chunk from the evaluated command.

```json
{"type": "stdout", "data": "<base64-encoded data>", "encoding": "base64"}
```

**stderr** — Standard error chunk from the evaluated command.

```json
{"type": "stderr", "data": "<base64-encoded data>", "encoding": "base64"}
```

**complete** — Command finished executing.

```json
{"type": "complete", "exit_code": N}
```

An **eval** or **execute** request produces zero or more **stdout**
and **stderr** messages followed by exactly one **complete** message.

# CHAN_STATE (2)

The state channel provides direct access to shell variables, functions,
aliases, and traps without evaluating Bash commands.

# Client messages

**get_var** — Retrieve a shell variable.

```json
{"type": "get_var", "name": "<variable name>"}
```

**set_var** — Set a shell variable.

```json
{"type": "set_var", "name": "<variable name>", "value": "<value>"}
```

**unset_var** — Unset a shell variable.

```json
{"type": "unset_var", "name": "<variable name>"}
```

**get_func** — Retrieve a shell function definition.

```json
{"type": "get_func", "name": "<function name>"}
```

**unset_func** — Remove a shell function.

```json
{"type": "unset_func", "name": "<function name>"}
```

**get_alias** — Retrieve an alias expansion.

```json
{"type": "get_alias", "name": "<alias name>"}
```

**set_alias** — Define an alias.

```json
{"type": "set_alias", "name": "<alias name>", "value": "<expansion>"}
```

**unset_alias** — Remove an alias.

```json
{"type": "unset_alias", "name": "<alias name>"}
```

**set_trap** — Set a trap on a signal.

```json
{"type": "set_trap", "signal": "<sigspec>", "action": "<command>"}
```

**unset_trap** — Remove a trap.

```json
{"type": "unset_trap", "signal": "<sigspec>"}
```

**inspect** — List all items of a given type.

```json
{"type": "inspect", "target": "vars|funcs|aliases|traps"}
```

# Server messages

**var** — Variable value response.

```json
{"type": "var", "name": "<name>", "value": "<value>"}
```

**func** — Function definition response.

```json
{"type": "func", "name": "<name>", "body": "<function body>"}
```

**alias** — Alias expansion response.

```json
{"type": "alias", "name": "<name>", "value": "<expansion>"}
```

**ok** — Generic success acknowledgement (for set/unset operations).

```json
{"type": "ok"}
```

**error** — Operation failed.

```json
{"type": "error", "message": "<reason>"}
```

**inspect_result** — Bulk listing response.

```json
{"type": "inspect_result", "target": "<type>", "data": [...]}
```

The **data** array contains objects appropriate to the target type
(variable, function, alias, or trap entries).

# CHAN_OBSERVE (3)

The observe channel delivers server-push event notifications. Clients
do not send messages on this channel. Events are only emitted if
observation is enabled via a **configure** message on CHAN_CONTROL.

# Server messages

**pre_command** — Emitted before a command executes.

```json
{
  "level": 1,
  "type": "pre_command",
  "seq": N,
  "timestamp": N,
  "data": {
    "command": "<command string>",
    "cwd": "<working directory>",
    "line_number": N,
    "is_subshell": false,
    "is_async": false
  }
}
```

**post_command** — Emitted after a command completes.

```json
{
  "level": 1,
  "type": "post_command",
  "seq": N,
  "timestamp": N,
  "data": {
    "command": "<command string>",
    "exit_status": N,
    "signal_number": N,
    "duration_ms": N
  }
}
```

Fields common to all observe messages:

- **level** (integer): verbosity level of the event.
- **seq** (integer): monotonically increasing sequence number.
- **timestamp** (integer): Unix timestamp in seconds.
- **data** (object): event-specific payload.

# CHAN_DEBUG (4)

The debug channel provides interactive debugger control including
breakpoints, single-stepping, and AST inspection.

# Client messages

**enable** — Enable the debugger.

```json
{"type": "enable"}
```

**disable** — Disable the debugger.

```json
{"type": "disable"}
```

**break** — Set a breakpoint.

```json
{
  "type": "break",
  "kind": "command|line|function",
  "pattern": "<glob pattern>",
  "line": N,
  "condition": "<bash expression>"
}
```

- **kind**: breakpoint type — **command** (match command name),
  **line** (match source line), or **function** (match function name).
- **pattern**: glob pattern for command/function matching.
- **line**: line number for line breakpoints.
- **condition**: optional Bash expression; breakpoint only triggers
  when it evaluates to true (exit code 0).

**delete** — Delete a breakpoint.

```json
{"type": "delete", "id": N}
```

**enable_bp** — Enable a disabled breakpoint.

```json
{"type": "enable_bp", "id": N}
```

**disable_bp** — Disable a breakpoint without deleting it.

```json
{"type": "disable_bp", "id": N}
```

**list** — List all breakpoints.

```json
{"type": "list"}
```

**step** — Execute one command and break.

```json
{"type": "step"}
```

**continue** — Resume execution until next breakpoint.

```json
{"type": "continue"}
```

**inspect_ast** — Inspect the current command's AST. Only valid while
stopped at a breakpoint.

```json
{"type": "inspect_ast"}
```

**status** — Query debugger state.

```json
{"type": "status"}
```

# Server messages

**enable_ok** — Debugger enabled.

```json
{"type": "enable_ok"}
```

**disable_ok** — Debugger disabled.

```json
{"type": "disable_ok"}
```

**break_ok** — Breakpoint set successfully.

```json
{"type": "break_ok", "id": N}
```

**delete_ok** — Breakpoint deletion result.

```json
{"type": "delete_ok", "id": N, "found": true|false}
```

**enable_bp_ok** — Breakpoint enable result.

```json
{"type": "enable_bp_ok", "id": N, "found": true|false}
```

**disable_bp_ok** — Breakpoint disable result.

```json
{"type": "disable_bp_ok", "id": N, "found": true|false}
```

**breakpoints** — List of all breakpoints.

```json
{"type": "breakpoints", "data": [...]}
```

**step_ok** — Step acknowledged.

```json
{"type": "step_ok", "mode": "step"}
```

**continue_ok** — Continue acknowledged.

```json
{"type": "continue_ok"}
```

**break_hit** — Execution stopped at a breakpoint.

```json
{"type": "break_hit", "line": N, "command": "<command>", "depth": N}
```

When stopped at a breakpoint, the client may send additional commands
before resuming: **break**, **delete**, **enable_bp**, **disable_bp**,
**list**, **inspect_ast**, and **status**. Execution resumes with
**continue**, **step**, **next**, **finish**, or **skip**.

**ast** — AST inspection result.

```json
{"type": "ast", "data": { ... }}
```

The **data** field contains a COMMAND tree in the format documented in
**bash-server-cmd-json**(5).

**status** — Debugger status.

```json
{
  "type": "status",
  "active": true|false,
  "mode": "<current mode>",
  "breakpoints": N,
  "depth": N
}
```

# CHAN_PTY (5)

The PTY channel provides pseudo-terminal allocation and I/O for
interactive shell sessions.

# Client messages

**spawn** — Allocate a new PTY and start a shell.

```json
{
  "type": "spawn",
  "rows": N,
  "cols": N,
  "shell": "<shell path>",
  "strip_ansi": false
}
```

- **rows**, **cols**: initial terminal dimensions.
- **shell**: path to the shell to run (default: /bin/bash).
- **strip_ansi** (boolean): if true, strip ANSI escape sequences from
  output before sending to the client.

**input** — Send input to the PTY.

```json
{"type": "input", "data": "<base64-encoded input>", "encoding": "base64"}
```

**resize** — Resize the PTY.

```json
{"type": "resize", "rows": N, "cols": N}
```

**signal** — Send a signal to the PTY process.

```json
{"type": "signal", "signal": "<signal name>"}
```

**close** — Close the PTY session.

```json
{"type": "close"}
```

# Server messages

**spawn_ok** — PTY created successfully.

```json
{
  "type": "spawn_ok",
  "rows": N,
  "cols": N,
  "pid": N,
  "strip_ansi": false
}
```

**output** — PTY output data.

```json
{"type": "output", "data": "<base64-encoded output>", "encoding": "base64"}
```

**resize_ok** — Resize acknowledged.

```json
{"type": "resize_ok", "rows": N, "cols": N}
```

**signal_ok** — Signal delivered.

```json
{"type": "signal_ok", "signal": "<signal name>"}
```

**exit** — PTY process exited.

```json
{"type": "exit", "exit_code": N}
```

**error** — PTY operation failed.

```json
{"type": "error", "message": "<reason>"}
```

# SEE ALSO

**bash-server**(1),
**bash-server-v2-protocol**(5),
**bash-server-ndjson**(5),
**bash-server-v1-protocol**(5),
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
