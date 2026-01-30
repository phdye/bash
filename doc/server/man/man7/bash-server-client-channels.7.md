# BASH-SERVER-CLIENT-CHANNELS(7) -- Channel usage patterns from the client perspective

# DESCRIPTION

The bash-server v2 protocol multiplexes six logical channels over a
single transport connection.  All four client bindings (Python,
TypeScript, C, Java) expose these channels through a consistent API
pattern, though the details vary by language.

This page describes how clients interact with each channel, the
request/response versus server-push distinction, callback patterns,
ordering guarantees, and channel availability rules.

# CONCEPTS

# Channel architecture

The v2 protocol defines six channels, each carrying a distinct class
of messages.  All channels share a single transport connection.
Messages are multiplexed by a channel ID (integer 0-5) in the NDJSON
`channel` field.

| Channel      | ID | Client property | Message flow             |
|--------------|----|-----------------|--------------------------|
| CHAN_CONTROL  | 0  | `control`       | Request/response         |
| CHAN_COMMAND  | 1  | `command`       | Request/response         |
| CHAN_STATE    | 2  | `state`         | Request/response         |
| CHAN_OBSERVE  | 3  | `observe`       | Request/response + push  |
| CHAN_DEBUG    | 4  | `debug`         | Request/response + push  |
| CHAN_PTY      | 5  | `pty`           | Request/response + push  |

# Request/response channels

Channels 0-2 (CONTROL, COMMAND, STATE) use a strict request/response
pattern.  The client sends a request and waits for exactly one
response (or, in the case of COMMAND, a sequence of stdout/stderr/
complete messages).

The client bindings implement this with:

- **Python:** `await self._recv(channel)` which awaits an
  `asyncio.Queue.get()`.
- **TypeScript:** A Promise added to a per-channel queue; resolved by
  the reader loop when the response arrives.
- **C:** Synchronous read loop that blocks until the matching response
  arrives on the target channel.
- **Java:** `queue.poll(30, TimeUnit.SECONDS)` on a per-channel
  `BlockingQueue`.

# Server-push channels

Channels 3-5 (OBSERVE, DEBUG, PTY) support server-initiated messages
in addition to request/response.  These push messages arrive
asynchronously and are dispatched to registered callbacks.

Push message types:

| Channel      | Push messages        | Trigger                        |
|--------------|----------------------|--------------------------------|
| CHAN_OBSERVE  | `pre_command`        | Before command execution       |
| CHAN_OBSERVE  | `post_command`       | After command execution        |
| CHAN_DEBUG    | `break_hit`          | Breakpoint or step fires       |
| CHAN_PTY      | `output`             | PTY produces output            |
| CHAN_PTY      | `exit`               | PTY child process exits        |

The background reader (task, loop, or thread) distinguishes push
messages from responses by their `type` field and routes them to the
appropriate dispatch method.

# CHAN_CONTROL (0) -- Session lifecycle

CHAN_CONTROL manages the session lifecycle.  It is the only channel
that accepts messages before authentication.

**Auth flow:**

The client sends an `auth` message with the server token.  The server
responds with `auth_ok` (success) or `auth_fail` (failure).

```
Client                          Server
  |                               |
  |  {"ch":0,"type":"auth",       |
  |   "token":"abcdef..."}        |
  |------------------------------>|
  |                               |
  |  {"ch":0,"type":"auth_ok"}    |
  |<------------------------------|
```

Python:
```python
await client.auth(token)
```

TypeScript:
```typescript
await client.auth(token);
```

C:
```c
int rc = bc_auth(c, token);
```

Java:
```java
client.auth(token);
```

**Ping/pong:**

Keepalive mechanism.  The client sends `ping`, the server responds
with `pong`.

Python:
```python
await client.ping()
```

TypeScript:
```typescript
await client.ping();
```

C:
```c
bc_ping(c);
```

Java:
```java
client.ping();
```

**Configure:**

Set session parameters.  Currently supports `observe_level` (integer)
and `wire_format` (string) settings.

Python:
```python
await client.control.configure(observe_level=1)
```

TypeScript:
```typescript
await client.control.configure({ observe_level: 1 });
```

C:
```c
/* Configuration is implicit in bc_observe_subscribe() */
```

Java:
```java
client.control.configure("observe_level", 1);
```

**Disconnect:**

Graceful session teardown.  The client sends `disconnect` and the
server closes the connection.

Python:
```python
await client.close()  # sends disconnect internally
```

TypeScript:
```typescript
await client.close();  // sends disconnect internally
```

C:
```c
bc_close(c);  /* sends disconnect internally */
```

