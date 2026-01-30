# bashclient Java Guide

## Comprehensive Usage Guide

**Package**: `org.gnu.bash.client`
**Java**: 11+
**License**: GNU General Public License v3.0 or later

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Core Concepts](#core-concepts)
3. [Getting Started](#getting-started)
4. [Transport Modes](#transport-modes)
5. [Authentication](#authentication)
6. [Command Execution](#command-execution)
7. [Control Channel](#control-channel)
8. [Command Channel](#command-channel)
9. [State Channel](#state-channel)
10. [Observe Channel](#observe-channel)
11. [Debug Channel](#debug-channel)
12. [PTY Channel](#pty-channel)
13. [Error Handling](#error-handling)
14. [Lifecycle Management](#lifecycle-management)
15. [Thread Safety](#thread-safety)
16. [Observer Pattern](#observer-pattern)
17. [Best Practices](#best-practices)
18. [Recipes](#recipes)

---

## Prerequisites

Before using the bashclient Java library, ensure:

1. **Java 11+** is installed and `JAVA_HOME` is set
2. **bash-server** is built and available (see main project docs)
3. **Dependencies** are resolved (junixsocket, Jackson) via Maven
4. A running **bash-server** instance to connect to

See [INSTALL.md](INSTALL.md) for detailed installation instructions.

### Starting a bash-server

```bash
# Start with default settings
./bash-server/bash-server --name myapp

# Start with explicit socket path
./bash-server/bash-server --socket /tmp/my-bash.sock

# Start for Java client compatibility (disable SO_PEERCRED)
./bash-server/bash-server --name myapp --no-peercred

# Get the authentication token
cat ~/.config/bash-server/myapp/token
```

---

## Core Concepts

### AutoCloseable and try-with-resources

`BashClient` implements `AutoCloseable`, ensuring the connection is properly
closed even if an exception occurs. Always use try-with-resources:

```java
import org.gnu.bash.client.BashClient;
import org.gnu.bash.client.types.EvalResult;

try (BashClient client = BashClient.connect("/tmp/bash-server-1000/sock")) {
    client.auth(token);
    EvalResult result = client.eval("echo hello");
    System.out.println(result.getStdout());
}
// Connection is automatically closed here, even on exception
```

If you cannot use try-with-resources, call `close()` explicitly in a finally
block:

```java
BashClient client = null;
try {
    client = BashClient.connect(socketPath);
    client.auth(token);
    // ... use client ...
} finally {
    if (client != null) {
        client.close();
    }
}
```

### Daemon Reader Thread

Internally, `BashClient` spawns a single daemon thread that continuously reads
from the server connection. This thread:

- Runs as a daemon thread (does not prevent JVM shutdown)
- Parses incoming NDJSON frames
- Routes messages to the appropriate channel's `BlockingQueue`
- Handles connection errors by marking the client as disconnected

You never interact with this thread directly. It starts automatically when
the connection is established and stops when `close()` is called.

### BlockingQueue per Channel

Each of the six channels (control, command, state, observe, debug, pty) has
its own `BlockingQueue<ObjectNode>` for incoming messages. When you call a
channel method like `eval()`, it:

1. Sends the request JSON to the server
2. Polls the channel's queue with a 30-second timeout
3. Returns the parsed response or throws `TimeoutException`

This design means:

- Each channel operates independently
- Multiple threads can use different channels concurrently
- A slow response on one channel does not block others
- The 30-second default timeout prevents indefinite blocking

### Jackson ObjectNode

All JSON messages are represented as Jackson `ObjectNode` instances. The
library handles serialization/deserialization internally, but you may encounter
`ObjectNode` when working with raw channel methods or custom extensions:

```java
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.databind.ObjectMapper;

ObjectMapper mapper = new ObjectMapper();
ObjectNode msg = mapper.createObjectNode();
msg.put("action", "get");
msg.put("namespace", "variable");
msg.put("name", "PATH");
```

Most users will not need to work with `ObjectNode` directly -- the typed
channel methods provide a higher-level interface.

### Static Factory Methods

`BashClient` uses static factory methods instead of public constructors.
This pattern allows:

- Clear naming of different connection strategies
- Encapsulation of transport-specific setup
- Validation before object construction

```java
// Unix socket transport
BashClient client = BashClient.connect("/tmp/bash-server-1000/sock");

// Stdio transport (spawns bash-server as subprocess)
BashClient client = BashClient.connectStdio("bash-server", "--stdio");

// Windows Named Pipe transport
BashClient client = BashClient.connectNamedPipe("bash-server");
```

---

## Getting Started

### Minimal Example

```java
import org.gnu.bash.client.BashClient;
import org.gnu.bash.client.BashClientException;
import org.gnu.bash.client.types.EvalResult;

public class QuickStart {
    public static void main(String[] args) throws Exception {
        String socketPath = args[0];
        String token = args[1];

        try (BashClient client = BashClient.connect(socketPath)) {
            // Authenticate
            client.auth(token);

            // Execute a command
            EvalResult result = client.eval("echo 'Hello from bash-server!'");

            // Print results
            System.out.println("stdout: " + result.getStdout());
            System.out.println("stderr: " + result.getStderr());
            System.out.println("exit code: " + result.getExitCode());
        }
    }
}
```

### Compile and Run

```bash
# Compile
javac -cp target/bashclient-0.1.0.jar:target/dependency/* \
    QuickStart.java

# Run
TOKEN=$(cat ~/.config/bash-server/myapp/token)
java -cp .:target/bashclient-0.1.0.jar:target/dependency/* \
    QuickStart /tmp/bash-server-$(id -u)/sock "$TOKEN"
```

### Reading the Token File

In production code, read the token from the bash-server config directory:

```java
import java.nio.file.Files;
import java.nio.file.Path;

public class TokenReader {
    public static String readToken(String serverName) throws IOException {
        // Standard token location
        Path tokenPath = Path.of(
            System.getProperty("user.home"),
            ".config", "bash-server", serverName, "token"
        );
        return Files.readString(tokenPath).trim();
    }

    public static String readTokenFromSocket(String socketPath) throws IOException {
        // Derive config dir from socket path
        Path socketDir = Path.of(socketPath).getParent();
        Path tokenPath = socketDir.resolve("token");
        if (Files.exists(tokenPath)) {
            return Files.readString(tokenPath).trim();
        }
        // Fall back to standard config location
        String name = socketDir.getFileName().toString();
        return readToken(name);
    }
}
```

---

## Transport Modes

### Unix Socket Transport

The primary transport. Uses junixsocket for Unix domain socket communication.

```java
// Connect to a specific socket path
try (BashClient client = BashClient.connect("/tmp/bash-server-1000/sock")) {
    client.auth(token);
    // ...
}

// Connect to the default socket for a named server
// Resolves: ~/.config/bash-server/<name>/socket
//       or: $XDG_RUNTIME_DIR/bash-server/<name>/sock
//       or: /tmp/bash-server-<uid>/sock
try (BashClient client = BashClient.connect(
        "/run/user/1000/bash-server/myapp/sock")) {
    client.auth(token);
    // ...
}
```

**Requirements:**

- junixsocket library on the classpath
- Platform-specific junixsocket native library
- bash-server listening on the specified socket path

**Socket path resolution:**

The client does not perform automatic socket path resolution. You must provide
the full socket path. Helper methods for resolution can be built on top:

```java
public static String resolveSocketPath(String serverName) {
    // Check XDG_RUNTIME_DIR first
    String xdgRuntime = System.getenv("XDG_RUNTIME_DIR");
    if (xdgRuntime != null) {
        Path xdgPath = Path.of(xdgRuntime, "bash-server", serverName, "sock");
        if (Files.exists(xdgPath)) {
            return xdgPath.toString();
        }
    }

    // Fall back to /tmp
    String uid = System.getenv("UID");
    if (uid == null) {
        try {
            uid = new String(Runtime.getRuntime()
                .exec("id -u").getInputStream().readAllBytes()).trim();
        } catch (IOException e) {
            uid = "1000";
        }
    }
    return "/tmp/bash-server-" + uid + "/sock";
}
```

### Stdio Transport

Spawns bash-server as a subprocess and communicates over stdin/stdout. Useful
for environments where Unix sockets are not available or for testing.

```java
// Basic stdio connection
try (BashClient client = BashClient.connectStdio(
        "./bash-server/bash-server", "--stdio")) {
    // Auth token is typically printed to stderr or a designated fd
    // For stdio, auth may be handled differently
    client.auth(token);
    EvalResult result = client.eval("echo hello");
}

// With additional bash-server arguments
try (BashClient client = BashClient.connectStdio(
        "./bash-server/bash-server", "--stdio",
        "--login", "--init", "/path/to/init.sh")) {
    client.auth(token);
    // ...
}
```

**How it works:**

1. `connectStdio()` starts the bash-server process with `ProcessBuilder`
2. The client's output stream connects to the server's stdin
3. The client's input stream connects to the server's stdout
4. The daemon reader thread reads from the server's stdout
5. On `close()`, the subprocess is destroyed

**Advantages:**

- No socket path management
- No filesystem permissions to worry about
- Server lifecycle tied to client lifecycle
- Works on all platforms without native socket libraries

**Limitations:**

- One client per server instance
- Higher overhead than Unix sockets (process spawn)
- Cannot share the server across multiple clients

### Named Pipe Transport (Windows/Cygwin)

For Windows environments, connects via Windows Named Pipes.

```java
// Connect to a named pipe
try (BashClient client = BashClient.connectNamedPipe("bash-server")) {
    client.auth(token);
    EvalResult result = client.eval("echo hello from Windows");
}

// With a custom pipe name
try (BashClient client = BashClient.connectNamedPipe("my-custom-pipe")) {
    client.auth(token);
    // ...
}
```

**How it works:**

1. Opens `\\.\pipe\<pipeName>` using `RandomAccessFile` in "rw" mode
2. The file's input/output streams serve as the communication channel
3. The daemon reader thread reads from the pipe's input stream

**Named Pipe path construction:**

The pipe name is automatically prefixed with `\\.\pipe\`:

| Argument | Full pipe path |
|----------|---------------|
| `"bash-server"` | `\\.\pipe\bash-server` |
| `"my-app"` | `\\.\pipe\my-app` |

**Platform notes:**

- On Cygwin, Named Pipes are accessible from both Cygwin and Windows native
- On native Windows, this is the preferred transport
- junixsocket is not required for Named Pipe transport
- DACL security on the pipe restricts access to the creating user

---

## Authentication

All connections must authenticate before executing commands. Authentication
uses a 256-bit random token generated by the server.

### Basic Authentication

```java
try (BashClient client = BashClient.connect(socketPath)) {
    // Authenticate with the server's token
    client.auth(token);

    // Verify authentication status
    assert client.isAuthenticated();

    // Now you can use all channels
    client.eval("echo authenticated");
}
```

### Authentication Failure Handling

```java
try (BashClient client = BashClient.connect(socketPath)) {
    try {
        client.auth(wrongToken);
    } catch (BashClientException.AuthException e) {
        System.err.println("Authentication failed: " + e.getMessage());
        // Connection is still open but unauthenticated
        // You can retry with the correct token
        client.auth(correctToken);
    }
}
```

### Pre-auth Operations

Before authentication, only limited operations are available:

```java
try (BashClient client = BashClient.connect(socketPath)) {
    // These work before auth:
    boolean connected = client.isConnected();      // true
    boolean authed = client.isAuthenticated();     // false

    // These throw BashClientException before auth:
    // client.eval("echo hello");      // throws
    // client.state.getVar("PATH");    // throws

    client.auth(token);
    // Now everything works
}
```

---

## Command Execution

### Simple eval()

The `eval()` method on `BashClient` is a convenience wrapper that sends a
command, waits for completion, and returns the result:

```java
EvalResult result = client.eval("echo hello world");
System.out.println(result.getStdout());   // "hello world\n"
System.out.println(result.getStderr());   // ""
System.out.println(result.getExitCode()); // 0
```

### Multi-line Commands

```java
EvalResult result = client.eval(
    "for i in 1 2 3; do\n" +
    "    echo \"item $i\"\n" +
    "done"
);
// stdout: "item 1\nitem 2\nitem 3\n"
```

### Exit Codes

```java
EvalResult result = client.eval("false");
assert result.getExitCode() == 1;

result = client.eval("exit 42");
assert result.getExitCode() == 42;
```

### Capturing Errors

```java
EvalResult result = client.eval("ls /nonexistent");
if (result.getExitCode() != 0) {
    System.err.println("Command failed: " + result.getStderr());
}
```

### Variable Substitution

Commands are evaluated in the server's bash environment, so variable
expansion works normally:

```java
client.eval("MY_VAR='hello world'");
EvalResult result = client.eval("echo $MY_VAR");
// stdout: "hello world\n"
```

### Command Pipelines

```java
EvalResult result = client.eval("cat /etc/passwd | grep root | wc -l");
int rootEntries = Integer.parseInt(result.getStdout().trim());
```

---

## Control Channel

The control channel handles connection-level operations.

```java
import org.gnu.bash.client.channels.ControlChannel;

// Access via client.control
ControlChannel ctl = client.control;
```

### Ping

```java
// Using convenience method
client.ping();

// Using channel directly
client.control.ping();
```

### Configure

```java
// Set observe level
client.control.configure("observe_level", "full");

// Set wire format to NDJSON
client.control.configure("wire_format", "ndjson");
```

### Disconnect

```java
// Graceful disconnect (also done by close())
client.control.disconnect();
```

---

## Command Channel

The command channel provides fine-grained control over command execution,
including streaming output and pre-parsed command evaluation.

```java
import org.gnu.bash.client.channels.CommandChannel;
import org.gnu.bash.client.types.EvalResult;

CommandChannel cmd = client.command;
```

### Standard Evaluation

```java
// Same as client.eval() but through the channel directly
EvalResult result = cmd.eval("echo hello");
```

### Streaming Output

For long-running commands, you can receive stdout and stderr incrementally
using callbacks:

```java
import java.util.function.Consumer;

Consumer<String> onStdout = line -> System.out.print("[OUT] " + line);
Consumer<String> onStderr = line -> System.err.print("[ERR] " + line);

EvalResult result = cmd.eval("find / -name '*.conf' 2>/dev/null",
    onStdout, onStderr);
```

### Eval with Parsed Command

If you have a pre-serialized COMMAND tree (JSON), you can send it directly:

```java
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.fasterxml.jackson.databind.ObjectMapper;

ObjectMapper mapper = new ObjectMapper();
ObjectNode commandTree = mapper.readValue(
    "{\"type\":\"simple\",\"words\":[\"echo\",\"hello\"]}",
    ObjectNode.class
);

EvalResult result = cmd.evalParsed(commandTree);
```

### Timeout Control

```java
import java.time.Duration;

// Custom timeout for a long-running command
EvalResult result = cmd.eval("sleep 60 && echo done",
    Duration.ofSeconds(90));
```

---

## State Channel

The state channel reads and modifies the server's bash environment: variables,
functions, aliases, and traps.

```java
import org.gnu.bash.client.channels.StateChannel;
import org.gnu.bash.client.types.VarInfo;

StateChannel state = client.state;
```

### Variables

#### Get a Variable

```java
VarInfo info = state.getVar("PATH");
System.out.println("Value: " + info.getValue());
System.out.println("Exported: " + info.isExported());
System.out.println("Readonly: " + info.isReadonly());
System.out.println("Type: " + info.getType());  // "string", "array", "assoc"
```

#### Set a Variable

```java
// Simple string variable
state.setVar("MY_APP_HOME", "/opt/myapp");

// Exported variable
state.setVar("MY_APP_HOME", "/opt/myapp", true);  // exported=true

// Array variable
state.setVar("MY_ARRAY", new String[]{"one", "two", "three"});
```

#### Unset a Variable

```java
state.unsetVar("MY_APP_HOME");
```

#### List Variables

```java
// Get all variable names
List<String> varNames = state.listVars();

// Get variables matching a pattern
List<String> pathVars = state.listVars("*PATH*");
```

### Functions

#### Get a Function

```java
String functionBody = state.getFunc("my_function");
System.out.println(functionBody);
// Output: "my_function () { echo hello; }"
```

#### Set a Function

```java
state.setFunc("greet", "greet() { echo \"Hello, $1!\"; }");
```

#### Unset a Function

```java
state.unsetFunc("greet");
```

#### List Functions

```java
List<String> funcNames = state.listFuncs();
```

### Aliases

#### Get an Alias

```java
String aliasValue = state.getAlias("ll");
// "ls -la"
```

#### Set an Alias

```java
state.setAlias("ll", "ls -la --color=auto");
```

#### Unset an Alias

```java
state.unsetAlias("ll");
```

#### List Aliases

```java
List<String> aliasNames = state.listAliases();
```

### Traps

#### Get a Trap

```java
String trapAction = state.getTrap("INT");
// "echo interrupted"
```

#### Set a Trap

```java
state.setTrap("EXIT", "echo 'goodbye'");
state.setTrap("INT", "echo 'interrupted'; exit 1");
```

#### Unset a Trap

```java
state.unsetTrap("EXIT");
```

#### List Traps

```java
List<String> trapSignals = state.listTraps();
```

### Inspect (Bulk Query)

Retrieve multiple items at once:

```java
import com.fasterxml.jackson.databind.node.ObjectNode;

// Inspect all variables, functions, aliases, and traps
ObjectNode snapshot = state.inspect();

// Inspect specific namespaces
ObjectNode varsOnly = state.inspect("variables");
ObjectNode funcsAndAliases = state.inspect("functions", "aliases");
```

---

## Observe Channel

The observe channel subscribes to pre-command and post-command events. This
is useful for monitoring, auditing, or building interactive tools.

```java
import org.gnu.bash.client.channels.ObserveChannel;
import java.util.function.Consumer;
```

### Subscribe to Events

```java
ObserveChannel observe = client.observe;

// Subscribe to pre-command events
observe.onPreCommand(event -> {
    System.out.println("About to execute: " + event.get("command").asText());
    System.out.println("Working dir: " + event.get("cwd").asText());
});

// Subscribe to post-command events
observe.onPostCommand(event -> {
    System.out.println("Finished: " + event.get("command").asText());
    System.out.println("Exit code: " + event.get("exit_code").asInt());
    System.out.println("Duration: " + event.get("duration_ms").asLong() + "ms");
});
```

### Observe Levels

```java
// Set observation level via control channel
client.control.configure("observe_level", "basic");   // command text only
client.control.configure("observe_level", "full");     // command + timing + cwd
client.control.configure("observe_level", "off");      // disable observation
```

### Polling for Events

If you prefer polling over callbacks:

```java
import com.fasterxml.jackson.databind.node.ObjectNode;
import java.time.Duration;

// Poll for the next event with timeout
ObjectNode event = observe.poll(Duration.ofSeconds(5));
if (event != null) {
    String eventType = event.get("event").asText();
    // "pre_command" or "post_command"
}
```

### Event Structure

Pre-command event fields:

| Field | Type | Description |
|-------|------|-------------|
| `event` | string | `"pre_command"` |
| `command` | string | The command text about to execute |
| `cwd` | string | Current working directory |
| `timestamp` | number | Unix timestamp (milliseconds) |

Post-command event fields:

| Field | Type | Description |
|-------|------|-------------|
| `event` | string | `"post_command"` |
| `command` | string | The command text that executed |
| `exit_code` | integer | Exit status |
| `cwd` | string | Current working directory after execution |
| `duration_ms` | number | Execution time in milliseconds |
| `timestamp` | number | Unix timestamp (milliseconds) |

### Unsubscribe

```java
// Remove specific callbacks
observe.removePreCommandListeners();
observe.removePostCommandListeners();

// Or remove all listeners
observe.removeAllListeners();
```

### Example: Command Logger

```java
public class CommandLogger {
    private final List<String> log = new CopyOnWriteArrayList<>();

    public void attach(BashClient client) {
        client.observe.onPostCommand(event -> {
            String entry = String.format("[%d] %s -> %d (%dms)",
                event.get("timestamp").asLong(),
                event.get("command").asText(),
                event.get("exit_code").asInt(),
                event.get("duration_ms").asLong()
            );
            log.add(entry);
        });
        client.control.configure("observe_level", "full");
    }

    public List<String> getLog() {
        return Collections.unmodifiableList(log);
    }
}
```

---

## Debug Channel

The debug channel enables breakpoints, stepping, and AST inspection.

```java
import org.gnu.bash.client.channels.DebugChannel;
import org.gnu.bash.client.types.BreakHitEvent;
```

### Breakpoints

#### Add a Breakpoint

```java
DebugChannel debug = client.debug;

// Break on a specific command pattern
int bpId1 = debug.addBreakpoint("command", "rm *");

// Break on a specific line number
int bpId2 = debug.addBreakpoint("line", "script.sh:42");

// Break on function entry
int bpId3 = debug.addBreakpoint("function", "deploy");
```

#### List Breakpoints

```java
List<ObjectNode> breakpoints = debug.listBreakpoints();
for (ObjectNode bp : breakpoints) {
    System.out.printf("BP #%d: type=%s pattern=%s enabled=%b%n",
        bp.get("id").asInt(),
        bp.get("type").asText(),
        bp.get("pattern").asText(),
        bp.get("enabled").asBoolean()
    );
}
```

#### Remove a Breakpoint

```java
debug.removeBreakpoint(bpId1);
```

#### Enable/Disable Breakpoints

```java
debug.disableBreakpoint(bpId2);
debug.enableBreakpoint(bpId2);
```

### Stepping

When execution hits a breakpoint, you can step through the code:

```java
// Subscribe to breakpoint hit events
debug.onBreakHit(event -> {
    System.out.println("Hit breakpoint #" + event.getBreakpointId());
    System.out.println("Command: " + event.getCommand());
    System.out.println("Location: " + event.getFile() + ":" + event.getLine());
});

// Step to the next command
debug.step();

// Step over (skip into function internals)
debug.next();

// Step out of the current function
debug.finish();

// Continue execution (until next breakpoint)
debug.continueExecution();

// Skip the current command (don't execute it)
debug.skip();
```

### AST Inspection

Inspect the parsed abstract syntax tree of the current command:

```java
ObjectNode ast = debug.inspectAst();
System.out.println("AST type: " + ast.get("type").asText());
// e.g., "pipeline", "simple", "if", "for", "while", etc.
```

### Example: Simple Debugger

```java
public class SimpleDebugger {
    private final BashClient client;
    private final Scanner scanner = new Scanner(System.in);

    public SimpleDebugger(BashClient client) {
        this.client = client;
    }

    public void run(String script) throws BashClientException {
        // Set a line breakpoint at line 1
        client.debug.addBreakpoint("line", script + ":1");

        // Handle breakpoint hits
        client.debug.onBreakHit(event -> {
            System.out.printf("Stopped at %s:%d%n",
                event.getFile(), event.getLine());
            System.out.println("Command: " + event.getCommand());

            while (true) {
                System.out.print("(debug) ");
                String input = scanner.nextLine().trim();

                switch (input) {
                    case "s": case "step":
                        client.debug.step(); return;
                    case "n": case "next":
                        client.debug.next(); return;
                    case "f": case "finish":
                        client.debug.finish(); return;
                    case "c": case "continue":
                        client.debug.continueExecution(); return;
                    case "a": case "ast":
                        ObjectNode ast = client.debug.inspectAst();
                        System.out.println(ast.toPrettyString());
                        break;
                    case "q": case "quit":
                        client.debug.continueExecution(); return;
                    default:
                        System.out.println("Unknown command: " + input);
                }
            }
        });

        // Source the script
        client.eval("source " + script);
    }
}
```

---

## PTY Channel

The PTY channel spawns a pseudo-terminal attached to the server's bash
instance. This enables interactive sessions with full terminal emulation.

```java
import org.gnu.bash.client.channels.PtyChannel;
import org.gnu.bash.client.types.PtyInfo;
```

### Spawn a PTY

```java
PtyChannel pty = client.pty;

// Spawn with default dimensions (80x24)
PtyInfo info = pty.spawn();
System.out.println("PTY spawned with PID: " + info.getPid());

// Spawn with custom dimensions
PtyInfo info = pty.spawn(120, 40);  // cols, rows
```

### Write Input

```java
// Send a command to the PTY
pty.writeInput("ls -la\n");

// Send special keys
pty.writeInput("\u0003");  // Ctrl+C
pty.writeInput("\u0004");  // Ctrl+D (EOF)
pty.writeInput("\t");      // Tab (completion)
```

### Read Output

```java
// Subscribe to PTY output
pty.onOutput(data -> {
    System.out.print(data);  // Raw terminal output (may include ANSI escapes)
});

// Or poll for output
String output = pty.readOutput(Duration.ofSeconds(1));
```

### Resize

```java
// Resize the PTY when the terminal window changes
pty.resize(132, 50);  // cols, rows
```

### Signal

```java
// Send a signal to the PTY process
pty.signal("INT");     // SIGINT
pty.signal("TERM");    // SIGTERM
pty.signal("WINCH");   // Window change (after resize)
```

### ANSI Stripping

If you need plain text without ANSI escape sequences:

```java
// Enable ANSI stripping on spawn
PtyInfo info = pty.spawn(80, 24, true);  // stripAnsi=true

// Or configure after spawn
pty.configure("strip_ansi", true);
```

### Close the PTY

```java
// Close the PTY session
pty.close();
```

### Example: Interactive Session

```java
public class InteractiveSession {
    public static void run(BashClient client) throws Exception {
        PtyChannel pty = client.pty;

        // Spawn PTY
        PtyInfo info = pty.spawn(80, 24);
        System.out.println("PTY ready (PID " + info.getPid() + ")");

        // Read output in background
        pty.onOutput(data -> System.out.print(data));

        // Forward user input to PTY
        Scanner scanner = new Scanner(System.in);
        while (scanner.hasNextLine()) {
            String line = scanner.nextLine();
            if (line.equals("!quit")) {
                break;
            }
            pty.writeInput(line + "\n");
        }

        pty.close();
    }
}
```

---

## Error Handling

### Exception Hierarchy

```
BashClientException (checked)
 |-- AuthException          Authentication failures
 |-- ProtocolException      Wire protocol errors
 |-- TimeoutException       Response timeout (default 30s)
 |-- TransportException     Connection/IO errors
 |-- ServerException        Server-side execution errors
```

All exceptions extend `BashClientException`, which is a checked exception.
This forces callers to handle error conditions explicitly.

### Catching Specific Exceptions

```java
try (BashClient client = BashClient.connect(socketPath)) {
    client.auth(token);
    EvalResult result = client.eval("rm -rf /important");
} catch (BashClientException.AuthException e) {
    // Wrong token, expired token, etc.
    System.err.println("Auth failed: " + e.getMessage());

} catch (BashClientException.TimeoutException e) {
    // Command took longer than 30 seconds
    System.err.println("Timed out: " + e.getMessage());
    System.err.println("Timeout was: " + e.getTimeout());

} catch (BashClientException.TransportException e) {
    // Socket closed, pipe broken, etc.
    System.err.println("Connection lost: " + e.getMessage());

} catch (BashClientException.ProtocolException e) {
    // Malformed JSON, unexpected message type, etc.
    System.err.println("Protocol error: " + e.getMessage());

} catch (BashClientException.ServerException e) {
    // Server reported an error (not command exit code)
    System.err.println("Server error: " + e.getMessage());
    System.err.println("Error code: " + e.getErrorCode());

} catch (BashClientException e) {
    // Catch-all for any bashclient error
    System.err.println("Error: " + e.getMessage());

} catch (IOException e) {
    // Connection setup failure
    System.err.println("IO error: " + e.getMessage());
}
```

### Exception Details

Each exception carries additional context:

```java
try {
    client.eval("sleep 600");
} catch (BashClientException.TimeoutException e) {
    Duration timeout = e.getTimeout();           // The timeout that expired
    String channel = e.getChannel();             // "command"
    String action = e.getAction();               // "eval"
    System.err.printf("Timed out after %ds on %s.%s%n",
        timeout.getSeconds(), channel, action);
}
```

### Retry Pattern

```java
public EvalResult evalWithRetry(BashClient client, String command,
                                 int maxRetries) throws BashClientException {
    BashClientException lastException = null;

    for (int attempt = 1; attempt <= maxRetries; attempt++) {
        try {
            return client.eval(command);
        } catch (BashClientException.TimeoutException e) {
            lastException = e;
            System.err.printf("Attempt %d/%d timed out%n", attempt, maxRetries);
        } catch (BashClientException.TransportException e) {
            // Connection lost -- cannot retry on same client
            throw e;
        }
    }
    throw lastException;
}
```

### Error Handling in Callbacks

Exceptions in observer/debug callbacks are caught by the daemon reader thread
and logged to stderr. They do not propagate to the calling code:

```java
client.observe.onPostCommand(event -> {
    // If this throws, the exception is logged but does not crash
    // the reader thread or affect other callbacks
    String cmd = event.get("command").asText();
    processEvent(cmd);  // May throw -- that's OK
});
```

If you need to handle callback errors, use your own try-catch:

```java
client.observe.onPostCommand(event -> {
    try {
        riskyProcessing(event);
    } catch (Exception e) {
        errorQueue.add(e);  // Forward to main thread for handling
    }
});
```

---

## Lifecycle Management

### Connection Lifecycle

```
    connect()          auth()           eval()/channels         close()
  +----------+     +-----------+     +----------------+     +---------+
  | CREATED  | --> | CONNECTED | --> | AUTHENTICATED  | --> | CLOSED  |
  +----------+     +-----------+     +----------------+     +---------+
       |                |                    |                    ^
       |                |                    |                    |
       |  (IOException) |  (AuthException)   |  (TransportExc.)  |
       +----------------+--------------------+--------------------+
                                CLOSED (error)
```

### State Checks

```java
BashClient client = BashClient.connect(socketPath);

client.isConnected();      // true (transport is open)
client.isAuthenticated();  // false (not yet authenticated)

client.auth(token);
client.isAuthenticated();  // true

client.close();
client.isConnected();      // false
client.isAuthenticated();  // false
```

### Multiple Sessions

Each `BashClient` instance is an independent session. You can have multiple
clients connected to the same server:

```java
try (BashClient reader = BashClient.connect(socketPath);
     BashClient writer = BashClient.connect(socketPath)) {

    reader.auth(token);
    writer.auth(token);

    // Each session has its own bash environment
    writer.eval("export MY_VAR=hello");

    // reader's environment is separate
    EvalResult result = reader.eval("echo $MY_VAR");
    // stdout: "\n" (empty -- MY_VAR not set in this session)
}
```

### Shutdown Hooks

For applications that need guaranteed cleanup:

```java
BashClient client = BashClient.connect(socketPath);
client.auth(token);

Runtime.getRuntime().addShutdownHook(new Thread(() -> {
    try {
        client.close();
    } catch (IOException e) {
        // Ignore during shutdown
    }
}));
```

Note: Since the daemon reader thread is a daemon thread, it will not prevent
JVM shutdown. However, explicit cleanup is still recommended to notify the
server and release resources.

---

## Thread Safety

### Thread Safety Guarantees

| Operation | Thread-safe? | Notes |
|-----------|-------------|-------|
| `connect()` | N/A | Static factory, returns new instance |
| `auth()` | No | Call once from a single thread |
| `eval()` | Yes | Serialized internally |
| `ping()` | Yes | Via control channel |
| `isConnected()` | Yes | Volatile read |
| `isAuthenticated()` | Yes | Volatile read |
| `close()` | Yes | Idempotent, can be called from any thread |
| Channel methods | Yes | Each channel has its own queue |

### Using from Multiple Threads

```java
try (BashClient client = BashClient.connect(socketPath)) {
    client.auth(token);

    // Safe: different channels from different threads
    ExecutorService pool = Executors.newFixedThreadPool(3);

    pool.submit(() -> {
        EvalResult r = client.eval("echo from thread 1");
        return r.getStdout();
    });

    pool.submit(() -> {
        VarInfo v = client.state.getVar("PATH");
        return v.getValue();
    });

    pool.submit(() -> {
        client.observe.onPostCommand(e ->
            System.out.println("event: " + e));
        return null;
    });

    pool.shutdown();
    pool.awaitTermination(30, TimeUnit.SECONDS);
}
```

### Caution: Same Channel from Multiple Threads

While technically safe (the queue is thread-safe), concurrent calls on the
same channel may interleave responses unpredictably. If you need to make
multiple calls on the same channel from different threads, synchronize
externally:

```java
private final Object evalLock = new Object();

public EvalResult safeEval(BashClient client, String cmd)
        throws BashClientException {
    synchronized (evalLock) {
        return client.eval(cmd);
    }
}
```

---

## Observer Pattern

The library uses Java's `Consumer<T>` functional interface for callbacks,
following the observer pattern.

### Registering Callbacks

```java
import java.util.function.Consumer;
import com.fasterxml.jackson.databind.node.ObjectNode;

// Observe channel callbacks
Consumer<ObjectNode> preCommandHandler = event -> {
    System.out.println("Pre: " + event.get("command").asText());
};

Consumer<ObjectNode> postCommandHandler = event -> {
    System.out.println("Post: " + event.get("command").asText()
        + " -> " + event.get("exit_code").asInt());
};

client.observe.onPreCommand(preCommandHandler);
client.observe.onPostCommand(postCommandHandler);

// Debug channel callbacks
Consumer<BreakHitEvent> breakHandler = event -> {
    System.out.printf("Break at %s:%d%n", event.getFile(), event.getLine());
};

client.debug.onBreakHit(breakHandler);

// PTY channel callbacks
Consumer<String> outputHandler = data -> System.out.print(data);
client.pty.onOutput(outputHandler);
```

### BiConsumer for Error Context

Some callbacks use `BiConsumer` for additional context:

```java
import java.util.function.BiConsumer;

// Error-aware callback
BiConsumer<ObjectNode, Throwable> safeHandler = (event, error) -> {
    if (error != null) {
        System.err.println("Callback error: " + error.getMessage());
    } else {
        System.out.println("Event: " + event);
    }
};
```

### Method References

Java method references work naturally as callbacks:

```java
public class EventProcessor {
    public void handlePreCommand(ObjectNode event) {
        // Process pre-command event
    }

    public void handlePostCommand(ObjectNode event) {
        // Process post-command event
    }
}

EventProcessor processor = new EventProcessor();
client.observe.onPreCommand(processor::handlePreCommand);
client.observe.onPostCommand(processor::handlePostCommand);
```

### Lambda Best Practices

```java
// Good: concise lambda
client.observe.onPostCommand(e ->
    logger.info("Executed: {} ({})",
        e.get("command").asText(),
        e.get("exit_code").asInt()));

// Good: method reference for complex logic
client.observe.onPostCommand(this::processEvent);

// Avoid: overly complex inline lambda
// Extract to a named method instead
```

---

## Best Practices

### 1. Always Use try-with-resources

```java
// GOOD
try (BashClient client = BashClient.connect(socketPath)) {
    client.auth(token);
    client.eval("echo hello");
}

// BAD - resource leak if exception occurs
BashClient client = BashClient.connect(socketPath);
client.auth(token);
client.eval("echo hello");
client.close();
```

### 2. Handle All Exception Types

```java
// GOOD - specific exception handling
try {
    client.eval(command);
} catch (BashClientException.TimeoutException e) {
    // Handle timeout specifically
} catch (BashClientException.TransportException e) {
    // Connection lost, need to reconnect
} catch (BashClientException e) {
    // Other errors
}

// BAD - swallowing exceptions
try {
    client.eval(command);
} catch (Exception e) {
    // Ignoring
}
```

### 3. Validate Input Before Sending

```java
// GOOD
public EvalResult safeEval(BashClient client, String command)
        throws BashClientException {
    if (command == null || command.isBlank()) {
        throw new IllegalArgumentException("Command must not be blank");
    }
    return client.eval(command);
}

// BAD
client.eval(null);  // Will cause confusing server-side error
```

### 4. Read Token from File, Not Hard-code

```java
// GOOD
String token = Files.readString(
    Path.of(System.getProperty("user.home"),
        ".config", "bash-server", "myapp", "token")).trim();

// BAD
String token = "a1b2c3d4e5f6...";  // Never hard-code tokens
```

### 5. Use Appropriate Timeouts

```java
// GOOD - custom timeout for long operations
EvalResult result = client.command.eval(
    "find / -name '*.log'",
    Duration.ofMinutes(5));

// BAD - relying on default 30s for known long operations
EvalResult result = client.eval("find / -name '*.log'");
// Will likely throw TimeoutException
```

### 6. Clean Up Observers

```java
// GOOD
try (BashClient client = BashClient.connect(socketPath)) {
    client.auth(token);
    client.observe.onPostCommand(handler);

    // Do work...

    client.observe.removeAllListeners();  // Clean up
}

// The close() also cleans up, but explicit removal is clearer
```

### 7. Check Connection State

```java
// GOOD
if (client.isConnected() && client.isAuthenticated()) {
    client.eval(command);
}

// GOOD - handle disconnection gracefully
try {
    client.eval(command);
} catch (BashClientException.TransportException e) {
    // Reconnect logic
    client = BashClient.connect(socketPath);
    client.auth(token);
    client.eval(command);
}
```

### 8. Avoid Shell Injection

```java
// GOOD - quote user input
String filename = userInput.replace("'", "'\\''");
client.eval("cat '" + filename + "'");

// BETTER - use printf for safe variable setting
client.eval("printf -v safe_name '%q' '" + userInput + "'");
client.eval("cat \"$safe_name\"");

// BAD - direct interpolation
client.eval("cat " + userInput);
// If userInput is "; rm -rf /" this is catastrophic
```

### 9. Prefer State Channel Over Eval for Variables

```java
// GOOD - typed, structured access
VarInfo info = client.state.getVar("PATH");
String path = info.getValue();

// LESS GOOD - parsing output
EvalResult result = client.eval("echo $PATH");
String path = result.getStdout().trim();
```

### 10. Log Operations for Debugging

```java
// GOOD
private static final Logger logger = LoggerFactory.getLogger(MyClass.class);

try {
    logger.debug("Evaluating: {}", command);
    EvalResult result = client.eval(command);
    logger.debug("Result: exit={}, stdout={}",
        result.getExitCode(), result.getStdout());
} catch (BashClientException e) {
    logger.error("Eval failed for '{}': {}", command, e.getMessage(), e);
    throw e;
}
```

---

## Recipes

### Recipe 1: Configuration File Manager

Read and write configuration files through bash-server:

```java
public class ConfigManager {
    private final BashClient client;

    public ConfigManager(BashClient client) {
        this.client = client;
    }

    public String readConfig(String path) throws BashClientException {
        EvalResult result = client.eval("cat '" +
            path.replace("'", "'\\''") + "'");
        if (result.getExitCode() != 0) {
            throw new BashClientException.ServerException(
                "Failed to read " + path + ": " + result.getStderr());
        }
        return result.getStdout();
    }

    public void writeConfig(String path, String content)
            throws BashClientException {
        // Use heredoc to avoid quoting issues
        EvalResult result = client.eval(
            "cat > '" + path.replace("'", "'\\''") + "' <<'HEREDOC_EOF'\n" +
            content + "\nHEREDOC_EOF");
        if (result.getExitCode() != 0) {
            throw new BashClientException.ServerException(
                "Failed to write " + path + ": " + result.getStderr());
        }
    }

    public Map<String, String> readProperties(String path)
            throws BashClientException {
        String content = readConfig(path);
        Map<String, String> props = new LinkedHashMap<>();
        for (String line : content.split("\n")) {
            line = line.trim();
            if (line.isEmpty() || line.startsWith("#")) continue;
            int eq = line.indexOf('=');
            if (eq > 0) {
                props.put(line.substring(0, eq).trim(),
                          line.substring(eq + 1).trim());
            }
        }
        return props;
    }
}
```

### Recipe 2: Process Monitor

Monitor processes running on the server:

```java
public class ProcessMonitor {
    private final BashClient client;
    private final ScheduledExecutorService scheduler;

    public ProcessMonitor(BashClient client) {
        this.client = client;
        this.scheduler = Executors.newScheduledThreadPool(1);
    }

    public void startMonitoring(Consumer<List<String>> callback,
                                 Duration interval) {
        scheduler.scheduleAtFixedRate(() -> {
            try {
                EvalResult result = client.eval("ps aux --sort=-%cpu | head -20");
                List<String> lines = List.of(result.getStdout().split("\n"));
                callback.accept(lines);
            } catch (BashClientException e) {
                System.err.println("Monitor error: " + e.getMessage());
            }
        }, 0, interval.toMillis(), TimeUnit.MILLISECONDS);
    }

    public void stop() {
        scheduler.shutdown();
    }
}
```

### Recipe 3: Batch Command Executor

Execute multiple commands with error collection:

```java
public class BatchExecutor {
    private final BashClient client;

    public BatchExecutor(BashClient client) {
        this.client = client;
    }

    public record BatchResult(
        List<EvalResult> results,
        List<String> errors,
        int successCount,
        int failureCount
    ) {}

    public BatchResult execute(List<String> commands) {
        List<EvalResult> results = new ArrayList<>();
        List<String> errors = new ArrayList<>();
        int success = 0, failure = 0;

        for (String cmd : commands) {
            try {
                EvalResult result = client.eval(cmd);
                results.add(result);
                if (result.getExitCode() == 0) {
                    success++;
                } else {
                    failure++;
                    errors.add(cmd + ": exit " + result.getExitCode()
                        + " - " + result.getStderr().trim());
                }
            } catch (BashClientException e) {
                failure++;
                errors.add(cmd + ": " + e.getMessage());
                results.add(null);
            }
        }

        return new BatchResult(results, errors, success, failure);
    }
}
```

### Recipe 4: Environment Snapshot and Restore

Capture and restore the bash environment:

```java
public class EnvironmentSnapshot {
    private final Map<String, String> variables;
    private final Map<String, String> functions;
    private final Map<String, String> aliases;

    private EnvironmentSnapshot(Map<String, String> variables,
                                 Map<String, String> functions,
                                 Map<String, String> aliases) {
        this.variables = variables;
        this.functions = functions;
        this.aliases = aliases;
    }

    public static EnvironmentSnapshot capture(BashClient client)
            throws BashClientException {
        StateChannel state = client.state;

        Map<String, String> vars = new LinkedHashMap<>();
        for (String name : state.listVars()) {
            VarInfo info = state.getVar(name);
            vars.put(name, info.getValue());
        }

        Map<String, String> funcs = new LinkedHashMap<>();
        for (String name : state.listFuncs()) {
            funcs.put(name, state.getFunc(name));
        }

        Map<String, String> aliases = new LinkedHashMap<>();
        for (String name : state.listAliases()) {
            aliases.put(name, state.getAlias(name));
        }

        return new EnvironmentSnapshot(vars, funcs, aliases);
    }

    public void restore(BashClient client) throws BashClientException {
        StateChannel state = client.state;

        for (var entry : variables.entrySet()) {
            state.setVar(entry.getKey(), entry.getValue());
        }
        for (var entry : functions.entrySet()) {
            state.setFunc(entry.getKey(), entry.getValue());
        }
        for (var entry : aliases.entrySet()) {
            state.setAlias(entry.getKey(), entry.getValue());
        }
    }
}
```

### Recipe 5: Script Runner with Progress Reporting

Run a script with line-by-line progress:

```java
public class ScriptRunner {
    private final BashClient client;

    public ScriptRunner(BashClient client) {
        this.client = client;
    }

    public interface ProgressCallback {
        void onProgress(int lineNumber, String command, int exitCode);
        void onComplete(int totalLines, int failures);
    }

    public void runScript(String scriptContent, ProgressCallback callback)
            throws BashClientException {
        String[] lines = scriptContent.split("\n");
        int failures = 0;
        int lineNum = 0;

        for (String line : lines) {
            lineNum++;
            line = line.trim();

            // Skip comments and blank lines
            if (line.isEmpty() || line.startsWith("#")) continue;

            EvalResult result = client.eval(line);
            callback.onProgress(lineNum, line, result.getExitCode());

            if (result.getExitCode() != 0) {
                failures++;
            }
        }

        callback.onComplete(lineNum, failures);
    }
}
```

### Recipe 6: Reconnecting Client Wrapper

Automatically reconnect on transport failures:

```java
public class ReconnectingBashClient implements AutoCloseable {
    private final String socketPath;
    private final String token;
    private BashClient client;
    private final int maxRetries;

    public ReconnectingBashClient(String socketPath, String token,
                                   int maxRetries) throws IOException,
                                   BashClientException {
        this.socketPath = socketPath;
        this.token = token;
        this.maxRetries = maxRetries;
        connect();
    }

    private void connect() throws IOException, BashClientException {
        this.client = BashClient.connect(socketPath);
        this.client.auth(token);
    }

    public EvalResult eval(String command) throws BashClientException {
        for (int attempt = 0; attempt < maxRetries; attempt++) {
            try {
                return client.eval(command);
            } catch (BashClientException.TransportException e) {
                if (attempt == maxRetries - 1) throw e;
                try {
                    client.close();
                } catch (IOException ignored) {}
                try {
                    connect();
                } catch (IOException | BashClientException reconnectError) {
                    if (attempt == maxRetries - 1) {
                        throw new BashClientException.TransportException(
                            "Reconnect failed after " + maxRetries +
                            " attempts", reconnectError);
                    }
                }
            }
        }
        throw new BashClientException.TransportException("Unreachable");
    }

    @Override
    public void close() throws IOException {
        if (client != null) {
            client.close();
        }
    }
}
```

### Recipe 7: Command Audit Trail

Complete audit logging of all commands:

```java
public class AuditTrail {
    public record AuditEntry(
        Instant timestamp,
        String command,
        int exitCode,
        long durationMs,
        String stdout,
        String stderr
    ) {}

    private final BashClient client;
    private final List<AuditEntry> entries = new CopyOnWriteArrayList<>();
    private final Path logFile;

    public AuditTrail(BashClient client, Path logFile)
            throws BashClientException {
        this.client = client;
        this.logFile = logFile;

        // Subscribe to post-command events
        client.control.configure("observe_level", "full");
        client.observe.onPostCommand(this::recordEvent);
    }

    private void recordEvent(ObjectNode event) {
        AuditEntry entry = new AuditEntry(
            Instant.ofEpochMilli(event.get("timestamp").asLong()),
            event.get("command").asText(),
            event.get("exit_code").asInt(),
            event.get("duration_ms").asLong(),
            "",  // stdout not available in observe events
            ""
        );
        entries.add(entry);

        // Append to log file
        try {
            String logLine = String.format("[%s] %s (exit=%d, %dms)%n",
                entry.timestamp(), entry.command(),
                entry.exitCode(), entry.durationMs());
            Files.writeString(logFile, logLine,
                StandardOpenOption.CREATE, StandardOpenOption.APPEND);
        } catch (IOException e) {
            System.err.println("Failed to write audit log: " + e.getMessage());
        }
    }

    public List<AuditEntry> getEntries() {
        return Collections.unmodifiableList(entries);
    }
}
```

---

## Next Steps

- [API Reference](API.md) -- complete method documentation
- [Architecture](ARCHITECTURE.md) -- internal design details
- [Examples](examples/README.md) -- runnable code samples
- [Troubleshooting](TROUBLESHOOTING.md) -- common issues and solutions
- [Contributing](CONTRIBUTING.md) -- how to contribute
