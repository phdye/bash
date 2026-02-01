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