Java:
```java
client.close();  // sends disconnect internally
```

# CHAN_COMMAND (1) -- Command evaluation

CHAN_COMMAND evaluates shell commands and returns the results.  An
`eval` request produces a sequence of three response messages:

```
Client                          Server
  |                               |
  |  {"ch":1,"type":"eval",       |
  |   "command":"echo hello"}     |
  |------------------------------>|
  |                               |
  |  {"ch":1,"type":"stdout",     |
  |   "data":"aGVsbG8K",          |
  |   "encoding":"base64"}        |
  |<------------------------------|
  |                               |
  |  {"ch":1,"type":"stderr",     |
  |   "data":"",                  |
  |   "encoding":"base64"}        |
  |<------------------------------|
  |                               |
  |  {"ch":1,"type":"complete",   |
  |   "exit_code":0}              |
  |<------------------------------|
```

The `stdout` and `stderr` payloads are base64-encoded.  The client
bindings automatically decode them and return an `EvalResult` object:

Python:
```python
result = await client.eval("echo hello")
# result.stdout == "hello\n"
# result.stderr == ""
# result.exit_code == 0
```

TypeScript:
```typescript
const result = await client.eval("echo hello");
// result.stdout === "hello\n"
// result.stderr === ""
// result.exit_code === 0
```

C:
```c
bc_eval_result_t result;
bc_eval(c, "echo hello", &result);
/* result.stdout_data == "hello\n" */
/* result.stderr_data == "" */
/* result.exit_code == 0 */
bc_eval_result_free(&result);
```

Java:
```java
EvalResult result = client.eval("echo hello");
// result.stdout == "hello\n"
// result.stderr == ""
// result.exitCode == 0
```

The `eval` method blocks (or awaits) until all three messages are
received.  The order is always stdout, stderr, complete.

# CHAN_STATE (2) -- Shell state manipulation

CHAN_STATE provides direct access to four shell namespaces without
executing commands.  Each operation is a request/response pair.

**Namespaces:**

| Namespace  | Operations                        |
|------------|-----------------------------------|
| Variables  | get, set (with attributes), unset |
| Functions  | get (definition), unset           |
| Aliases    | get, set, unset                   |
| Traps      | set, unset                        |
| (all)      | inspect (list all items)          |

**Variable operations:**

Python:
```python
info = await client.state.get_var("PATH")
await client.state.set_var("MY_VAR", "value", attributes=["-x", "-r"])
await client.state.unset_var("MY_VAR")
```

TypeScript:
```typescript
const info = await client.state.getVar("PATH");
await client.state.setVar("MY_VAR", "value", ["-x", "-r"]);
await client.state.unsetVar("MY_VAR");
```

C:
```c
bc_var_info_t info;
bc_state_get_var(c, "PATH", &info);
bc_var_info_free(&info);

const char *attrs[] = {"-x", "-r"};
bc_state_set_var(c, "MY_VAR", "value", attrs, 2);
bc_state_unset_var(c, "MY_VAR");
```

Java:
```java
VarInfo info = client.state.getVar("PATH");
client.state.setVar("MY_VAR", "value", List.of("-x", "-r"));
client.state.unsetVar("MY_VAR");
```

**Function operations:**

Python:
```python
func = await client.state.get_func("my_func")
# func.definition contains the function body
await client.state.unset_func("my_func")
```

TypeScript:
```typescript
const func = await client.state.getFunc("my_func");
await client.state.unsetFunc("my_func");
```

C:
```c
char *definition;
bc_state_get_func(c, "my_func", &definition);
bc_free(definition);
bc_state_unset_func(c, "my_func");
```

Java:
```java
String definition = client.state.getFunc("my_func");
client.state.unsetFunc("my_func");
```

**Alias operations:**

Python:
```python
alias = await client.state.get_alias("ll")
await client.state.set_alias("ll", "ls -la")
await client.state.unset_alias("ll")
```

TypeScript:
```typescript
const alias = await client.state.getAlias("ll");
await client.state.setAlias("ll", "ls -la");
await client.state.unsetAlias("ll");
```

C:
```c
char *value;
bc_state_get_alias(c, "ll", &value);
bc_free(value);
bc_state_set_alias(c, "ll", "ls -la");
bc_state_unset_alias(c, "ll");
```

Java:
```java
String value = client.state.getAlias("ll");
client.state.setAlias("ll", "ls -la");
client.state.unsetAlias("ll");
```

**Trap operations:**

Python:
```python
await client.state.set_trap("SIGINT", "echo caught")
await client.state.unset_trap("SIGINT")
```

