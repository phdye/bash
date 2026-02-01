# Contributing to bashclient Java

## Development Guide

**Package**: `org.gnu.bash.client`
**Java**: 11+
**Build**: Maven 3.6+
**License**: GNU General Public License v3.0 or later

---

## Table of Contents

1. [Development Setup](#development-setup)
2. [Project Structure](#project-structure)
3. [Coding Style](#coding-style)
4. [Testing](#testing)
5. [Adding New Methods](#adding-new-methods)
6. [Adding New Transports](#adding-new-transports)
7. [Adding New Channels](#adding-new-channels)
8. [Dependency Management](#dependency-management)
9. [Documentation Standards](#documentation-standards)
10. [Pull Request Process](#pull-request-process)
11. [Release Process](#release-process)

---

## Development Setup

### Prerequisites

| Tool | Version | Check command |
|------|---------|---------------|
| JDK | 11+ | `java -version` |
| Maven | 3.6+ | `mvn --version` |
| Git | 2.x+ | `git --version` |
| bash-server | latest | `./bash-server/bash-server --version` |

### Initial Setup

```bash
# Clone the repository
git clone <repository-url>
cd bash/bash-server/clients/java

# Build and run tests
mvn verify

# Install to local Maven repository
mvn install

# Generate IDE project files (optional)
mvn eclipse:eclipse    # For Eclipse
mvn idea:idea          # For IntelliJ (prefer native import instead)
```

### Environment Variables

| Variable | Purpose | Example |
|----------|---------|---------|
| `JAVA_HOME` | JDK installation path | `/usr/lib/jvm/java-11` |
| `BASH_SERVER_SOCKET` | Test server socket | `/tmp/bash-server-1000/sock` |
| `BASH_SERVER_TOKEN` | Test server token | `(read from file)` |

### Running bash-server for Development

```bash
# Start a test server instance
cd /path/to/bash-source
./bash-server/bash-server --name dev --no-peercred

# In another terminal, get the token
TOKEN=$(cat ~/.config/bash-server/dev/token)
echo "BASH_SERVER_TOKEN=$TOKEN"
```

---

## Project Structure

```
bash-server/clients/java/
  pom.xml                            # Maven build configuration
  INSTALL.md                         # Installation guide
  GUIDE.md                           # Usage guide
  ARCHITECTURE.md                    # Internal design
  CONTRIBUTING.md                    # This file
  API.md                             # API reference
  TROUBLESHOOTING.md                 # Common issues
  CHANGELOG.md                       # Version history
  api-metadata.json                  # Machine-readable API metadata
  examples/
    README.md                        # Example index
    Eval.java                        # Basic evaluation example
    Observe.java                     # Observer pattern example
    Debugger.java                    # Debug channel example
    PtySession.java                  # PTY session example
  src/
    main/java/org/gnu/bash/client/
      BashClient.java               # Main class
      BashClientException.java       # Exceptions
      NdjsonProtocol.java           # Wire protocol
      Transport.java                # Transport interface
      channels/                     # Channel implementations
      transport/                    # Transport implementations
      types/                        # Value types
    test/java/org/gnu/bash/client/
      ...                           # Test classes (mirror main structure)
```

---

## Coding Style

### Java Conventions

Follow standard Java coding conventions with these specifics:

#### Naming

| Element | Convention | Example |
|---------|-----------|---------|
| Classes | PascalCase | `BashClient`, `EvalResult` |
| Methods | camelCase | `getVar`, `setAlias`, `inspectAst` |
| Constants | UPPER_SNAKE | `CHAN_CONTROL`, `DEFAULT_TIMEOUT` |
| Parameters | camelCase | `socketPath`, `pipeName` |
| Packages | lowercase | `org.gnu.bash.client.channels` |
| Type parameters | single uppercase | `T`, `E` |

#### Formatting

- **Indentation**: 4 spaces (no tabs)
- **Line length**: 100 characters maximum
- **Braces**: opening brace on same line (K&R style)
- **Blank lines**: one between methods, two between sections

```java
// Good
public EvalResult eval(String command) throws BashClientException {
    if (command == null) {
        throw new IllegalArgumentException("command must not be null");
    }
    ObjectNode request = protocol.createMessage();
    request.put("action", "eval");
    request.put("command", command);

    protocol.send(CHAN_COMMAND, request);
    return parseEvalResponse(awaitResponse(commandQueue));
}

// Bad: wrong brace style, no validation
public EvalResult eval(String command) throws BashClientException
{
    ObjectNode request = protocol.createMessage();
    request.put("action", "eval");
    request.put("command", command);
    protocol.send(CHAN_COMMAND, request);
    return parseEvalResponse(awaitResponse(commandQueue));
}
```

#### Imports

- No wildcard imports (`import java.util.*`)
- Group imports: `java.*`, blank line, `javax.*`, blank line, third-party, blank line, project
- Remove unused imports

```java
import java.io.IOException;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.BlockingQueue;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;

import org.gnu.bash.client.types.EvalResult;
```

#### Javadoc

All public classes and methods must have Javadoc:

```java
/**
 * Evaluates a bash command on the server and returns the result.
 *
 * <p>The command is sent to the server's command channel and executed
 * in the session's bash environment. The method blocks until the
 * server returns a response or the timeout expires.</p>
 *
 * @param command the bash command to evaluate; must not be null or blank
 * @return the evaluation result containing stdout, stderr, and exit code
 * @throws BashClientException.TimeoutException if no response within 30 seconds
 * @throws BashClientException.TransportException if the connection is lost
 * @throws BashClientException.ServerException if the server reports an error
 * @throws IllegalArgumentException if command is null or blank
 */
public EvalResult eval(String command) throws BashClientException {
    // ...
}
```

#### Null Handling

- Validate public method parameters for null at the top of the method
- Use `Objects.requireNonNull()` for constructor parameters
- Document nullable returns with `@return ... or {@code null} if ...`
- Prefer empty collections over null returns

```java
public BashClient(Transport transport) {
    this.transport = Objects.requireNonNull(transport, "transport");
    this.protocol = new NdjsonProtocol(transport.getOutputStream());
}
```

#### Exception Usage

- Throw `IllegalArgumentException` for programming errors (null/invalid args)
- Throw `BashClientException` subtypes for operational errors
- Never catch `Exception` or `Throwable` unless re-throwing
- Always preserve the cause chain

```java
// Good
try {
    protocol.send(channel, request);
} catch (IOException e) {
    throw new BashClientException.TransportException("Send failed", e);
}

// Bad: loses cause
try {
    protocol.send(channel, request);
} catch (IOException e) {
    throw new BashClientException.TransportException("Send failed");
}
```

---

## Testing

### Running Tests

```bash
# All tests
mvn test

# Specific test class
mvn test -Dtest=BashClientTest

# Specific test method
mvn test -Dtest="CommandChannelTest#testEval"

# By category tag
mvn test -Dgroups=unit
mvn test -Dgroups=integration

# With verbose output
mvn test -X

# Skip tests
mvn package -DskipTests
```

### Writing Tests

#### Unit Tests

Unit tests use mock transports and do not require a running bash-server:

```java
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.Timeout;
import static org.junit.jupiter.api.Assertions.*;

class CommandChannelTest {
    private MockTransport transport;
    private NdjsonProtocol protocol;
    private BlockingQueue<ObjectNode> queue;
    private CommandChannel channel;

    @BeforeEach
    void setUp() throws Exception {
        transport = new MockTransport();
        protocol = new NdjsonProtocol(transport.getOutputStream());
        queue = new LinkedBlockingQueue<>();
        channel = new CommandChannel(protocol, queue);
    }

    @Test
    @Timeout(10)
    void testEvalReturnsResult() throws Exception {
        // Simulate server response
        ObjectMapper mapper = new ObjectMapper();
        ObjectNode response = mapper.createObjectNode();
        response.put("channel", 1);
        response.put("status", "complete");
        response.put("exit_code", 0);
        response.put("stdout", "hello\n");
        response.put("stderr", "");
        queue.put(response);

        // Execute
        EvalResult result = channel.eval("echo hello");

        // Verify
        assertEquals("hello\n", result.getStdout());
        assertEquals("", result.getStderr());
        assertEquals(0, result.getExitCode());
    }

    @Test
    @Timeout(10)
    void testEvalThrowsOnTimeout() {
        // Queue is empty, so poll will timeout
        assertThrows(BashClientException.TimeoutException.class,
            () -> channel.eval("echo hello", Duration.ofMillis(100)));
    }
}
```

#### Integration Tests

Integration tests require a running bash-server:

```java
@Tag("integration")
class BashClientIntegrationTest {
    private static String socketPath;
    private static String token;

    @BeforeAll
    static void setUpServer() {
        socketPath = System.getenv("BASH_SERVER_SOCKET");
        token = System.getenv("BASH_SERVER_TOKEN");
        assumeTrue(socketPath != null, "BASH_SERVER_SOCKET not set");
        assumeTrue(token != null, "BASH_SERVER_TOKEN not set");
    }

    @Test
    @Timeout(30)
    void testFullLifecycle() throws Exception {
        try (BashClient client = BashClient.connect(socketPath)) {
            client.auth(token);
            assertTrue(client.isAuthenticated());

            EvalResult result = client.eval("echo integration-test");
            assertEquals(0, result.getExitCode());
            assertEquals("integration-test\n", result.getStdout());

            client.ping();
        }
    }
}
```

#### Test Requirements

1. **Every test MUST have a `@Timeout` annotation** (10s for unit, 30s for integration)
2. **No test may block indefinitely** -- use timeouts on all blocking operations
3. **Tests must be independent** -- no shared mutable state between tests
4. **Clean up resources** in `@AfterEach` or try-with-resources
5. **Use `assumeTrue`** for conditional tests (platform, server availability)

### Test Coverage

Aim for at least 80% line coverage on core classes:

```bash
# Generate coverage report
mvn verify -Pcoverage

# View report
open target/site/jacoco/index.html
```

---

## Adding New Methods

### To an Existing Channel

1. **Add the method** to the channel class:

```java
// In StateChannel.java
public List<String> listVars(String pattern) throws BashClientException {
    ObjectNode request = protocol.createMessage();
    request.put("action", "list");
    request.put("namespace", "variable");
    if (pattern != null) {
        request.put("pattern", pattern);
    }

    protocol.send(CHAN_STATE, request);
    ObjectNode response = awaitResponse(queue);
    checkError(response);

    List<String> names = new ArrayList<>();
    for (JsonNode node : response.get("names")) {
        names.add(node.asText());
    }
    return names;
}
```

2. **Add tests**:

```java
// In StateChannelTest.java
@Test
@Timeout(10)
void testListVarsWithPattern() throws Exception {
    // Setup mock response
    ObjectNode response = mapper.createObjectNode();
    response.put("channel", 2);
    response.put("status", "ok");
    ArrayNode names = response.putArray("names");
    names.add("PATH");
    names.add("CLASSPATH");
    queue.put(response);

    // Execute
    List<String> result = channel.listVars("*PATH*");

    // Verify
    assertEquals(2, result.size());
    assertTrue(result.contains("PATH"));
}
```

3. **Add Javadoc** with `@param`, `@return`, `@throws`
4. **Update API.md** with the new method
5. **Update api-metadata.json** with the new method entry

### To BashClient (Convenience Method)

If the method is frequently used, add a convenience wrapper:

```java
// In BashClient.java
/**
 * Convenience method for {@link StateChannel#getVar(String)}.
 */
public VarInfo getVar(String name) throws BashClientException {
    return state.getVar(name);
}
```

Only add convenience methods for the most common operations. Most methods
should live on their channel class.

---

## Adding New Transports

1. **Implement the `Transport` interface**:

```java
// src/main/java/org/gnu/bash/client/transport/NewTransport.java
public class NewTransport implements Transport {
    public NewTransport(/* constructor params */) throws IOException {
        // Setup connection
    }

    @Override
    public InputStream getInputStream() throws IOException { ... }

    @Override
    public OutputStream getOutputStream() throws IOException { ... }

    @Override
    public boolean isOpen() { ... }

    @Override
    public void close() throws IOException { ... }
}
```

2. **Add a static factory method to BashClient**:

```java
public static BashClient connectNew(/* params */) throws IOException {
    Transport transport = new NewTransport(/* params */);
    return new BashClient(transport);
}
```

3. **Add transport tests** in `transport/NewTransportTest.java`
4. **Update documentation**: GUIDE.md, API.md, ARCHITECTURE.md, api-metadata.json

---

## Adding New Channels

Adding a new channel requires changes in multiple places:

1. **Define the channel constant** in `BashClient`:

```java
static final int CHAN_NEW = 6;
```

2. **Create the channel class** in `channels/`:

```java
public class NewChannel {
    private final NdjsonProtocol protocol;
    private final BlockingQueue<ObjectNode> queue;

    NewChannel(NdjsonProtocol protocol, BlockingQueue<ObjectNode> queue) {
        this.protocol = protocol;
        this.queue = queue;
    }

    // Channel methods...
}
```

3. **Add queue and field** in `BashClient`:

```java
private final BlockingQueue<ObjectNode> newQueue = new LinkedBlockingQueue<>();
public final NewChannel newChannel;
```

4. **Add routing** in the reader thread's `routeMessage()`:

```java
case 6: newQueue.put(msg); break;
```

5. **Initialize** in the constructor:

```java
this.newChannel = new NewChannel(protocol, newQueue);
```

6. **Add tests** for the new channel
7. **Update all documentation** files

---

## Dependency Management

### Adding Dependencies

Before adding a new dependency:

1. **Justify the addition** -- can the functionality be implemented without it?
2. **Check the license** -- must be GPL-compatible
3. **Check transitive dependencies** -- minimize dependency tree growth
4. **Prefer well-maintained libraries** with active communities

Add to `pom.xml` with explicit version:

```xml
<dependency>
    <groupId>group</groupId>
    <artifactId>artifact</artifactId>
    <version>${artifact.version}</version>
</dependency>
```

### Version Properties

All dependency versions must be defined as properties:

```xml
<properties>
    <junixsocket.version>2.9.1</junixsocket.version>
    <jackson.version>2.17.0</jackson.version>
    <junit.version>5.10.2</junit.version>
</properties>
```

### Updating Dependencies

```bash
# Check for updates
mvn versions:display-dependency-updates

# Update to latest versions
mvn versions:use-latest-releases

# Verify after update
mvn verify
```

---

## Documentation Standards

### Javadoc

- All public classes, methods, and fields must have Javadoc
- Use `@param`, `@return`, `@throws` for all applicable elements
- Include code examples in `{@code ...}` or `<pre>` blocks
- Link to related methods with `{@link ...}`

### Markdown Documentation

- Use ATX headers (`#`, `##`, `###`)
- Code blocks with language specifier (````java`, ````bash`)
- Tables for structured data
- Reference links between documents

### When to Update Documentation

| Change | Files to Update |
|--------|----------------|
| New public method | API.md, api-metadata.json, Javadoc |
| New class | API.md, ARCHITECTURE.md, api-metadata.json |
| New transport | GUIDE.md, API.md, ARCHITECTURE.md, INSTALL.md |
| New channel | All documentation files |
| Bug fix | CHANGELOG.md |
| New feature | GUIDE.md, CHANGELOG.md |
| Dependency change | INSTALL.md, CHANGELOG.md |

---

## Pull Request Process

### Before Submitting

1. **Run the full test suite**: `mvn verify`
2. **Check code style**: review against coding standards above
3. **Update documentation**: all affected files
4. **Update CHANGELOG.md**: add entry under `[Unreleased]`
5. **Ensure no new warnings**: `mvn compile -Xlint:all`

### PR Template

```markdown
## Summary
Brief description of the change.

## Changes
- List of specific changes

## Testing
- How the changes were tested
- New tests added

## Documentation
- Documentation files updated

## Checklist
- [ ] Tests pass (`mvn verify`)
- [ ] Javadoc added/updated for public API
- [ ] CHANGELOG.md updated
- [ ] API.md updated (if public API changed)
- [ ] api-metadata.json updated (if public API changed)
- [ ] No new compiler warnings
```

### Review Criteria

PRs are reviewed for:

1. **Correctness**: Does the code do what it claims?
2. **Thread safety**: Are shared resources properly synchronized?
3. **Error handling**: Are all exceptions properly handled?
4. **Testing**: Is there adequate test coverage?
5. **Documentation**: Are public APIs documented?
6. **Style**: Does the code follow the coding standards?
7. **Performance**: Are there obvious performance issues?

---

## Release Process

### Version Numbering

Follow [Semantic Versioning](https://semver.org/):

- **MAJOR**: Breaking API changes
- **MINOR**: New features, backward-compatible
- **PATCH**: Bug fixes, backward-compatible

### Release Steps

1. Update version in `pom.xml`
2. Update `CHANGELOG.md` (move Unreleased to version heading)
3. Update `api-metadata.json` version field
4. Run full test suite: `mvn verify`
5. Build release artifacts: `mvn package -Prelease`
6. Tag the release: `git tag v0.1.0`
7. Deploy to Maven repository: `mvn deploy`

---

## Questions?

- Check existing documentation first
- Look at similar code in the project for patterns
- Review the [Architecture](ARCHITECTURE.md) for design decisions
- Open an issue for design questions before implementing large changes
