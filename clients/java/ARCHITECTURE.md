# bashclient Java Architecture

## Internal Design Documentation

**Package**: `org.gnu.bash.client`
**Java**: 11+
**License**: GNU General Public License v3.0 or later

---

## Table of Contents

1. [Package Structure](#package-structure)
2. [Class Hierarchy](#class-hierarchy)
3. [Data Flow](#data-flow)
4. [Daemon Reader Thread](#daemon-reader-thread)
5. [BlockingQueue Routing](#blockingqueue-routing)
6. [Transport Layer](#transport-layer)
7. [Protocol Layer](#protocol-layer)
8. [Channel Architecture](#channel-architecture)
9. [Jackson ObjectNode Usage](#jackson-objectnode-usage)
10. [Exception Design](#exception-design)
11. [Testing Architecture](#testing-architecture)
12. [Thread Model](#thread-model)
13. [Shutdown Sequence](#shutdown-sequence)

---

## Package Structure

```
src/main/java/org/gnu/bash/client/
  BashClient.java                    # Main entry point, static factories, convenience methods
  BashClientException.java           # Exception hierarchy (base + 5 inner classes)
  NdjsonProtocol.java                # NDJSON wire protocol (read/write JSON frames)
  Transport.java                     # Transport interface (abstract I/O layer)
  channels/
    ControlChannel.java              # Channel 0: auth, ping, disconnect, configure
    CommandChannel.java              # Channel 1: eval, eval_parsed, streaming output
    StateChannel.java                # Channel 2: get/set/unset vars, funcs, aliases, traps
    ObserveChannel.java              # Channel 3: pre/post command event subscription
    DebugChannel.java                # Channel 4: breakpoints, stepping, AST inspection
    PtyChannel.java                  # Channel 5: PTY spawn, I/O, resize, signal
  transport/
    UnixSocketTransport.java         # junixsocket Unix domain socket transport
    StdioTransport.java              # Subprocess stdin/stdout transport
    NamedPipeTransport.java          # Windows Named Pipe transport
  types/
    EvalResult.java                  # Command execution result (stdout, stderr, exit code)
    VarInfo.java                     # Variable metadata (value, type, exported, readonly)
    BreakHitEvent.java               # Breakpoint hit event (bp id, command, file, line)
    PtyInfo.java                     # PTY session info (pid, cols, rows)

src/test/java/org/gnu/bash/client/
  BashClientTest.java                # Integration tests for BashClient
  NdjsonProtocolTest.java            # Unit tests for protocol parsing
  channels/
    ControlChannelTest.java          # Unit tests for control channel
    CommandChannelTest.java          # Unit tests for command channel
    StateChannelTest.java            # Unit tests for state channel
    ObserveChannelTest.java          # Unit tests for observe channel
    DebugChannelTest.java            # Unit tests for debug channel
    PtyChannelTest.java              # Unit tests for PTY channel
  transport/
    UnixSocketTransportTest.java     # Transport integration tests
    StdioTransportTest.java          # Stdio transport tests
    NamedPipeTransportTest.java      # Named pipe transport tests
```

### File Size Estimates

| File | Lines | Responsibility |
|------|-------|---------------|
| `BashClient.java` | ~400 | Facade, factories, lifecycle, reader thread |
| `BashClientException.java` | ~150 | Exception hierarchy with context fields |
| `NdjsonProtocol.java` | ~200 | NDJSON serialization/deserialization |
| `Transport.java` | ~50 | Interface: getInputStream, getOutputStream, close |
| `UnixSocketTransport.java` | ~100 | junixsocket AFUNIXSocket wrapper |
| `StdioTransport.java` | ~120 | ProcessBuilder + Process wrapper |
| `NamedPipeTransport.java` | ~80 | RandomAccessFile on \\.\pipe\ |
| Each channel class | ~150-300 | Channel-specific request/response methods |
| Each type class | ~50-100 | Immutable value objects |

---

## Class Hierarchy

### Main Classes

```
BashClient (AutoCloseable)
  - static connect(socketPath) -> BashClient
  - static connectStdio(args...) -> BashClient
  - static connectNamedPipe(pipeName) -> BashClient
  - auth(token)
  - eval(command) -> EvalResult
  - ping()
  - isConnected() -> boolean
  - isAuthenticated() -> boolean
  - close()
  - public final control: ControlChannel
  - public final command: CommandChannel
  - public final state: StateChannel
  - public final observe: ObserveChannel
  - public final debug: DebugChannel
  - public final pty: PtyChannel
```

### Transport Hierarchy

```
<<interface>> Transport
  + getInputStream(): InputStream
  + getOutputStream(): OutputStream
  + isOpen(): boolean
  + close(): void
    |
    +-- UnixSocketTransport
    |     - AFUNIXSocket socket
    |     - AFUNIXSocketAddress address
    |
    +-- StdioTransport
    |     - Process process
    |     - ProcessBuilder builder
    |
    +-- NamedPipeTransport
          - RandomAccessFile pipe
          - String pipePath
```

### Exception Hierarchy

```
BashClientException (extends Exception)
  |
  +-- AuthException
  |     - String providedToken (masked)
  |
  +-- ProtocolException
  |     - String rawData
  |     - String expectedFormat
  |
  +-- TimeoutException
  |     - Duration timeout
  |     - String channel
  |     - String action
  |
  +-- TransportException
  |     - String transportType
  |     - Throwable cause
  |
  +-- ServerException
        - String errorCode
        - String serverMessage
```

### Channel Hierarchy

All channels share a common pattern but do not extend a common base class.
Each channel is a standalone class that holds a reference to the protocol
layer and its own `BlockingQueue`:

```
ControlChannel
  - NdjsonProtocol protocol
  - BlockingQueue<ObjectNode> queue
  - ping(), disconnect(), configure(key, value)

CommandChannel
  - NdjsonProtocol protocol
  - BlockingQueue<ObjectNode> queue
  - eval(command), eval(command, timeout), evalParsed(commandTree)

StateChannel
  - NdjsonProtocol protocol
  - BlockingQueue<ObjectNode> queue
  - getVar, setVar, unsetVar, listVars
  - getFunc, setFunc, unsetFunc, listFuncs
  - getAlias, setAlias, unsetAlias, listAliases
  - getTrap, setTrap, unsetTrap, listTraps
  - inspect(namespaces...)

ObserveChannel
  - NdjsonProtocol protocol
  - BlockingQueue<ObjectNode> queue
  - List<Consumer<ObjectNode>> preCommandListeners
  - List<Consumer<ObjectNode>> postCommandListeners
  - onPreCommand, onPostCommand, poll, removeAllListeners

DebugChannel
  - NdjsonProtocol protocol
  - BlockingQueue<ObjectNode> queue
  - List<Consumer<BreakHitEvent>> breakHitListeners
  - addBreakpoint, removeBreakpoint, listBreakpoints
  - step, next, finish, continueExecution, skip
  - inspectAst

PtyChannel
  - NdjsonProtocol protocol
  - BlockingQueue<ObjectNode> queue
  - List<Consumer<String>> outputListeners
  - spawn, writeInput, readOutput, resize, signal, close, configure
```

---

## Data Flow

### Outgoing (Client to Server)

```
User code
  |
  v
BashClient.eval("echo hello")
  |
  v
CommandChannel.eval("echo hello")
  |
  v
NdjsonProtocol.send(channelId=1, {"action":"eval","command":"echo hello"})
  |
  v
ObjectMapper.writeValueAsString(frame) + "\n"
  |
  v
Transport.getOutputStream().write(jsonBytes)
  |
  v
[Wire: NDJSON line to bash-server]
```

### Incoming (Server to Client)

```
[Wire: NDJSON line from bash-server]
  |
  v
Transport.getInputStream() -- read by daemon reader thread
  |
  v
BufferedReader.readLine()
  |
  v
NdjsonProtocol.parse(line) -> ObjectNode
  |
  v
Route by "channel" field:
  channel=0 -> controlQueue.put(msg)
  channel=1 -> commandQueue.put(msg)
  channel=2 -> stateQueue.put(msg)
  channel=3 -> observeQueue.put(msg) + notify listeners
  channel=4 -> debugQueue.put(msg) + notify listeners
  channel=5 -> ptyQueue.put(msg) + notify listeners
  |
  v
Waiting thread wakes up:
  commandQueue.poll(30, SECONDS) -> ObjectNode
  |
  v
CommandChannel parses ObjectNode into EvalResult
  |
  v
Return to user code
```

### Full Round-Trip

```
Thread A (user)              Reader Thread              Server
     |                            |                       |
     | eval("echo hi")           |                       |
     |--- send JSON ------------>|                       |
     |                            |--- wire write ------->|
     |                            |                       |
     |   commandQueue.poll()      |                       |
     |   (blocks, waiting)        |                       |
     |                            |                       |
     |                            |<-- wire read ---------|
     |                            |                       |
     |                            | parse JSON            |
     |                            | route to commandQueue  |
     |                            |                       |
     |<-- queue.poll() returns ---|                       |
     |                            |                       |
     | parse EvalResult           |                       |
     | return to caller           |                       |
```

---

## Daemon Reader Thread

The daemon reader thread is the central component of the asynchronous
message handling system.

### Lifecycle

```
BashClient constructor
  |
  v
new Thread(this::readerLoop, "bashclient-reader")
  |
  v
thread.setDaemon(true)  // Won't prevent JVM shutdown
  |
  v
thread.start()
  |
  v
readerLoop() runs continuously:
  while (!closed) {
      line = reader.readLine()
      if (line == null) break    // EOF
      msg = protocol.parse(line)
      routeMessage(msg)
  }
  |
  v
On exception or EOF:
  markDisconnected()
  // Thread exits naturally
```

### Implementation

```java
private void readerLoop() {
    try {
        BufferedReader reader = new BufferedReader(
            new InputStreamReader(transport.getInputStream(),
                StandardCharsets.UTF_8));

        String line;
        while (!closed && (line = reader.readLine()) != null) {
            try {
                ObjectNode msg = protocol.parse(line);
                routeMessage(msg);
            } catch (Exception e) {
                // Log parse error, continue reading
                System.err.println("bashclient: parse error: " + e.getMessage());
            }
        }
    } catch (IOException e) {
        if (!closed) {
            // Unexpected disconnection
            lastError = new BashClientException.TransportException(
                "Connection lost", e);
        }
    } finally {
        markDisconnected();
    }
}
```

### Error Handling in Reader Thread

| Scenario | Action |
|----------|--------|
| Single malformed line | Log warning, skip, continue reading |
| `readLine()` returns null (EOF) | Mark disconnected, exit thread |
| `IOException` during read | Mark disconnected, store error, exit thread |
| `closed` flag set | Exit loop normally |

### Why Daemon Thread?

The reader thread is a daemon thread (`setDaemon(true)`) so it does not
prevent JVM shutdown. If the user forgets to call `close()` and the main
thread exits, the JVM will still terminate. The daemon thread is a safety
net, not a substitute for proper cleanup.

---

## BlockingQueue Routing

### Queue Configuration

Each channel has its own `LinkedBlockingQueue<ObjectNode>`:

```java
private final BlockingQueue<ObjectNode> controlQueue = new LinkedBlockingQueue<>();
private final BlockingQueue<ObjectNode> commandQueue = new LinkedBlockingQueue<>();
private final BlockingQueue<ObjectNode> stateQueue = new LinkedBlockingQueue<>();
private final BlockingQueue<ObjectNode> observeQueue = new LinkedBlockingQueue<>();
private final BlockingQueue<ObjectNode> debugQueue = new LinkedBlockingQueue<>();
private final BlockingQueue<ObjectNode> ptyQueue = new LinkedBlockingQueue<>();
```

### Routing Logic

```java
private void routeMessage(ObjectNode msg) {
    int channel = msg.get("channel").asInt();

    switch (channel) {
        case 0: controlQueue.put(msg); break;
        case 1: commandQueue.put(msg); break;
        case 2: stateQueue.put(msg); break;
        case 3:
            observeQueue.put(msg);
            notifyObserveListeners(msg);
            break;
        case 4:
            debugQueue.put(msg);
            notifyDebugListeners(msg);
            break;
        case 5:
            ptyQueue.put(msg);
            notifyPtyListeners(msg);
            break;
        default:
            System.err.println("bashclient: unknown channel: " + channel);
    }
}
```

### Queue vs. Callback

Channels 0-2 (control, command, state) are request-response only. The calling
thread sends a request and polls the queue for the response.

Channels 3-5 (observe, debug, pty) support both request-response AND
server-initiated events. For these channels:

1. Messages are always put on the queue (for polling)
2. Registered listeners are also notified (for callback pattern)
3. The calling thread can use either approach

```
Request-response channels (0-2):

  User thread:    send() ---> queue.poll(30s) ---> return result

Event channels (3-5):

  User thread:    send() ---> queue.poll(30s) ---> return result
                                     AND
  Reader thread:  routeMessage() ---> queue.put() + listeners.forEach(notify)
```

### Timeout Behavior

```java
// Default timeout: 30 seconds
private static final Duration DEFAULT_TIMEOUT = Duration.ofSeconds(30);

public ObjectNode awaitResponse(BlockingQueue<ObjectNode> queue,
                                 Duration timeout)
        throws BashClientException {
    try {
        ObjectNode msg = queue.poll(timeout.toMillis(), TimeUnit.MILLISECONDS);
        if (msg == null) {
            throw new BashClientException.TimeoutException(
                "No response within " + timeout, timeout);
        }
        return msg;
    } catch (InterruptedException e) {
        Thread.currentThread().interrupt();
        throw new BashClientException.TransportException(
            "Interrupted while waiting for response", e);
    }
}
```

---

## Transport Layer

### Transport Interface

```java
public interface Transport extends Closeable {
    /**
     * Returns the input stream for reading server messages.
     */
    InputStream getInputStream() throws IOException;

    /**
     * Returns the output stream for sending client messages.
     */
    OutputStream getOutputStream() throws IOException;

    /**
     * Returns true if the transport is currently open and usable.
     */
    boolean isOpen();

    /**
     * Closes the transport and releases all resources.
     */
    @Override
    void close() throws IOException;
}
```

### UnixSocketTransport

```java
public class UnixSocketTransport implements Transport {
    private final AFUNIXSocket socket;

    public UnixSocketTransport(String socketPath) throws IOException {
        AFUNIXSocketAddress address =
            AFUNIXSocketAddress.of(new File(socketPath));
        this.socket = AFUNIXSocket.newInstance();
        this.socket.connect(address);
    }

    @Override
    public InputStream getInputStream() throws IOException {
        return socket.getInputStream();
    }

    @Override
    public OutputStream getOutputStream() throws IOException {
        return socket.getOutputStream();
    }

    @Override
    public boolean isOpen() {
        return socket.isConnected() && !socket.isClosed();
    }

    @Override
    public void close() throws IOException {
        socket.close();
    }
}
```

### StdioTransport

```java
public class StdioTransport implements Transport {
    private final Process process;

    public StdioTransport(String... command) throws IOException {
        ProcessBuilder pb = new ProcessBuilder(command);
        pb.redirectErrorStream(false);
        this.process = pb.start();
    }

    @Override
    public InputStream getInputStream() {
        return process.getInputStream();  // server's stdout
    }

    @Override
    public OutputStream getOutputStream() {
        return process.getOutputStream(); // server's stdin
    }

    @Override
    public boolean isOpen() {
        return process.isAlive();
    }

    @Override
    public void close() throws IOException {
        process.getOutputStream().close();
        process.destroy();
        try {
            process.waitFor(5, TimeUnit.SECONDS);
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        if (process.isAlive()) {
            process.destroyForcibly();
        }
    }
}
```

### NamedPipeTransport

```java
public class NamedPipeTransport implements Transport {
    private final RandomAccessFile pipe;
    private final String pipePath;
    private volatile boolean open = true;

    public NamedPipeTransport(String pipeName) throws IOException {
        this.pipePath = "\\\\.\\pipe\\" + pipeName;
        this.pipe = new RandomAccessFile(pipePath, "rw");
    }

    @Override
    public InputStream getInputStream() {
        return new FileInputStream(pipe.getFD());
    }

    @Override
    public OutputStream getOutputStream() {
        return new FileOutputStream(pipe.getFD());
    }

    @Override
    public boolean isOpen() {
        return open;
    }

    @Override
    public void close() throws IOException {
        open = false;
        pipe.close();
    }
}
```

### Transport Selection Logic

```java
// In BashClient static factory methods:

public static BashClient connect(String socketPath) throws IOException {
    Transport transport = new UnixSocketTransport(socketPath);
    return new BashClient(transport);
}

public static BashClient connectStdio(String... args) throws IOException {
    Transport transport = new StdioTransport(args);
    return new BashClient(transport);
}

public static BashClient connectNamedPipe(String pipeName) throws IOException {
    Transport transport = new NamedPipeTransport(pipeName);
    return new BashClient(transport);
}
```

---

## Protocol Layer

### NDJSON Wire Format

The Java client uses NDJSON (Newline-Delimited JSON) exclusively. Each message
is a single JSON object followed by a newline character (`\n`).

```
{"channel":0,"action":"auth","token":"abc123..."}\n
{"channel":1,"action":"eval","command":"echo hello"}\n
```

### NdjsonProtocol

```java
public class NdjsonProtocol {
    private final ObjectMapper mapper;
    private final OutputStream output;
    private final Object writeLock = new Object();

    public NdjsonProtocol(OutputStream output) {
        this.mapper = new ObjectMapper();
        this.output = output;
    }

    /**
     * Send a message on the specified channel.
     */
    public void send(int channel, ObjectNode payload) throws IOException {
        payload.put("channel", channel);
        byte[] bytes = mapper.writeValueAsBytes(payload);

        synchronized (writeLock) {
            output.write(bytes);
            output.write('\n');
            output.flush();
        }
    }

    /**
     * Parse a single NDJSON line into an ObjectNode.
     */
    public ObjectNode parse(String line) throws BashClientException {
        try {
            return (ObjectNode) mapper.readTree(line);
        } catch (Exception e) {
            throw new BashClientException.ProtocolException(
                "Failed to parse NDJSON: " + e.getMessage(), line);
        }
    }

    /**
     * Create a new empty message.
     */
    public ObjectNode createMessage() {
        return mapper.createObjectNode();
    }

    /**
     * Get the shared ObjectMapper for custom serialization.
     */
    public ObjectMapper getMapper() {
        return mapper;
    }
}
```

### Write Synchronization

The `writeLock` ensures that concurrent sends from different threads do not
interleave bytes on the wire. Each `send()` call writes the complete JSON
line atomically:

```java
synchronized (writeLock) {
    output.write(bytes);   // JSON payload
    output.write('\n');    // Line terminator
    output.flush();        // Ensure delivery
}
```

### Message Format

Every message has a `channel` field (0-5) and an `action` field:

```json
{"channel": 0, "action": "auth", "token": "..."}
{"channel": 1, "action": "eval", "command": "echo hello"}
{"channel": 2, "action": "get", "namespace": "variable", "name": "PATH"}
{"channel": 3, "action": "subscribe", "events": ["pre_command", "post_command"]}
{"channel": 4, "action": "add_breakpoint", "type": "command", "pattern": "rm *"}
{"channel": 5, "action": "spawn", "cols": 80, "rows": 24}
```

Response messages include a `status` field:

```json
{"channel": 0, "status": "ok"}
{"channel": 1, "status": "complete", "exit_code": 0, "stdout": "hello\n", "stderr": ""}
{"channel": 2, "status": "ok", "value": "/usr/bin:/bin", "exported": true}
{"channel": 3, "event": "post_command", "command": "echo hello", "exit_code": 0}
{"channel": 4, "event": "break_hit", "breakpoint_id": 1, "line": 42}
{"channel": 5, "event": "output", "data": "$ "}
```

---

## Channel Architecture

### Channel Lifecycle

```
BashClient constructor
  |
  v
Create NdjsonProtocol(transport.getOutputStream())
  |
  v
Create 6 BlockingQueues
  |
  v
Create 6 channel instances, each receiving:
  - NdjsonProtocol reference (for sending)
  - BlockingQueue reference (for receiving)
  |
  v
Assign to public final fields:
  this.control = new ControlChannel(protocol, controlQueue);
  this.command = new CommandChannel(protocol, commandQueue);
  this.state   = new StateChannel(protocol, stateQueue);
  this.observe = new ObserveChannel(protocol, observeQueue);
  this.debug   = new DebugChannel(protocol, debugQueue);
  this.pty     = new PtyChannel(protocol, ptyQueue);
```

### Channel Pattern (Request-Response)

All request-response channel methods follow this pattern:

```java
public EvalResult eval(String command) throws BashClientException {
    // 1. Build request
    ObjectNode request = protocol.createMessage();
    request.put("action", "eval");
    request.put("command", command);

    // 2. Send request
    try {
        protocol.send(CHANNEL_ID, request);
    } catch (IOException e) {
        throw new BashClientException.TransportException("Send failed", e);
    }

    // 3. Await response
    ObjectNode response = awaitResponse(queue, DEFAULT_TIMEOUT);

    // 4. Check for errors
    String status = response.get("status").asText();
    if ("error".equals(status)) {
        throw new BashClientException.ServerException(
            response.get("error").asText(),
            response.path("error_code").asText(null));
    }

    // 5. Parse and return result
    return new EvalResult(
        response.path("stdout").asText(""),
        response.path("stderr").asText(""),
        response.path("exit_code").asInt(0)
    );
}
```

### Channel IDs

| Constant | Value | Channel |
|----------|-------|---------|
| `CHAN_CONTROL` | 0 | Control |
| `CHAN_COMMAND` | 1 | Command |
| `CHAN_STATE` | 2 | State |
| `CHAN_OBSERVE` | 3 | Observe |
| `CHAN_DEBUG` | 4 | Debug |
| `CHAN_PTY` | 5 | PTY |

These are defined as package-private constants in `BashClient`:

```java
static final int CHAN_CONTROL = 0;
static final int CHAN_COMMAND = 1;
static final int CHAN_STATE   = 2;
static final int CHAN_OBSERVE = 3;
static final int CHAN_DEBUG   = 4;
static final int CHAN_PTY     = 5;
```

---

## Jackson ObjectNode Usage

### Why ObjectNode?

The library uses Jackson's `ObjectNode` as the internal message representation
rather than custom Java classes for each message type. Reasons:

1. **Flexibility**: New fields from the server don't break the client
2. **Simplicity**: No need for a class per message type
3. **Direct mapping**: JSON objects map 1:1 to ObjectNode
4. **Extensibility**: Users can access raw fields for new features

### ObjectNode Operations

```java
// Creating messages
ObjectMapper mapper = protocol.getMapper();
ObjectNode msg = mapper.createObjectNode();
msg.put("action", "eval");
msg.put("command", "echo hello");
msg.put("timeout", 30000);

// Reading responses
String status = response.get("status").asText();
int exitCode = response.path("exit_code").asInt(0);
boolean exported = response.path("exported").asBoolean(false);

// Checking for fields
if (response.has("error")) { ... }

// Nested objects
ObjectNode nested = (ObjectNode) response.get("details");

// Arrays
ArrayNode items = (ArrayNode) response.get("items");
for (JsonNode item : items) {
    String name = item.asText();
}
```

### path() vs get()

- `get("field")` returns `null` if the field does not exist
- `path("field")` returns a `MissingNode` that has safe defaults

```java
// Safe: returns 0 if exit_code is missing
int code = response.path("exit_code").asInt(0);

// Unsafe: NullPointerException if exit_code is missing
int code = response.get("exit_code").asInt();
```

The library uses `path()` with default values for optional fields and `get()`
for required fields (with null checks).

---

## Exception Design

### Checked vs. Unchecked

`BashClientException` is a checked exception (`extends Exception`). This
design choice means:

- Callers MUST handle or declare all bashclient errors
- Error handling is explicit and visible in method signatures
- IDE assistance for missing catch clauses
- Follows Java convention for recoverable conditions

`IllegalArgumentException` and `NullPointerException` are thrown for
programming errors (null arguments, empty strings) and are unchecked.

### Inner Class Pattern

Exception subclasses are inner classes of `BashClientException`:

```java
public class BashClientException extends Exception {
    // Base constructor
    public BashClientException(String message) { ... }
    public BashClientException(String message, Throwable cause) { ... }

    // Inner exception classes
    public static class AuthException extends BashClientException { ... }
    public static class ProtocolException extends BashClientException { ... }
    public static class TimeoutException extends BashClientException { ... }
    public static class TransportException extends BashClientException { ... }
    public static class ServerException extends BashClientException { ... }
}
```

This pattern:

- Groups all exceptions under one import
- Makes the hierarchy clear in code: `BashClientException.TimeoutException`
- Allows catching the base class for catch-all handling
- Keeps the package clean (one file, not six)

### Context Fields

Each exception carries context beyond the message:

```java
public static class TimeoutException extends BashClientException {
    private final Duration timeout;
    private final String channel;
    private final String action;

    public Duration getTimeout() { return timeout; }
    public String getChannel() { return channel; }
    public String getAction() { return action; }
}
```

This enables structured error handling:

```java
catch (BashClientException.TimeoutException e) {
    logger.warn("Timeout after {}s on {}.{}",
        e.getTimeout().getSeconds(),
        e.getChannel(),
        e.getAction());
}
```

---

## Testing Architecture

### JUnit 5 Organization

```
src/test/java/org/gnu/bash/client/
  BashClientTest.java            # Integration: full lifecycle tests
  NdjsonProtocolTest.java        # Unit: parse/serialize JSON frames
  channels/
    ControlChannelTest.java      # Unit: auth, ping, disconnect, configure
    CommandChannelTest.java      # Unit: eval, evalParsed, streaming
    StateChannelTest.java        # Unit: all var/func/alias/trap operations
    ObserveChannelTest.java      # Unit: subscribe, events, listeners
    DebugChannelTest.java        # Unit: breakpoints, stepping, AST
    PtyChannelTest.java          # Unit: spawn, I/O, resize, signal
  transport/
    UnixSocketTransportTest.java # Integration: real socket connection
    StdioTransportTest.java      # Integration: process spawn
    NamedPipeTransportTest.java  # Integration: named pipe (Windows only)
```

### Test Patterns

#### Mock Transport for Unit Tests

Channel tests use a mock transport to avoid needing a running bash-server:

```java
class MockTransport implements Transport {
    private final PipedInputStream clientInput;
    private final PipedOutputStream clientOutput;
    private final PipedInputStream serverInput;
    private final PipedOutputStream serverOutput;

    MockTransport() throws IOException {
        clientOutput = new PipedOutputStream();
        serverInput = new PipedInputStream(clientOutput);
        serverOutput = new PipedOutputStream();
        clientInput = new PipedInputStream(serverOutput);
    }

    // Simulate server sending a response
    void simulateResponse(String ndjsonLine) throws IOException {
        serverOutput.write((ndjsonLine + "\n").getBytes(StandardCharsets.UTF_8));
        serverOutput.flush();
    }
}
```

#### Timeout Annotations

All tests have timeout protection:

```java
@Test
@Timeout(value = 10, unit = TimeUnit.SECONDS)
void testEval() throws Exception {
    // Test code
}
```

#### Conditional Tests

Platform-specific tests use JUnit conditions:

```java
@Test
@EnabledOnOs(OS.LINUX)
void testUnixSocketTransport() { ... }

@Test
@EnabledOnOs(OS.WINDOWS)
void testNamedPipeTransport() { ... }

@Test
@DisabledIfEnvironmentVariable(named = "CI", matches = "true")
void testWithRealServer() { ... }
```

#### Test Lifecycle

```java
@BeforeEach
void setUp() throws Exception {
    mockTransport = new MockTransport();
    protocol = new NdjsonProtocol(mockTransport.getOutputStream());
    queue = new LinkedBlockingQueue<>();
    channel = new CommandChannel(protocol, queue);
}

@AfterEach
void tearDown() throws Exception {
    mockTransport.close();
}
```

### Test Categories

| Category | Tag | Requires | Run with |
|----------|-----|----------|----------|
| Unit tests | `@Tag("unit")` | Nothing | `mvn test -Dgroups=unit` |
| Integration | `@Tag("integration")` | bash-server | `mvn test -Dgroups=integration` |
| Platform | `@Tag("platform")` | Specific OS | `mvn test -Dgroups=platform` |

---

## Thread Model

### Thread Inventory

A typical `BashClient` instance uses these threads:

| Thread | Type | Name | Purpose |
|--------|------|------|---------|
| User thread | User | (varies) | Calls BashClient methods |
| Reader thread | Daemon | `bashclient-reader` | Reads from transport, routes messages |
| StdioTransport only | Daemon | `bashclient-stderr-drain` | Drains subprocess stderr |

### Thread Interactions

```
User Thread                 Reader Thread
     |                           |
     |  eval() ----------------  |
     |    protocol.send()        |
     |    queue.poll() [blocks]  |
     |                           | readLine()
     |                           | parse()
     |                           | routeMessage()
     |                           |   queue.put()
     |    queue.poll() returns   |
     |  <-- EvalResult           |
     |                           |
```

### Lock Hierarchy

To prevent deadlocks, locks are acquired in this order:

1. `NdjsonProtocol.writeLock` -- for serializing writes
2. No other locks held during `queue.poll()` -- blocking wait is lock-free
3. Listener lists use `CopyOnWriteArrayList` -- no explicit locking

### Thread Safety Annotations

While not enforced at compile time, the codebase documents thread safety:

```java
// Volatile fields for cross-thread visibility
private volatile boolean connected;
private volatile boolean authenticated;
private volatile boolean closed;

// Thread-safe collections
private final BlockingQueue<ObjectNode> commandQueue;
private final List<Consumer<ObjectNode>> listeners = new CopyOnWriteArrayList<>();

// Synchronized sections
synchronized (writeLock) { /* protocol writes */ }
```

---

## Shutdown Sequence

### Normal Shutdown (close())

```
1. Set closed = true (volatile write)
2. Send disconnect to control channel (best effort)
3. Close transport
   - UnixSocket: socket.close()
   - Stdio: process.destroy()
   - NamedPipe: pipe.close()
4. Reader thread sees EOF or IOException, exits
5. Clear all queues
6. Set connected = false, authenticated = false
```

### Abnormal Shutdown (transport failure)

```
1. Reader thread catches IOException
2. Store exception as lastError
3. Set connected = false
4. Queues remain intact (pending polls will timeout)
5. Next user call throws TransportException
```

### JVM Shutdown

```
1. Main thread exits
2. Shutdown hooks run (if registered)
3. Daemon reader thread is terminated by JVM
4. Resources may not be cleanly released
   (OS reclaims sockets/pipes on process exit)
```

### Close Idempotency

`close()` is idempotent -- calling it multiple times is safe:

```java
@Override
public void close() throws IOException {
    if (closed) return;
    closed = true;

    try {
        control.disconnect();
    } catch (BashClientException e) {
        // Best effort -- server may already be gone
    }

    transport.close();
}
```

---

## Design Decisions

### Why Not Java NIO / Channels?

The library uses blocking I/O (`InputStream`/`OutputStream`) instead of NIO
channels. Reasons:

1. **Simplicity**: Blocking I/O with a single reader thread is simpler than
   selector-based multiplexing
2. **junixsocket**: The junixsocket library provides blocking socket I/O
3. **One connection**: Each client has exactly one connection -- NIO's
   multiplexing advantage does not apply
4. **Reader thread**: The daemon thread model provides adequate concurrency

### Why Not CompletableFuture?

The API uses blocking calls instead of `CompletableFuture<EvalResult>`.
Reasons:

1. **Java 11 target**: CompletableFuture works but adds complexity for
   users targeting Java 11 without virtual threads
2. **Simpler API**: Blocking calls are easier to reason about
3. **BlockingQueue**: The internal queue model naturally supports blocking
4. **Timeout control**: `queue.poll(timeout)` is straightforward

A future version could add async variants alongside the blocking API.

### Why Inner Exception Classes?

Using `BashClientException.AuthException` instead of `BashAuthException`:

1. **Grouping**: All exceptions are visually grouped under one name
2. **Import**: Single import for the base class, use qualified names for specifics
3. **Hierarchy**: Clear parent-child relationship
4. **Convention**: Follows patterns like `javax.xml.transform.TransformerException`

### Why Public Final Channel Fields?

Using `client.state.getVar("PATH")` instead of `client.getVar("PATH")`:

1. **Organization**: Groups related methods under the channel name
2. **Discoverability**: IDE auto-complete shows channel methods
3. **Extensibility**: New channels don't pollute the main class
4. **Clarity**: Makes the protocol structure visible to users

---

## Next Steps

- [API Reference](API.md) -- complete method documentation
- [Guide](GUIDE.md) -- usage patterns and examples
- [Contributing](CONTRIBUTING.md) -- development workflow