TypeScript:
```typescript
await client.state.setTrap("SIGINT", "echo caught");
await client.state.unsetTrap("SIGINT");
```

C:
```c
bc_state_set_trap(c, "SIGINT", "echo caught");
bc_state_unset_trap(c, "SIGINT");
```

Java:
```java
client.state.setTrap("SIGINT", "echo caught");
client.state.unsetTrap("SIGINT");
```

**Inspect:**

The inspect operation lists all items of a given type.  The query
string specifies the namespace: `"variables"`, `"functions"`,
`"aliases"`, or `"traps"`.

Python:
```python
items = await client.state.inspect("variables")
for item in items:
    print(f"{item['name']}={item.get('value', '')}")
```

TypeScript:
```typescript
const items = await client.state.inspect("variables");
for (const item of items) {
    console.log(`${item.name}=${item.value ?? ""}`);
}
```

C:
```c
char *json;
bc_state_inspect(c, "variables", &json);
printf("%s\n", json);
bc_free(json);
```

Java:
```java
List<Map<String, Object>> items = client.state.inspect("variables");
```

# CHAN_OBSERVE (3) -- Command observation events

CHAN_OBSERVE delivers pre-command and post-command events when
commands are executed on the server.  This channel requires explicit
subscription.

**Subscribe/unsubscribe** are request/response operations:

Python:
```python
await client.observe.subscribe(level=1)
await client.observe.unsubscribe()
```

TypeScript:
```typescript
await client.observe.subscribe(1);
await client.observe.unsubscribe();
```

C:
```c
bc_observe_subscribe(c, 1);
bc_observe_unsubscribe(c);
```

Java:
```java
client.observe.subscribe(1);
client.observe.unsubscribe();
```

**Events** are server-push messages dispatched to callbacks:

`pre_command` fires before a command executes and includes:
- `seq` -- Sequence number for ordering
- `timestamp` -- Unix timestamp (milliseconds)
- `command` -- The command string
- `cwd` -- Current working directory
- `line_number` -- Source line number
- `is_subshell` -- True if executing in a subshell
- `is_async` -- True if background execution

`post_command` fires after a command completes and includes:
- `seq` -- Sequence number
- `timestamp` -- Unix timestamp (milliseconds)
- `command` -- The command string
- `exit_status` -- Exit code
- `signal_number` -- Signal that killed the command (0 if none)
- `duration_ms` -- Execution time in milliseconds

**Callback registration per language:**

Python:
```python
def on_pre(event: PreCommandEvent):
    print(f"running: {event.command}")

async def on_post(event: PostCommandEvent):
    print(f"done: {event.command} ({event.duration_ms}ms)")

client.observe.on("pre_command", on_pre)
client.observe.on("post_command", on_post)
client.observe.off("pre_command", on_pre)
```

TypeScript:
```typescript
client.observe.on("pre_command", (event: PreCommandEvent) => {
    console.log(`running: ${event.command}`);
});
client.observe.on("post_command", (event: PostCommandEvent) => {
    console.log(`done: ${event.command}`);
});
client.observe.off("pre_command");
```

C:
```c
void on_pre(const bc_pre_command_event_t *e, void *ud) {
    printf("running: %s\n", e->command);
}
void on_post(const bc_post_command_event_t *e, void *ud) {
    printf("done: %s (%dms)\n", e->command, e->duration_ms);
}
bc_observe_on_pre_command(c, on_pre, NULL);
bc_observe_on_post_command(c, on_post, NULL);
/* Must call bc_poll() to dispatch events */
bc_poll(c, 1000);
```

Java:
```java
client.observe.onPreCommand(event ->
    System.out.println("running: " + event.command));
client.observe.onPostCommand(event ->
    System.out.println("done: " + event.command));
```

# CHAN_DEBUG (4) -- Interactive debugger

CHAN_DEBUG provides breakpoint management, execution control, and AST
inspection.  It combines request/response operations (enable, add
breakpoint, step, inspect) with server-push events (break_hit).

**Enable/disable:**

Python:
```python
await client.debug.enable()
await client.debug.disable()
status = await client.debug.status()
```

TypeScript:
```typescript
await client.debug.enable();
await client.debug.disable();
const status = await client.debug.status();
```

C:
```c
bc_debug_enable(c);
bc_debug_disable(c);
bc_debug_status_t status;
bc_debug_status(c, &status);
bc_debug_status_free(&status);
```

Java:
```java
client.debug.enable();
client.debug.disable();
DebugStatus status = client.debug.status();
```

**Breakpoint management:**

Three breakpoint kinds are supported: `"command"` (pattern matching
on command strings), `"line"` (source line number), and `"function"`
(function name pattern).

