# BASH-SERVER-CLIENT-JAVA(7) -- Java client binding for bash-server

# DESCRIPTION

The Java client binding provides a blocking, thread-safe interface to
the bash-server v2 NDJSON protocol.  It is implemented as the
`org.gnu.bash:bashclient` Maven artifact, requires Java 11 or later,
and depends on junixsocket for Unix domain socket support and Jackson
for JSON processing.

The binding uses a daemon reader thread that continuously reads from
the transport and dispatches messages to per-channel `BlockingQueue`
instances.  Channel methods block the calling thread (with a 30-second
default timeout) until the response arrives.

# CONCEPTS

# Installation

The library is located at `bash-server/clients/java/` and uses Maven:

```sh
cd bash-server/clients/java
mvn package
```

This produces `target/bashclient-0.1.0.jar`.

To run tests:

```sh
mvn test
```

# Maven coordinates

```xml
<dependency>
    <groupId>org.gnu.bash</groupId>
    <artifactId>bashclient</artifactId>
    <version>0.1.0</version>
</dependency>
```

# Dependencies

| Dependency          | Version | Purpose                          |
|---------------------|---------|----------------------------------|
| junixsocket-core    | 2.9.0   | Unix domain socket (AF_UNIX)     |
| jackson-databind    | 2.16.0  | JSON serialization/deserialization|
| junit-jupiter       | 5.10.0  | Unit testing (test scope only)   |

The Java binding is the only one that requires external runtime
dependencies.  This is because Java's standard library did not include
Unix domain socket support until Java 16 (via `UnixDomainSocketAddress`),
and the binding targets Java 11.  junixsocket provides portable
AF_UNIX support back to Java 8.

# Package structure

```
org.gnu.bash.client
    BashClient                -- Main client class (AutoCloseable)
    BashClientException       -- Exception hierarchy
    Transport                 -- Transport interface
    UnixSocketTransport       -- AF_UNIX via junixsocket
    StdioTransport            -- Subprocess stdin/stdout
    FdTransport               -- Inherited file descriptor
    NamedPipeTransport        -- Windows Named Pipe
    NdjsonProtocol            -- NDJSON encode/decode

org.gnu.bash.client.channels
    ControlChannel            -- Channel 0
    CommandChannel            -- Channel 1
    StateChannel              -- Channel 2
    ObserveChannel            -- Channel 3
    DebugChannel              -- Channel 4
    PtyChannel                -- Channel 5

org.gnu.bash.client.types
    EvalResult                -- Command result POJO
    VarInfo                   -- Variable info POJO
    BreakHitEvent             -- Debugger break event
    PtyInfo                   -- PTY session info
```

# Architecture

The central class is `BashClient`, which implements `AutoCloseable`.
It owns a `Transport` instance and starts a daemon reader thread at
construction time.

The reader thread continuously reads NDJSON lines from the transport
and routes them:

- **Request/response messages** are placed on per-channel
  `LinkedBlockingQueue<ObjectNode>` instances.  When a channel method
  sends a request, it calls `queue.poll(30, TimeUnit.SECONDS)` to
  block until the response arrives or a timeout occurs.

- **Server-push messages** (observe events, debug break_hit, PTY
  output/exit) are dispatched directly to registered callbacks via the
  channel's `dispatch()` method.

Each channel is exposed as a public final field:

```java
client.control   // ControlChannel  (channel 0)
client.command   // CommandChannel  (channel 1)
client.state     // StateChannel    (channel 2)
client.observe   // ObserveChannel  (channel 3)
client.debug     // DebugChannel    (channel 4)
client.pty       // PtyChannel      (channel 5)
```

The reader thread is marked as a daemon thread, so it does not prevent
JVM shutdown.

# Connection

`BashClient` provides static factory methods:

```java
// Unix domain socket (default)
BashClient client = BashClient.connect("/tmp/bash-server-1000/sock");

// Subprocess (--stdio mode)
BashClient client = BashClient.connectStdio("bash-server", "--stdio");

// Windows Named Pipe (Cygwin)
BashClient client = BashClient.connectNamedPipe("\\\\.\\pipe\\bash-server-myname");
```

All factory methods throw `IOException` on connection failure.  They
instantiate the appropriate `Transport` implementation, call its
`connect()` method, and construct the `BashClient` (which starts the
reader thread).

A `BashClient` can also be constructed directly from a `Transport`:

```java
Transport t = new UnixSocketTransport();
t.connect(socketPath);
BashClient client = new BashClient(t);
```

# Lifecycle -- try-with-resources

`BashClient` implements `AutoCloseable`, making try-with-resources the
recommended usage pattern:

```java
try (BashClient client = BashClient.connect(socketPath)) {
    client.auth(token);
    EvalResult result = client.eval("echo hello");
    System.out.println(result.stdout);
}
// close() called automatically
```

The `close()` method:

1. Sets the `running` flag to `false`, stopping the reader loop.
2. Sends a `disconnect` message (ignoring errors).
3. Closes the transport.
4. Interrupts the reader thread.

# Authentication

```java
client.auth(token);
```

Throws `BashClientException.AuthException` on failure.  Check status:

