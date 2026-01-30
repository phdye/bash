# bashclient Java API Reference

## Complete API Documentation

**Package**: `org.gnu.bash.client`
**Java**: 11+
**License**: GNU General Public License v3.0 or later

---

## Table of Contents

1. [BashClient](#bashclient)
2. [ControlChannel](#controlchannel)
3. [CommandChannel](#commandchannel)
4. [StateChannel](#statechannel)
5. [ObserveChannel](#observechannel)
6. [DebugChannel](#debugchannel)
7. [PtyChannel](#ptychannel)
8. [NdjsonProtocol](#ndjsonprotocol)
9. [Transport Interface](#transport-interface)
10. [UnixSocketTransport](#unixsockettransport)
11. [StdioTransport](#stdiotransport)
12. [NamedPipeTransport](#namedpipetransport)
13. [EvalResult](#evalresult)
14. [VarInfo](#varinfo)
15. [BreakHitEvent](#breakhitevent)
16. [PtyInfo](#ptyinfo)
17. [BashClientException](#bashclientexception)
18. [Constants](#constants)

---

## BashClient

```java
package org.gnu.bash.client;

public class BashClient implements AutoCloseable
```

The main entry point for connecting to a bash-server instance. Provides static
factory methods for different transport modes, convenience methods for common
operations, and public channel fields for advanced usage.

### Fields

| Modifier | Type | Name | Description |
|----------|------|------|-------------|
| `public final` | `ControlChannel` | `control` | Channel 0: auth, ping, disconnect, configure |
| `public final` | `CommandChannel` | `command` | Channel 1: eval, streaming output |
| `public final` | `StateChannel` | `state` | Channel 2: variables, functions, aliases, traps |
| `public final` | `ObserveChannel` | `observe` | Channel 3: pre/post command events |
| `public final` | `DebugChannel` | `debug` | Channel 4: breakpoints, stepping, AST |
| `public final` | `PtyChannel` | `pty` | Channel 5: pseudo-terminal |

### Static Factory Methods

---

#### connect

```java
public static BashClient connect(String socketPath) throws IOException
```

Creates a new BashClient connected via Unix domain socket.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `socketPath` | `String` | Absolute path to the Unix domain socket |

**Returns:** A new `BashClient` instance connected to the server.

**Throws:**

| Exception | Condition |
|-----------|-----------|
| `IOException` | If the socket cannot be opened or connected |
| `IllegalArgumentException` | If `socketPath` is null or blank |

**Example:**

```java
try (BashClient client = BashClient.connect("/tmp/bash-server-1000/sock")) {
    client.auth(token);
    EvalResult result = client.eval("echo hello");
}
```

---

#### connectStdio

```java
public static BashClient connectStdio(String... args) throws IOException
```

Creates a new BashClient by spawning a bash-server subprocess and communicating
over its stdin/stdout.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `args` | `String...` | Command and arguments to start bash-server (e.g., `"bash-server", "--stdio"`) |

**Returns:** A new `BashClient` instance connected via stdio.

**Throws:**

| Exception | Condition |
|-----------|-----------|
| `IOException` | If the process cannot be started |
| `IllegalArgumentException` | If `args` is null or empty |

**Example:**

```java
try (BashClient client = BashClient.connectStdio(
        "./bash-server/bash-server", "--stdio", "--login")) {
    client.auth(token);
    EvalResult result = client.eval("whoami");
}
```

---

#### connectNamedPipe

```java
public static BashClient connectNamedPipe(String pipeName) throws IOException
```

Creates a new BashClient connected via a Windows Named Pipe.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `pipeName` | `String` | The pipe name (without `\\.\pipe\` prefix) |

**Returns:** A new `BashClient` instance connected via Named Pipe.

**Throws:**

| Exception | Condition |
|-----------|-----------|
| `IOException` | If the pipe cannot be opened |
| `IllegalArgumentException` | If `pipeName` is null or blank |

**Example:**

```java
try (BashClient client = BashClient.connectNamedPipe("bash-server")) {
    client.auth(token);
    EvalResult result = client.eval("echo hello from Windows");
}
```

---

### Instance Methods

---

#### auth

```java
public void auth(String token) throws BashClientException
```

Authenticates the connection with the server's token.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `token` | `String` | The 256-bit authentication token (hex string) |

**Throws:**

| Exception | Condition |
|-----------|-----------|
| `BashClientException.AuthException` | If the token is rejected |
| `BashClientException.TransportException` | If the connection is lost |
| `BashClientException.TimeoutException` | If no response within timeout |
| `IllegalArgumentException` | If `token` is null or blank |

**Example:**

```java
String token = Files.readString(Path.of("/path/to/token")).trim();
client.auth(token);
```

---

#### eval

```java
public EvalResult eval(String command) throws BashClientException
```

Evaluates a bash command and returns the result. Convenience method that
delegates to `command.eval(command)`.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `command` | `String` | The bash command to evaluate |

**Returns:** An `EvalResult` containing stdout, stderr, and exit code.

**Throws:**

| Exception | Condition |
|-----------|-----------|
| `BashClientException.TimeoutException` | If no response within 30 seconds |
| `BashClientException.TransportException` | If the connection is lost |
| `BashClientException.ServerException` | If the server reports an error |
| `IllegalArgumentException` | If `command` is null or blank |

**Example:**

```java
EvalResult result = client.eval("ls -la /tmp");
System.out.println(result.getStdout());
```

---

#### ping

```java
public void ping() throws BashClientException
```

Sends a ping to the server and waits for the response. Useful for
checking connectivity and keeping the connection alive.

**Throws:**

| Exception | Condition |
|-----------|-----------|
| `BashClientException.TimeoutException` | If no pong within 30 seconds |
| `BashClientException.TransportException` | If the connection is lost |

**Example:**

```java
client.ping();
System.out.println("Server is alive");
```

---

#### isConnected

```java
public boolean isConnected()
```

Returns whether the transport is currently open.

**Returns:** `true` if the transport is open; `false` if closed or disconnected.

**Thread safety:** Safe to call from any thread (volatile read).

---

#### isAuthenticated

```java
public boolean isAuthenticated()
```

Returns whether authentication has been completed successfully.

**Returns:** `true` if `auth()` succeeded; `false` otherwise.

**Thread safety:** Safe to call from any thread (volatile read).

---

#### close

```java
@Override
public void close() throws IOException
```

Closes the connection and releases all resources. Sends a disconnect
message to the server (best effort), then closes the transport.
Idempotent: calling `close()` multiple times is safe.

**Throws:**

| Exception | Condition |
|-----------|-----------|
| `IOException` | If the transport close fails |

**Example:**

```java
// Explicit close
BashClient client = BashClient.connect(socketPath);
try {
    client.auth(token);
    client.eval("echo hello");
} finally {
    client.close();
}

// Or with try-with-resources (preferred)
try (BashClient client = BashClient.connect(socketPath)) {
    client.auth(token);
    client.eval("echo hello");
}
```

---

## ControlChannel

```java
package org.gnu.bash.client.channels;

public class ControlChannel
```

Channel 0: Connection-level control operations.

### Methods

---

#### ping

```java
public void ping() throws BashClientException
```

Sends a ping and waits for pong.

**Throws:** `TimeoutException`, `TransportException`

---

#### disconnect

```java
public void disconnect() throws BashClientException
```

Sends a graceful disconnect request to the server.

**Throws:** `TransportException`

---

#### configure

```java
public void configure(String key, String value) throws BashClientException
```

Sets a session configuration option on the server.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `key` | `String` | Configuration key (e.g., `"observe_level"`, `"wire_format"`) |
| `value` | `String` | Configuration value (e.g., `"full"`, `"ndjson"`) |

**Supported keys:**

| Key | Values | Description |
|-----|--------|-------------|
| `observe_level` | `"off"`, `"basic"`, `"full"` | Set observation detail level |
| `wire_format` | `"ndjson"`, `"binary"` | Set wire protocol format |

**Throws:** `ServerException`, `TimeoutException`, `TransportException`

**Example:**

```java
client.control.configure("observe_level", "full");
```

---

## CommandChannel

```java
package org.gnu.bash.client.channels;

public class CommandChannel
```

Channel 1: Command execution.

### Methods

---

#### eval

```java
public EvalResult eval(String command) throws BashClientException
```

Evaluates a command with the default 30-second timeout.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `command` | `String` | Bash command to evaluate |

**Returns:** `EvalResult` with stdout, stderr, exit code.

**Throws:** `TimeoutException`, `TransportException`, `ServerException`

**Example:**

```java
EvalResult result = client.command.eval("date +%Y-%m-%d");
String today = result.getStdout().trim();
```

---

#### eval (with timeout)

```java
public EvalResult eval(String command, Duration timeout) throws BashClientException
```

Evaluates a command with a custom timeout.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `command` | `String` | Bash command to evaluate |
| `timeout` | `Duration` | Maximum wait time for the response |

**Returns:** `EvalResult`

**Throws:** `TimeoutException`, `TransportException`, `ServerException`

**Example:**

```java
EvalResult result = client.command.eval("find / -name '*.log'",
    Duration.ofMinutes(5));
```

---

#### eval (with callbacks)

```java
public EvalResult eval(String command,
                        Consumer<String> onStdout,
                        Consumer<String> onStderr) throws BashClientException
```

Evaluates a command with streaming output callbacks. Callbacks are invoked
as output arrives, before the final result is returned.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `command` | `String` | Bash command to evaluate |
| `onStdout` | `Consumer<String>` | Called with each stdout chunk; may be null |
| `onStderr` | `Consumer<String>` | Called with each stderr chunk; may be null |

**Returns:** `EvalResult` with complete stdout, stderr, exit code.

**Throws:** `TimeoutException`, `TransportException`, `ServerException`

**Example:**

```java
EvalResult result = client.command.eval("make -j12",
    out -> System.out.print(out),
    err -> System.err.print(err));
```

---

#### evalParsed

```java
public EvalResult evalParsed(ObjectNode commandTree) throws BashClientException
```

Evaluates a pre-parsed COMMAND tree (JSON). The command tree must conform
to the bash-server COMMAND JSON schema.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `commandTree` | `ObjectNode` | Serialized COMMAND tree JSON |

**Returns:** `EvalResult`

**Throws:** `TimeoutException`, `TransportException`, `ServerException`, `ProtocolException`

**Example:**

```java
ObjectMapper mapper = new ObjectMapper();
ObjectNode tree = mapper.readValue(
    "{\"type\":\"simple\",\"words\":[\"echo\",\"hello\"]}", ObjectNode.class);
EvalResult result = client.command.evalParsed(tree);
```

---

## StateChannel

```java
package org.gnu.bash.client.channels;

public class StateChannel
```

Channel 2: Environment state management -- variables, functions, aliases, traps.

### Variable Methods

---

#### getVar

```java
public VarInfo getVar(String name) throws BashClientException
```

Gets a variable's value and metadata.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `name` | `String` | Variable name (e.g., `"PATH"`, `"HOME"`) |

**Returns:** `VarInfo` with value, type, exported, readonly flags.

**Throws:** `ServerException` (if variable not found), `TimeoutException`, `TransportException`

**Example:**

```java
VarInfo path = client.state.getVar("PATH");
System.out.println("PATH=" + path.getValue());
System.out.println("Exported: " + path.isExported());
```

---

#### setVar

```java
public void setVar(String name, String value) throws BashClientException
```

Sets a string variable.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `name` | `String` | Variable name |
| `value` | `String` | Variable value |

**Throws:** `ServerException`, `TimeoutException`, `TransportException`

---

#### setVar (exported)

```java
public void setVar(String name, String value, boolean exported)
    throws BashClientException
```

Sets a string variable with export control.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `name` | `String` | Variable name |
| `value` | `String` | Variable value |
| `exported` | `boolean` | Whether to export the variable |

---

#### setVar (array)

```java
public void setVar(String name, String[] values) throws BashClientException
```

Sets an indexed array variable.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `name` | `String` | Variable name |
| `values` | `String[]` | Array element values |

---

#### unsetVar

```java
public void unsetVar(String name) throws BashClientException
```

Unsets (removes) a variable.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `name` | `String` | Variable name to unset |

**Throws:** `ServerException`, `TimeoutException`, `TransportException`

---

#### listVars

```java
public List<String> listVars() throws BashClientException
```

Lists all variable names in the current environment.

**Returns:** List of variable names.

---

#### listVars (filtered)

```java
public List<String> listVars(String pattern) throws BashClientException
```

Lists variable names matching a glob pattern.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `pattern` | `String` | Glob pattern (e.g., `"*PATH*"`, `"BASH_*"`) |

**Returns:** List of matching variable names.

---

### Function Methods

---

#### getFunc

```java
public String getFunc(String name) throws BashClientException
```

Gets a function's definition.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `name` | `String` | Function name |

**Returns:** Function definition string (e.g., `"greet () { echo hello; }"`).

**Throws:** `ServerException` (if not found), `TimeoutException`, `TransportException`

---

#### setFunc

```java
public void setFunc(String name, String definition) throws BashClientException
```

Sets (defines) a function.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `name` | `String` | Function name |
| `definition` | `String` | Complete function definition including name and body |

**Example:**

```java
client.state.setFunc("greet", "greet() { echo \"Hello, $1!\"; }");
```

---

#### unsetFunc

```java
public void unsetFunc(String name) throws BashClientException
```

Unsets (removes) a function.

---

#### listFuncs

```java
public List<String> listFuncs() throws BashClientException
```

Lists all function names.

**Returns:** List of function names.

---

### Alias Methods

---

#### getAlias

```java
public String getAlias(String name) throws BashClientException
```

Gets an alias's value.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `name` | `String` | Alias name |

**Returns:** Alias expansion string.

**Example:**

```java
String ll = client.state.getAlias("ll");
// "ls -la --color=auto"
```

---

#### setAlias

```java
public void setAlias(String name, String value) throws BashClientException
```

Sets an alias.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `name` | `String` | Alias name |
| `value` | `String` | Alias expansion |

---

#### unsetAlias

```java
public void unsetAlias(String name) throws BashClientException
```

Removes an alias.

---

#### listAliases

```java
public List<String> listAliases() throws BashClientException
```

Lists all alias names.

**Returns:** List of alias names.

---

### Trap Methods

---

#### getTrap

```java
public String getTrap(String signal) throws BashClientException
```

Gets a trap's action for a signal.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `signal` | `String` | Signal name (e.g., `"INT"`, `"EXIT"`, `"ERR"`) |

**Returns:** Trap action string.

---

#### setTrap

```java
public void setTrap(String signal, String action) throws BashClientException
```

Sets a trap action for a signal.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `signal` | `String` | Signal name |
| `action` | `String` | Command to execute when signal is received |

---

#### unsetTrap

```java
public void unsetTrap(String signal) throws BashClientException
```

Removes a trap for a signal.

---

#### listTraps

```java
public List<String> listTraps() throws BashClientException
```

Lists all signals with active traps.

**Returns:** List of signal names.

---

### Inspect Method

---

#### inspect

```java
public ObjectNode inspect(String... namespaces) throws BashClientException
```

Retrieves a bulk snapshot of the environment. If no namespaces are specified,
returns all namespaces.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `namespaces` | `String...` | Namespaces to include: `"variables"`, `"functions"`, `"aliases"`, `"traps"` |

**Returns:** `ObjectNode` with keys for each requested namespace.

**Example:**

```java
ObjectNode snapshot = client.state.inspect("variables", "aliases");
ObjectNode vars = (ObjectNode) snapshot.get("variables");
ObjectNode aliases = (ObjectNode) snapshot.get("aliases");
```

---

## ObserveChannel

```java
package org.gnu.bash.client.channels;

public class ObserveChannel
```

Channel 3: Pre/post command event subscription and notification.

### Methods

---

#### onPreCommand

```java
public void onPreCommand(Consumer<ObjectNode> listener)
```

Registers a callback for pre-command events. The callback is invoked on
the daemon reader thread when the server sends a pre-command notification.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `listener` | `Consumer<ObjectNode>` | Callback receiving the event JSON |

**Event fields:**

| Field | Type | Description |
|-------|------|-------------|
| `event` | `String` | `"pre_command"` |
| `command` | `String` | Command text about to execute |
| `cwd` | `String` | Current working directory |
| `timestamp` | `long` | Unix timestamp in milliseconds |

**Example:**

```java
client.observe.onPreCommand(event -> {
    System.out.println("Executing: " + event.get("command").asText());
});
```

---

#### onPostCommand

```java
public void onPostCommand(Consumer<ObjectNode> listener)
```

Registers a callback for post-command events.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `listener` | `Consumer<ObjectNode>` | Callback receiving the event JSON |

**Event fields:**

| Field | Type | Description |
|-------|------|-------------|
| `event` | `String` | `"post_command"` |
| `command` | `String` | Command text that executed |
| `exit_code` | `int` | Command exit status |
| `cwd` | `String` | Current working directory after execution |
| `duration_ms` | `long` | Execution time in milliseconds |
| `timestamp` | `long` | Unix timestamp in milliseconds |

**Example:**

```java
client.observe.onPostCommand(event -> {
    System.out.printf("%s exited %d in %dms%n",
        event.get("command").asText(),
        event.get("exit_code").asInt(),
        event.get("duration_ms").asLong());
});
```

---

#### poll

```java
public ObjectNode poll(Duration timeout) throws BashClientException
```

Polls for the next observe event, blocking up to the specified timeout.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `timeout` | `Duration` | Maximum wait time |

**Returns:** The next event `ObjectNode`, or `null` if timeout expires.

**Throws:** `TransportException` if interrupted.

---

#### removePreCommandListeners

```java
public void removePreCommandListeners()
```

Removes all pre-command callback listeners.

---

#### removePostCommandListeners

```java
public void removePostCommandListeners()
```

Removes all post-command callback listeners.

---

#### removeAllListeners

```java
public void removeAllListeners()
```

Removes all callback listeners (both pre and post).

---

## DebugChannel

```java
package org.gnu.bash.client.channels;

public class DebugChannel
```

Channel 4: Breakpoints, stepping, and AST inspection.

### Breakpoint Methods

---

#### addBreakpoint

```java
public int addBreakpoint(String type, String pattern) throws BashClientException
```

Adds a new breakpoint.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `type` | `String` | Breakpoint type: `"command"`, `"line"`, `"function"` |
| `pattern` | `String` | Match pattern (command glob, `"file:line"`, function name) |

**Returns:** The breakpoint ID (integer).

**Throws:** `ServerException`, `TimeoutException`, `TransportException`

**Example:**

```java
int bpId = client.debug.addBreakpoint("function", "deploy");
```

---

#### removeBreakpoint

```java
public void removeBreakpoint(int breakpointId) throws BashClientException
```

Removes a breakpoint by ID.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `breakpointId` | `int` | The breakpoint ID returned by `addBreakpoint` |

---

#### listBreakpoints

```java
public List<ObjectNode> listBreakpoints() throws BashClientException
```

Lists all breakpoints.

**Returns:** List of breakpoint objects with fields: `id`, `type`, `pattern`, `enabled`.

---

#### enableBreakpoint

```java
public void enableBreakpoint(int breakpointId) throws BashClientException
```

Enables a previously disabled breakpoint.

---

#### disableBreakpoint

```java
public void disableBreakpoint(int breakpointId) throws BashClientException
```

Disables a breakpoint without removing it.

---

### Stepping Methods

---

#### step

```java
public void step() throws BashClientException
```

Steps to the next command (step into).

---

#### next

```java
public void next() throws BashClientException
```

Steps over the current command (skip function internals).

---

#### finish

```java
public void finish() throws BashClientException
```

Runs until the current function returns.

---

#### continueExecution

```java
public void continueExecution() throws BashClientException
```

Continues execution until the next breakpoint or completion.

---

#### skip

```java
public void skip() throws BashClientException
```

Skips the current command (does not execute it) and advances to the next.

---

### Inspection Methods

---

#### inspectAst

```java
public ObjectNode inspectAst() throws BashClientException
```

Returns the AST (abstract syntax tree) of the current command at a
breakpoint. Only valid when execution is paused at a breakpoint.

**Returns:** `ObjectNode` representing the COMMAND tree JSON.

**Throws:** `ServerException` (if not at a breakpoint), `TimeoutException`, `TransportException`

**Example:**

```java
ObjectNode ast = client.debug.inspectAst();
System.out.println("Type: " + ast.get("type").asText());
System.out.println(ast.toPrettyString());
```

---

### Callback Methods

---

#### onBreakHit

```java
public void onBreakHit(Consumer<BreakHitEvent> listener)
```

Registers a callback for breakpoint hit events.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `listener` | `Consumer<BreakHitEvent>` | Callback receiving the break event |

**Example:**

```java
client.debug.onBreakHit(event -> {
    System.out.printf("Break #%d at %s:%d: %s%n",
        event.getBreakpointId(),
        event.getFile(),
        event.getLine(),
        event.getCommand());
});
```

---

#### removeBreakHitListeners

```java
public void removeBreakHitListeners()
```

Removes all breakpoint hit callback listeners.

---

## PtyChannel

```java
package org.gnu.bash.client.channels;

public class PtyChannel
```

Channel 5: Pseudo-terminal session management.

### Methods

---

#### spawn

```java
public PtyInfo spawn() throws BashClientException
```

Spawns a new PTY with default dimensions (80 columns, 24 rows).

**Returns:** `PtyInfo` with the PTY's PID and dimensions.

**Throws:** `ServerException`, `TimeoutException`, `TransportException`

---

#### spawn (with dimensions)

```java
public PtyInfo spawn(int cols, int rows) throws BashClientException
```

Spawns a new PTY with specified dimensions.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `cols` | `int` | Terminal width in columns |
| `rows` | `int` | Terminal height in rows |

**Returns:** `PtyInfo`

---

#### spawn (with dimensions and ANSI stripping)

```java
public PtyInfo spawn(int cols, int rows, boolean stripAnsi)
    throws BashClientException
```

Spawns a new PTY with optional ANSI escape sequence stripping.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `cols` | `int` | Terminal width in columns |
| `rows` | `int` | Terminal height in rows |
| `stripAnsi` | `boolean` | If true, strip ANSI escape sequences from output |

**Returns:** `PtyInfo`

**Example:**

```java
PtyInfo info = client.pty.spawn(120, 40, true);
System.out.println("PTY PID: " + info.getPid());
```

---

#### writeInput

```java
public void writeInput(String data) throws BashClientException
```

Sends input data to the PTY.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `data` | `String` | Data to write (may include control characters like `\n`, `\t`, `\u0003`) |

**Example:**

```java
client.pty.writeInput("ls -la\n");
client.pty.writeInput("\u0003");  // Ctrl+C
```

---

#### readOutput

```java
public String readOutput(Duration timeout) throws BashClientException
```

Reads available PTY output, blocking up to the specified timeout.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `timeout` | `Duration` | Maximum wait time |

**Returns:** Output data string, or `null` if timeout expires with no data.

---

#### onOutput

```java
public void onOutput(Consumer<String> listener)
```

Registers a callback for PTY output data.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `listener` | `Consumer<String>` | Callback receiving output strings |

---

#### resize

```java
public void resize(int cols, int rows) throws BashClientException
```

Resizes the PTY dimensions.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `cols` | `int` | New terminal width |
| `rows` | `int` | New terminal height |

---

#### signal

```java
public void signal(String signalName) throws BashClientException
```

Sends a signal to the PTY process.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `signalName` | `String` | Signal name: `"INT"`, `"TERM"`, `"KILL"`, `"WINCH"`, etc. |

---

#### configure

```java
public void configure(String key, Object value) throws BashClientException
```

Configures a PTY option.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `key` | `String` | Configuration key (e.g., `"strip_ansi"`) |
| `value` | `Object` | Configuration value |

---

#### close

```java
public void close() throws BashClientException
```

Closes the PTY session. This terminates the PTY process on the server.

---

#### removeOutputListeners

```java
public void removeOutputListeners()
```

Removes all PTY output callback listeners.

---

## NdjsonProtocol

```java
package org.gnu.bash.client;

public class NdjsonProtocol
```

Handles NDJSON (Newline-Delimited JSON) serialization and deserialization
for the bash-server wire protocol.

### Constructor

```java
public NdjsonProtocol(OutputStream output)
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `output` | `OutputStream` | The output stream to write NDJSON frames to |

### Methods

---

#### send

```java
public void send(int channel, ObjectNode payload) throws IOException
```

Sends a message on the specified channel. Thread-safe: concurrent calls
are serialized with an internal write lock.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `channel` | `int` | Channel ID (0-5) |
| `payload` | `ObjectNode` | Message payload (channel field is set automatically) |

---

#### parse

```java
public ObjectNode parse(String line) throws BashClientException.ProtocolException
```

Parses a single NDJSON line into a Jackson ObjectNode.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `line` | `String` | A single line of NDJSON text |

**Returns:** Parsed `ObjectNode`.

**Throws:** `ProtocolException` if the line is not valid JSON.

---

#### createMessage

```java
public ObjectNode createMessage()
```

Creates a new empty ObjectNode for building a message.

**Returns:** Empty `ObjectNode`.

---

#### getMapper

```java
public ObjectMapper getMapper()
```

Returns the shared Jackson ObjectMapper.

**Returns:** The `ObjectMapper` instance used for JSON processing.

---

## Transport Interface

```java
package org.gnu.bash.client;

public interface Transport extends Closeable
```

Abstract transport layer for communication with bash-server.

### Methods

---

#### getInputStream

```java
InputStream getInputStream() throws IOException
```

Returns the input stream for reading server messages.

---

#### getOutputStream

```java
OutputStream getOutputStream() throws IOException
```

Returns the output stream for sending client messages.

---

#### isOpen

```java
boolean isOpen()
```

Returns whether the transport is currently open and usable.

---

#### close

```java
void close() throws IOException
```

Closes the transport and releases all resources.

---

## UnixSocketTransport

```java
package org.gnu.bash.client.transport;

public class UnixSocketTransport implements Transport
```

Transport implementation using Unix domain sockets via junixsocket.

### Constructor

```java
public UnixSocketTransport(String socketPath) throws IOException
```

Connects to a Unix domain socket.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `socketPath` | `String` | Absolute path to the Unix domain socket file |

**Throws:** `IOException` if the connection fails.

---

## StdioTransport

```java
package org.gnu.bash.client.transport;

public class StdioTransport implements Transport
```

Transport implementation that spawns a bash-server subprocess and
communicates over its stdin/stdout.

### Constructor

```java
public StdioTransport(String... command) throws IOException
```

Starts a subprocess with the given command.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `command` | `String...` | Command and arguments to execute |

**Throws:** `IOException` if the process cannot be started.

---

## NamedPipeTransport

```java
package org.gnu.bash.client.transport;

public class NamedPipeTransport implements Transport
```

Transport implementation using Windows Named Pipes.

### Constructor

```java
public NamedPipeTransport(String pipeName) throws IOException
```

Opens a Named Pipe for communication.

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `pipeName` | `String` | Pipe name (without `\\.\pipe\` prefix) |

**Throws:** `IOException` if the pipe cannot be opened.

---

## EvalResult

```java
package org.gnu.bash.client.types;

public class EvalResult
```

Immutable result of a command evaluation. Contains the standard output,
standard error, and exit code.

### Constructor

```java
public EvalResult(String stdout, String stderr, int exitCode)
```

### Methods

---

#### getStdout

```java
public String getStdout()
```

Returns the command's standard output.

**Returns:** stdout content (may be empty, never null).

---

#### getStderr

```java
public String getStderr()
```

Returns the command's standard error output.

**Returns:** stderr content (may be empty, never null).

---

#### getExitCode

```java
public int getExitCode()
```

Returns the command's exit code.

**Returns:** Exit code (0 for success, non-zero for failure).

---

#### isSuccess

```java
public boolean isSuccess()
```

Returns whether the command succeeded (exit code 0).

**Returns:** `true` if exit code is 0.

---

#### toString

```java
@Override
public String toString()
```

Returns a string representation including exit code and truncated output.

**Example output:** `"EvalResult{exitCode=0, stdout='hello\n', stderr=''}"`

---

## VarInfo

```java
package org.gnu.bash.client.types;

public class VarInfo
```

Immutable variable metadata returned by `StateChannel.getVar()`.

### Constructor

```java
public VarInfo(String value, String type, boolean exported, boolean readonly)
```

### Methods

---

#### getValue

```java
public String getValue()
```

Returns the variable's value.

**Returns:** Variable value string.

---

#### getType

```java
public String getType()
```

Returns the variable's type.

**Returns:** One of: `"string"`, `"array"`, `"assoc"`, `"integer"`, `"nameref"`.

---

#### isExported

```java
public boolean isExported()
```

Returns whether the variable is exported.

---

#### isReadonly

```java
public boolean isReadonly()
```

Returns whether the variable is readonly.

---

#### toString

```java
@Override
public String toString()
```

**Example output:** `"VarInfo{value='/usr/bin:/bin', type='string', exported=true, readonly=false}"`

---

## BreakHitEvent

```java
package org.gnu.bash.client.types;

public class BreakHitEvent
```

Immutable breakpoint hit event received from the debug channel.

### Constructor

```java
public BreakHitEvent(int breakpointId, String command, String file, int line)
```

### Methods

---

#### getBreakpointId

```java
public int getBreakpointId()
```

Returns the ID of the breakpoint that was hit.

---

#### getCommand

```java
public String getCommand()
```

Returns the command text at the breakpoint location.

---

#### getFile

```java
public String getFile()
```

Returns the source file name, or `null` for interactive input.

---

#### getLine

```java
public int getLine()
```

Returns the line number in the source file.

---

#### toString

```java
@Override
public String toString()
```

**Example output:** `"BreakHitEvent{bp=1, command='rm -rf /tmp', file='deploy.sh', line=42}"`

---

## PtyInfo

```java
package org.gnu.bash.client.types;

public class PtyInfo
```

Immutable PTY session information returned by `PtyChannel.spawn()`.

### Constructor

```java
public PtyInfo(int pid, int cols, int rows)
```

### Methods

---

#### getPid

```java
public int getPid()
```

Returns the PID of the PTY process on the server.

---

#### getCols

```java
public int getCols()
```

Returns the terminal width in columns.

---

#### getRows

```java
public int getRows()
```

Returns the terminal height in rows.

---

#### toString

```java
@Override
public String toString()
```

**Example output:** `"PtyInfo{pid=12345, cols=80, rows=24}"`

---

## BashClientException

```java
package org.gnu.bash.client;

public class BashClientException extends Exception
```

Base checked exception for all bashclient errors. Contains five inner
exception classes for specific error categories.

### Constructors

```java
public BashClientException(String message)
public BashClientException(String message, Throwable cause)
```

### Inner Classes

---

### AuthException

```java
public static class AuthException extends BashClientException
```

Thrown when authentication fails (wrong token, expired, etc.).

#### Constructor

```java
public AuthException(String message)
```

---

### ProtocolException

```java
public static class ProtocolException extends BashClientException
```

Thrown when the wire protocol is violated (malformed JSON, unexpected
message format).

#### Constructor

```java
public ProtocolException(String message, String rawData)
```

#### Methods

```java
public String getRawData()
```

Returns the raw data that caused the protocol error.

---

### TimeoutException

```java
public static class TimeoutException extends BashClientException
```

Thrown when a response is not received within the timeout period.

#### Constructor

```java
public TimeoutException(String message, Duration timeout)
public TimeoutException(String message, Duration timeout,
                         String channel, String action)
```

#### Methods

```java
public Duration getTimeout()
```

Returns the timeout duration that expired.

```java
public String getChannel()
```

Returns the channel name where the timeout occurred (may be null).

```java
public String getAction()
```

Returns the action that timed out (may be null).

---

### TransportException

```java
public static class TransportException extends BashClientException
```

Thrown when the transport layer fails (connection lost, pipe broken, etc.).

#### Constructor

```java
public TransportException(String message)
public TransportException(String message, Throwable cause)
```

---

### ServerException

```java
public static class ServerException extends BashClientException
```

Thrown when the server reports an error in its response.

#### Constructor

```java
public ServerException(String message)
public ServerException(String message, String errorCode)
```

#### Methods

```java
public String getErrorCode()
```

Returns the server's error code (may be null).

---

## Constants

### Channel IDs

Defined as package-private in `BashClient`:

| Constant | Value | Channel |
|----------|-------|---------|
| `CHAN_CONTROL` | `0` | Control |
| `CHAN_COMMAND` | `1` | Command |
| `CHAN_STATE` | `2` | State |
| `CHAN_OBSERVE` | `3` | Observe |
| `CHAN_DEBUG` | `4` | Debug |
| `CHAN_PTY` | `5` | PTY |

### Timeouts

| Constant | Value | Usage |
|----------|-------|-------|
| `DEFAULT_TIMEOUT` | `Duration.ofSeconds(30)` | Default response timeout |

### Protocol

| Constant | Value | Usage |
|----------|-------|-------|
| `NDJSON_CHARSET` | `StandardCharsets.UTF_8` | Wire encoding |

---

## See Also

- [Guide](GUIDE.md) -- usage patterns and examples
- [Architecture](ARCHITECTURE.md) -- internal design details
- [Examples](examples/README.md) -- runnable code samples