Python:
```python
bp_id = await client.debug.add_breakpoint("command", pattern="echo*")
bp_id2 = await client.debug.add_breakpoint("line", line=10)
bps = await client.debug.list_breakpoints()
await client.debug.remove_breakpoint(bp_id)
await client.debug.enable_breakpoint(bp_id2)
await client.debug.disable_breakpoint(bp_id2)
```

TypeScript:
```typescript
const bpId = await client.debug.addBreakpoint("command", { pattern: "echo*" });
const bpId2 = await client.debug.addBreakpoint("line", { line: 10 });
const bps = await client.debug.listBreakpoints();
await client.debug.removeBreakpoint(bpId);
```

C:
```c
int bp_id = bc_debug_add_breakpoint(c, "command", "echo*", -1, NULL);
int bp_id2 = bc_debug_add_breakpoint(c, "line", NULL, 10, NULL);
bc_breakpoint_t *bps; int count;
bc_debug_list_breakpoints(c, &bps, &count);
bc_breakpoint_list_free(bps, count);
bc_debug_remove_breakpoint(c, bp_id);
```

Java:
```java
int bpId = client.debug.addBreakpoint("command", "echo*", -1, null);
int bpId2 = client.debug.addBreakpoint("line", null, 10, null);
List<Breakpoint> bps = client.debug.listBreakpoints();
client.debug.removeBreakpoint(bpId);
```

**Execution control:**

Five execution control commands resume execution after a break_hit:

| Command    | Behavior                                  |
|------------|-------------------------------------------|
| `continue` | Run until next breakpoint                 |
| `step`     | Execute one command, step into functions  |
| `next`     | Execute one command, step over functions  |
| `finish`   | Run until current function returns        |
| `skip`     | Skip the current command without executing|

Python:
```python
await client.debug.continue_()  # trailing underscore
await client.debug.step()
await client.debug.next()
await client.debug.finish()
await client.debug.skip()
```

TypeScript:
```typescript
await client.debug.continue();
await client.debug.step();
await client.debug.next();
await client.debug.finish();
await client.debug.skip();
```

C:
```c
bc_debug_continue(c);
bc_debug_step(c);
bc_debug_next(c);
bc_debug_finish(c);
bc_debug_skip(c);
```

Java:
```java
client.debug.doContinue();  // avoid Java keyword
client.debug.step();
client.debug.next();
client.debug.finish();
client.debug.skip();
```

Note language keyword conflicts: Python uses `continue_()` (trailing
underscore), Java uses `doContinue()`.  TypeScript and C have no
conflict.

**AST inspection:**

Inspect the pending COMMAND tree as JSON when stopped at a breakpoint:

Python:
```python
ast = await client.debug.inspect_ast()
```

TypeScript:
```typescript
const ast = await client.debug.inspectAst();
```

C:
```c
char *json_ast;
bc_debug_inspect_ast(c, &json_ast);
bc_free(json_ast);
```

Java:
```java
String ast = client.debug.inspectAst();
```

**break_hit callback:**

The `break_hit` push event fires when a breakpoint or step boundary
is reached.  It includes `line`, `command`, and `depth` fields.

Python:
```python
async def on_break(event: BreakHitEvent):
    print(f"break: line {event.line}, {event.command}")
    await client.debug.step()

client.debug.on("break_hit", on_break)
```

TypeScript:
```typescript
client.debug.on("break_hit", (event: BreakHitEvent) => {
    console.log(`break: line ${event.line}, ${event.command}`);
    client.debug.step();
});
```

C:
```c
void on_break(const bc_break_hit_event_t *e, void *ud) {
    printf("break: line %d, %s\n", e->line, e->command);
    bc_debug_step((bc_client_t *)ud);
}
bc_debug_on_break_hit(c, on_break, c);
```

Java:
```java
client.debug.onBreakHit(event -> {
    System.out.printf("break: line %d, %s%n", event.line, event.command);
});
```

# CHAN_PTY (5) -- Pseudo-terminal I/O

CHAN_PTY spawns an interactive Bash session in a pseudo-terminal and
relays I/O between the client and the PTY.  It uses request/response
for spawn, resize, signal, and close, and server-push for output and
exit events.

**Spawn:**

Python:
```python
info = await client.pty.spawn(rows=24, cols=80, strip_ansi=True)
```

TypeScript:
```typescript
const info = await client.pty.spawn({ rows: 24, cols: 80, stripAnsi: true });
```

C:
```c
bc_pty_info_t info;
bc_pty_spawn(c, 24, 80, "/bin/bash", 1, &info);
```