```java
if (client.isAuthenticated()) {
    // safe to use all channels
}
```

# Type system

Protocol objects are plain Java objects (POJOs) in
`org.gnu.bash.client.types`:

| Class              | Fields                                          |
|--------------------|-------------------------------------------------|
| `EvalResult`       | `stdout`, `stderr`, `exitCode`                  |
| `VarInfo`          | `name`, `value`, `attributes`                   |
| `BreakHitEvent`    | `line`, `command`, `depth`                      |
| `PtyInfo`          | `rows`, `cols`, `pid`, `stripAnsi`              |

Fields use standard Java naming conventions (`camelCase`).  Each POJO
has public fields and a constructor accepting all fields.

# Error handling

All errors are checked exceptions extending `BashClientException`:

```
BashClientException
    .channel           -- Channel that produced the error (-1 if N/A)
    AuthException      -- Authentication failed
    ProtocolException  -- Malformed frame or message
    TimeoutException   -- Operation timed out (30s default)
    TransportException -- Socket or I/O error
    ServerException    -- Server returned error response
        .channel       -- Channel that returned the error
```

All exception subclasses are static inner classes of
`BashClientException`.  This keeps the exception hierarchy contained
within a single file.

Standard try/catch patterns:

```java
try {
    client.auth(wrongToken);
} catch (BashClientException.AuthException e) {
    System.err.println("bad token: " + e.getMessage());
}

try {
    EvalResult result = client.eval("sleep 60");
} catch (BashClientException.TimeoutException e) {
    System.err.println("timed out after 30s");
}

try {
    client.state.getVar("NONEXISTENT");
} catch (BashClientException.ServerException e) {
    System.err.println("server error on channel " + e.getChannel());
}
```

Note that `TransportException` may wrap an underlying `IOException`
as its cause, accessible via `getCause()`.

# CONTROL channel (channel 0)

```java
// Authenticate
client.auth(token);

// Ping
client.ping();

// Configure
client.control.configure("observe_level", 1);

// Disconnect
client.control.disconnect();
```

# COMMAND channel (channel 1)

```java
// Simple eval (convenience on client)
EvalResult result = client.eval("echo hello");
System.out.println(result.stdout);    // "hello\n"
System.out.println(result.stderr);    // ""
System.out.println(result.exitCode);  // 0

// Via channel object
EvalResult result2 = client.command.eval("ls -la");
```

The `eval()` method blocks until all three response messages
(`stdout`, `stderr`, `complete`) have been received.  stdout and
stderr payloads are automatically base64-decoded.

# STATE channel (channel 2)

```java
// Variables
VarInfo info = client.state.getVar("HOME");
System.out.println(info.value);        // "/home/user"
System.out.println(info.attributes);   // []

client.state.setVar("MY_VAR", "hello", List.of("-x"));
client.state.setVar("SIMPLE", "value");  // no attributes
client.state.unsetVar("MY_VAR");

// Functions
String definition = client.state.getFunc("my_function");
System.out.println(definition);

client.state.unsetFunc("my_function");

// Aliases
String aliasValue = client.state.getAlias("ll");
client.state.setAlias("ll", "ls -la --color");
client.state.unsetAlias("ll");

// Traps
client.state.setTrap("SIGINT", "echo interrupted");
client.state.unsetTrap("SIGINT");

// Inspect
List<Map<String, Object>> items = client.state.inspect("variables");
for (Map<String, Object> item : items) {
    System.out.println(item.get("name") + "=" + item.get("value"));
}
```

# OBSERVE channel (channel 3)

```java
// Subscribe
client.observe.subscribe(1);

// Register callbacks (Consumer functional interfaces)
client.observe.onPreCommand(event -> {
    System.out.printf("[%d] running: %s in %s%n",
        event.seq, event.command, event.cwd);
});

client.observe.onPostCommand(event -> {
    System.out.printf("[%d] done: %s exit=%d duration=%dms%n",
        event.seq, event.command,
        event.exitStatus, event.durationMs);
});

// Execute commands -- callbacks fire on reader thread
client.eval("echo hello");
client.eval("ls /nonexistent");

// Unsubscribe
client.observe.unsubscribe();
```

Observe callbacks are `Consumer<PreCommandEvent>` and
`Consumer<PostCommandEvent>` functional interfaces.  They are invoked
on the reader thread, so keep them fast and non-blocking.  If heavy
processing is needed, submit work to a separate executor.

# DEBUG channel (channel 4)

```java
// Enable
client.debug.enable();

// Status
DebugStatus status = client.debug.status();
System.out.printf("active=%b mode=%s%n", status.active, status.mode);

// Add breakpoints
int bpId = client.debug.addBreakpoint("command", "echo*", -1, null);
int bpId2 = client.debug.addBreakpoint("line", null, 10, null);

// List breakpoints
List<Breakpoint> bps = client.debug.listBreakpoints();
for (Breakpoint bp : bps) {
    System.out.printf("#%d %s hits=%d%n", bp.id, bp.kind, bp.hitCount);
}

// Break hit callback
client.debug.onBreakHit(event -> {
    System.out.printf("break at line %d: %s%n", event.line, event.command);
});

// Execution control
client.debug.doContinue();
client.debug.step();
client.debug.next();
client.debug.finish();
client.debug.skip();

// Inspect AST
String ast = client.debug.inspectAst();
System.out.println(ast);

// Remove breakpoint
client.debug.removeBreakpoint(bpId);

// Disable
client.debug.disable();
```

Note that `doContinue()` is used instead of `continue()` because
`continue` is a reserved keyword in Java.

# PTY channel (channel 5)

```java
// Spawn
PtyInfo info = client.pty.spawn(24, 80, "/bin/bash", false);
System.out.printf("pid=%d %dx%d%n", info.pid, info.rows, info.cols);

// Output callback
client.pty.onOutput((data, length) -> {
    System.out.print(new String(data, 0, length));
});

client.pty.onExit(exitCode -> {
    System.out.printf("%nPTY exited: %d%n", exitCode);
});

// Write input
client.pty.writeInput("ls -la\n");

// Resize
client.pty.resize(48, 120);

// Signal
client.pty.signal("SIGINT");

// Close
client.pty.close();
```

# Callback model

The Java binding uses `Consumer` and `BiConsumer` functional interfaces
for callbacks:

```java
// Observe: Consumer<PreCommandEvent>, Consumer<PostCommandEvent>
client.observe.onPreCommand(event -> { ... });
client.observe.onPostCommand(event -> { ... });

// Debug: Consumer<BreakHitEvent>
client.debug.onBreakHit(event -> { ... });

// PTY: BiConsumer<byte[], Integer> for output, Consumer<Integer> for exit
client.pty.onOutput((data, length) -> { ... });
client.pty.onExit(exitCode -> { ... });
```

Callbacks are invoked on the reader thread.  Only one callback per
event type is active at a time.  Setting a new callback replaces the
previous one.  Pass `null` to unregister.

# Threading model

The `BashClient` uses a single daemon reader thread named
`"bashclient-reader"`.  This thread:

- Reads NDJSON lines from the transport.
- Routes request/response messages to per-channel `BlockingQueue`.
- Dispatches server-push messages to callbacks.

Channel methods block the calling thread with `queue.poll(30, SECONDS)`.
This means:

- Multiple threads can call different channel methods concurrently
  (each waits on its own channel queue).
- Two threads calling the same channel concurrently may receive each
  other's responses -- avoid this pattern.
- Callbacks run on the reader thread, so blocking in a callback will
  block all message processing.

# Timeout behavior

All blocking channel operations use a 30-second timeout by default.
If no response is received within this period,
`BashClientException.TimeoutException` is thrown.

The timeout is implemented via `BlockingQueue.poll(30, SECONDS)` in
the receive path.  There is currently no per-call timeout override;
the 30-second default is hardcoded.

# Properties

```java
client.isConnected()      // true if transport is open
client.isAuthenticated()  // true if auth() succeeded
```

# Complete example

```java
import org.gnu.bash.client.BashClient;
import org.gnu.bash.client.BashClientException;
import org.gnu.bash.client.types.EvalResult;
import org.gnu.bash.client.types.VarInfo;

public class Example {
    public static void main(String[] args) throws Exception {
        try (BashClient client = BashClient.connect(
                "/tmp/bash-server-1000/sock")) {

            // Authenticate
            client.auth("abcdef0123456789...");

            // Simple command
            EvalResult result = client.eval("echo hello world");
            System.out.println(result.stdout);

            // Set and read a variable
            client.state.setVar("GREETING", "hello");
            VarInfo info = client.state.getVar("GREETING");
            System.out.println(info.value);  // "hello"

            // Observe
            client.observe.onPostCommand(event -> {
                System.out.printf("done: %s exit=%d%n",
                    event.command, event.exitStatus);
            });
            client.observe.subscribe(1);
            client.eval("true");

            // Brief pause for push events
            Thread.sleep(100);
        }
    }
}
```

# Comparison with other bindings

The Java binding is most similar to the C binding in its blocking
call style, but differs in several ways:

| Aspect           | Java                      | C                       |
|------------------|---------------------------|-------------------------|
| Threading        | daemon reader thread      | no threads, manual poll |
| Memory           | garbage collected         | manual bc_free()        |
| Errors           | checked exceptions        | integer return codes    |
| Push events      | automatic dispatch        | manual bc_poll()        |
| Dependencies     | junixsocket, Jackson      | none (POSIX only)       |
| Cleanup          | try-with-resources        | manual bc_close()       |

The Java binding is most similar to the TypeScript binding in
architecture (background reader, blocking queues), but uses threads
instead of an event loop.

# SEE ALSO

**bash-server-client-api**(7),
**bash-server-client-channels**(7),
**bash-server-client-transports**(7),
**bash-server**(1),
**bash-server-channels**(7)

# AUTHORS

GNU Bash is Copyright (C) Free Software Foundation, Inc.
The bash-server extension was developed as part of the Cygwin Bash project.

# COPYRIGHT

This is free software; see the GNU General Public License v3 or later
for copying conditions.  There is NO warranty.