Java:
```java
PtyInfo info = client.pty.spawn(24, 80, "/bin/bash", true);
```

The spawn response includes the PTY's `pid`, actual `rows` and `cols`,
and the `strip_ansi` setting.

**I/O relay:**

Client-to-PTY input is sent as base64-encoded data:

Python:
```python
await client.pty.write_input("ls -la\n")
```

TypeScript:
```typescript
await client.pty.writeInput("ls -la\n");
```

C:
```c
bc_pty_write(c, "ls -la\n", 7);
```

Java:
```java
client.pty.writeInput("ls -la\n");
```

PTY-to-client output arrives as `output` push events:

Python:
```python
client.pty.on("output", lambda data: print(data, end=""))
```

TypeScript:
```typescript
client.pty.on("output", (data: string) => process.stdout.write(data));
```

C:
```c
void on_out(const char *data, size_t len, void *ud) {
    fwrite(data, 1, len, stdout);
}
bc_pty_on_output(c, on_out, NULL);
```

Java:
```java
client.pty.onOutput((data, len) -> System.out.print(new String(data, 0, len)));
```

**Resize, signal, close:**

Python:
```python
await client.pty.resize(rows=48, cols=120)
await client.pty.signal("SIGINT")
await client.pty.close()
```

TypeScript:
```typescript
await client.pty.resize(48, 120);
await client.pty.signal("SIGINT");
await client.pty.close();
```

C:
```c
bc_pty_resize(c, 48, 120);
bc_pty_signal(c, "SIGINT");
bc_pty_close(c);
```

Java:
```java
client.pty.resize(48, 120);
client.pty.signal("SIGINT");
client.pty.close();
```

**Exit notification:**

When the PTY child process exits, the server sends an `exit` push
message with the exit code:

Python:
```python
client.pty.on("exit", lambda code: print(f"exited: {code}"))
```

TypeScript:
```typescript
client.pty.on("exit", (code: number) => console.log(`exited: ${code}`));
```

C:
```c
void on_exit(int code, void *ud) { printf("exited: %d\n", code); }
bc_pty_on_exit(c, on_exit, NULL);
```

Java:
```java
client.pty.onExit(code -> System.out.println("exited: " + code));
```

# Callback patterns per language

| Aspect           | Python              | TypeScript          | C                    | Java                |
|------------------|---------------------|---------------------|----------------------|---------------------|
| Registration     | `on(event, cb)`     | `on(event, cb)`     | `bc_*_on_*(c,cb,ud)` | `on*(consumer)`     |
| Unregistration   | `off(event[, cb])`  | `off(event[, cb])`  | pass NULL callback   | pass null           |
| Multiple per event | yes              | yes                 | no (one at a time)   | no (one at a time)  |
| Async callbacks  | yes (auto-detected) | no (sync only)      | N/A                  | N/A                 |
| Userdata         | N/A (use closures)  | N/A (use closures)  | `void *userdata`     | N/A (use lambdas)   |
| Invocation thread| reader task         | reader loop         | bc_poll() caller     | reader thread       |

# Channel availability

All channels are available after successful authentication.
CHAN_CONTROL is the only channel that accepts messages before
authentication -- specifically, the `auth` message.

Sending a message on any other channel before authentication results
in a `ServerError` / `BC_ERR_SERVER` response.

# Ordering guarantees

**Within a channel:** Messages are processed strictly in order.  A
response to request N always arrives before the response to request
N+1 on the same channel.

**Between channels:** Messages on different channels may be
interleaved.  The server processes requests from all channels
concurrently.

**Server-push ordering:** Push events (OBSERVE, DEBUG, PTY) include
sequence numbers (`seq` in observe events) that allow clients to
reconstruct the original order relative to command responses.

**Practical implication:** If you send an `eval` on CHAN_COMMAND and a
`get_var` on CHAN_STATE simultaneously, the responses may arrive in
either order.  The client bindings handle this correctly because each
channel has its own response queue.

# SEE ALSO

**bash-server-channels**(7),
**bash-server-client-api**(7),
**bash-server-client-python**(7),
**bash-server-client-typescript**(7),
**bash-server-client-c**(7),
**bash-server-client-java**(7),
**bash-server**(1),
**bash-server-observe**(7),
**bash-server-debug**(7),
**bash-server-pty**(7)

# AUTHORS

GNU Bash is Copyright (C) Free Software Foundation, Inc.
The bash-server extension was developed as part of the Cygwin Bash project.

# COPYRIGHT

This is free software; see the GNU General Public License v3 or later
for copying conditions.  There is NO warranty.
