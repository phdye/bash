# Testing Guide

This document covers testing philosophy, practices, and infrastructure
for the bash-server client libraries. All four bindings follow the
same three-tier testing strategy.

---

## Table of Contents

- [Test Categories](#test-categories)
- [Unit Tests](#unit-tests)
- [Channel Tests](#channel-tests)
- [Integration Tests](#integration-tests)
- [Mock Transport Pattern](#mock-transport-pattern)
  - [Python Mock Transport](#python-mock-transport)
  - [TypeScript Mock Transport](#typescript-mock-transport)
  - [C Mock Transport](#c-mock-transport)
  - [Java Mock Transport](#java-mock-transport)
- [Test Coverage Requirements](#test-coverage-requirements)
- [Running Tests](#running-tests)
- [Writing New Tests](#writing-new-tests)
- [Test Infrastructure](#test-infrastructure)
  - [Python: pytest](#python-pytest)
  - [TypeScript: Jest](#typescript-jest)
  - [C: make check](#c-make-check)
  - [Java: JUnit 5](#java-junit-5)
- [Common Test Patterns](#common-test-patterns)
- [CI/CD Integration](#cicd-integration)
- [Debugging Test Failures](#debugging-test-failures)

---

## Test Categories

Every binding implements tests at three levels. Each level tests
different things and has different dependencies.

| Tier        | Tests What                      | Needs Server | Needs I/O | Speed    |
|-------------|--------------------------------|--------------|-----------|----------|
| Unit        | Protocol, encoding, types      | No           | No        | Fast     |
| Channel     | Channel methods, message flow  | No           | Mock only | Fast     |
| Integration | Full round-trip behavior       | Yes          | Real I/O  | Slower   |

### Why Three Tiers

- **Unit tests** catch encoding bugs, JSON format issues, and type
  conversion errors without any external dependencies. They run in
  milliseconds and should be the first line of defense.

- **Channel tests** verify that each channel method sends the correct
  request and handles the correct response, using a mock transport that
  simulates server replies. They catch logic errors in the channel
  layer without needing a running server.

- **Integration tests** verify end-to-end behavior against a real
  bash-server instance. They catch protocol mismatches, timing issues,
  and assumptions that unit/channel tests cannot detect.

---

## Unit Tests

Unit tests cover the protocol layer in isolation. No server, no I/O,
no async. Pure function tests.

### What to Test

| Component        | Test Cases                                        |
|------------------|---------------------------------------------------|
| `encode_frame`   | Correct JSON output, compact format, trailing `\n`, channel ID in output |
| `decode_frame`   | Valid frames, extract channel + data, strip newline |
| `decode_frame`   | Malformed JSON raises ProtocolError               |
| `decode_frame`   | Missing `ch` field raises ProtocolError           |
| `decode_frame`   | Invalid channel ID (negative, >5) raises error    |
| `encode_base64`  | Standard base64 output with padding               |
| `decode_base64`  | Correct round-trip with encode                    |
| `decode_base64`  | Invalid base64 input raises error                 |
| Message builders | Each message type produces correct JSON structure  |
| Type constructors| Dataclass/struct fields populated correctly        |
| Error types      | Each error type instantiates with message          |

### Example: Protocol Unit Tests

```python
# Python
def test_encode_frame_basic():
    result = encode_frame(0, {"type": "auth", "token": "abc"})
    assert result == b'{"ch":0,"type":"auth","token":"abc"}\n'

def test_encode_frame_compact():
    """No spaces in JSON output."""
    result = encode_frame(1, {"type": "eval", "cmd": "echo hi"})
    assert b" " not in result.rstrip(b"\n")

def test_decode_frame_basic():
    ch, data = decode_frame(b'{"ch":1,"type":"complete","exit_code":0}\n')
    assert ch == 1
    assert data["type"] == "complete"
    assert data["exit_code"] == 0

def test_decode_frame_missing_channel():
    with pytest.raises(ProtocolError):
        decode_frame(b'{"type":"auth"}\n')

def test_decode_frame_malformed_json():
    with pytest.raises(ProtocolError):
        decode_frame(b'not json\n')

def test_base64_roundtrip():
    original = b"\x00\x01\x02\xff\xfe\xfd"
    assert decode_base64(encode_base64(original)) == original

def test_base64_empty():
    assert encode_base64(b"") == ""
    assert decode_base64("") == b""
```

### Key Principle

Unit tests should be exhaustive for the protocol layer. Every edge
case in encoding/decoding should be covered: empty strings, unicode,
maximum-size frames, binary data, special JSON characters.

---

## Channel Tests

Channel tests verify each channel method's request/response behavior
using a mock transport. The mock transport has pre-loaded response
sequences that simulate server replies.

### What to Test

| Channel   | Test Cases                                             |
|-----------|--------------------------------------------------------|
| CONTROL   | auth success, auth failure, ping, configure, disconnect |
| COMMAND   | eval basic, eval with stderr, eval multi-chunk stdout, eval error, eval timeout |
| STATE     | get/set/unset for each namespace (var, func, alias, trap), inspect, not-found error |
| OBSERVE   | subscribe, unsubscribe, pre_command callback, post_command callback |
| DEBUG     | enable, disable, add/remove breakpoint, continue, step, next, finish, skip, inspect_ast, breakpoint_hit callback |
| PTY       | spawn, write, resize, signal, close, output callback, exit callback |

### Key Principle

Channel tests must cover both happy paths and error paths. For every
method, test:

1. **Success**: Method sends correct request, parses correct response
2. **Server error**: Server returns `{"type":"error",...}`, method
   raises appropriate exception
3. **Timeout**: No response arrives, method raises TimeoutError
4. **Multi-frame**: For eval, test assembly of multiple stdout/stderr
   chunks before complete

---

## Integration Tests

Integration tests run against a real bash-server instance using the
stdio transport (`bash-server --stdio`).

### Prerequisites

- `bash-server` binary must be on PATH or at a known location
- The binary must support `--stdio` mode
- Integration tests are skipped if the binary is not available

### What to Test

| Test                          | Verifies                                |
|-------------------------------|-----------------------------------------|
| Connect and authenticate      | Full handshake works                    |
| Eval simple command           | `echo hello` produces correct stdout    |
| Eval with exit code           | `exit 42` returns exit_code=42          |
| Eval with stderr              | Command stderr captured correctly       |
| State get variable            | `$PATH` readable                        |
| State set/get roundtrip       | Set MY_VAR, get MY_VAR, values match    |
| State unset                   | Unset MY_VAR, get returns not-found     |
| State inspect                 | Lists variables                         |
| Observe events                | Subscribe, eval, receive pre/post events |
| Ping                          | Returns server version and uptime       |
| Multiple sequential evals     | Back-to-back evals work correctly       |

### Test Lifecycle

```
1. Launch bash-server --stdio as subprocess
2. Read auth token from server stderr
3. Connect client via stdio transport
4. Authenticate with token
5. Run test operations
6. Close client (sends disconnect)
7. Wait for subprocess to exit
```

### Skipping When Unavailable

Integration tests must not fail when bash-server is not installed.
Use the language's skip mechanism:

```python
# Python
@pytest.mark.skipif(not shutil.which("bash-server"),
                    reason="bash-server not found")
class TestIntegration:
    ...
```

```typescript
// TypeScript
const BASH_SERVER = process.env.BASH_SERVER_PATH || "bash-server";
const available = /* check if binary exists */;
(available ? describe : describe.skip)("integration", () => { ... });
```

```c
/* C */
static int bash_server_available(void) {
    return system("which bash-server > /dev/null 2>&1") == 0;
}
/* Skip tests if unavailable */
```

```java
// Java
@EnabledIf("bashServerAvailable")
class IntegrationTest {
    static boolean bashServerAvailable() {
        return new File("/usr/local/bin/bash-server").exists()
            || System.getenv("BASH_SERVER_PATH") != null;
    }
}
```

---

## Mock Transport Pattern

All bindings use the same mock transport pattern for channel tests.
The mock transport has two responsibilities:

1. **Provide pre-loaded responses**: Return pre-defined NDJSON lines
   when `read_line()` is called
2. **Capture sent requests**: Record all data written via `write()`
   for later assertion

### Python Mock Transport

```python
from bashclient.transport import Transport
from typing import List

class MockTransport(Transport):
    """Mock transport with pre-loaded responses and write capture."""

    def __init__(self, responses: List[bytes]):
        self._responses = iter(responses)
        self._written: List[bytes] = []
        self._connected = True

    async def connect(self, **kwargs) -> None:
        self._connected = True

    async def read_line(self) -> bytes:
        try:
            return next(self._responses)
        except StopIteration:
            return b""

    async def write(self, data: bytes) -> None:
        self._written.append(data)

    async def close(self) -> None:
        self._connected = False

    @property
    def is_connected(self) -> bool:
        return self._connected

    @property
    def written(self) -> List[bytes]:
        return self._written

    def assert_sent(self, index: int, expected: dict) -> None:
        """Assert that the Nth written frame matches expected."""
        import json
        frame = json.loads(self._written[index].decode("utf-8").rstrip())
        for key, value in expected.items():
            assert frame[key] == value, (
                f"Frame {index}: expected {key}={value}, "
                f"got {key}={frame.get(key)}"
            )
```

Usage:

```python
async def test_eval_sends_correct_request():
    mock = MockTransport([
        b'{"ch":1,"type":"stdout","data":"hello\\n"}\n',
        b'{"ch":1,"type":"complete","exit_code":0}\n',
    ])
    client = BashClient._from_transport(mock)
    client._authenticated = True

    result = await client.eval("echo hello")

    assert result.stdout == "hello\n"
    assert result.exit_code == 0
    mock.assert_sent(0, {"ch": 1, "type": "eval", "cmd": "echo hello"})
```

### TypeScript Mock Transport

```typescript
import { Transport } from "../src/transport";

export class MockTransport implements Transport {
    private responses: Buffer[];
    private index = 0;
    private _written: Buffer[] = [];
    private _connected = true;

    constructor(responses: string[]) {
        this.responses = responses.map((r) => Buffer.from(r + "\n"));
    }

    async connect(): Promise<void> {
        this._connected = true;
    }

    async readLine(): Promise<Buffer> {
        if (this.index >= this.responses.length) {
            return Buffer.alloc(0);
        }
        return this.responses[this.index++];
    }

    async write(data: Buffer): Promise<void> {
        this._written.push(data);
    }

    async close(): Promise<void> {
        this._connected = false;
    }

    get isConnected(): boolean {
        return this._connected;
    }

    get written(): Buffer[] {
        return this._written;
    }

    assertSent(index: number, expected: Record<string, unknown>): void {
        const frame = JSON.parse(this._written[index].toString().trim());
        for (const [key, value] of Object.entries(expected)) {
            expect(frame[key]).toEqual(value);
        }
    }
}
```

Usage:

```typescript
test("eval sends correct request", async () => {
    const mock = new MockTransport([
        '{"ch":1,"type":"stdout","data":"hello\\n"}',
        '{"ch":1,"type":"complete","exit_code":0}',
    ]);
    const client = BashClient.fromTransport(mock);

    const result = await client.eval("echo hello");

    expect(result.stdout).toBe("hello\n");
    expect(result.exitCode).toBe(0);
    mock.assertSent(0, { ch: 1, type: "eval", cmd: "echo hello" });
});
```

### C Mock Transport

```c
#include "bashclient.h"
#include <string.h>
#include <stdlib.h>

#define MOCK_MAX_RESPONSES 64
#define MOCK_MAX_WRITTEN   64

typedef struct {
    const char *responses[MOCK_MAX_RESPONSES];
    int response_count;
    int response_index;
    char *written[MOCK_MAX_WRITTEN];
    int written_count;
} mock_transport_t;

static mock_transport_t *mock_create(const char **responses, int count) {
    mock_transport_t *m = calloc(1, sizeof(*m));
    m->response_count = count;
    for (int i = 0; i < count; i++)
        m->responses[i] = responses[i];
    return m;
}

static void mock_free(mock_transport_t *m) {
    for (int i = 0; i < m->written_count; i++)
        free(m->written[i]);
    free(m);
}

/*
 * These functions are registered as callbacks in the transport
 * vtable to replace real I/O with mock behavior.
 */
static int mock_read_line(void *ctx, char *buf, size_t buflen) {
    mock_transport_t *m = (mock_transport_t *)ctx;
    if (m->response_index >= m->response_count)
        return 0;  /* EOF */
    const char *line = m->responses[m->response_index++];
    size_t len = strlen(line);
    if (len >= buflen) len = buflen - 1;
    memcpy(buf, line, len);
    buf[len] = '\0';
    return (int)len;
}

static int mock_write(void *ctx, const char *data, size_t len) {
    mock_transport_t *m = (mock_transport_t *)ctx;
    if (m->written_count < MOCK_MAX_WRITTEN) {
        m->written[m->written_count] = strndup(data, len);
        m->written_count++;
    }
    return (int)len;
}
```

Usage:

```c
static void test_eval_sends_correct_request(void) {
    const char *responses[] = {
        "{\"ch\":1,\"type\":\"stdout\",\"data\":\"hello\\n\"}\n",
        "{\"ch\":1,\"type\":\"complete\",\"exit_code\":0}\n",
    };
    mock_transport_t *mock = mock_create(responses, 2);
    bc_client_t *c = bc_client_from_mock(mock);

    bc_eval_result_t result;
    int rc = bc_eval(c, "echo hello", &result);

    assert(rc == BC_OK);
    assert(strcmp(result.stdout_data, "hello\n") == 0);
    assert(result.exit_code == 0);

    /* Verify sent request */
    assert(mock->written_count == 1);
    assert(strstr(mock->written[0], "\"type\":\"eval\"") != NULL);
    assert(strstr(mock->written[0], "\"cmd\":\"echo hello\"") != NULL);

    bc_eval_result_free(&result);
    bc_close(c);
    mock_free(mock);
}
```

### Java Mock Transport

```java
import org.gnu.bash.client.transport.Transport;
import java.util.*;

public class MockTransport implements Transport {
    private final Queue<String> responses;
    private final List<String> written = new ArrayList<>();
    private boolean connected = true;

    public MockTransport(String... responses) {
        this.responses = new LinkedList<>(Arrays.asList(responses));
    }

    @Override
    public void connect(String path) {
        connected = true;
    }

    @Override
    public String readLine() {
        if (responses.isEmpty()) return null;
        return responses.poll();
    }

    @Override
    public void write(String data) {
        written.add(data);
    }

    @Override
    public void close() {
        connected = false;
    }

    @Override
    public boolean isConnected() {
        return connected;
    }

    public List<String> getWritten() {
        return Collections.unmodifiableList(written);
    }

    public void assertSent(int index, String key, Object expectedValue) {
        ObjectMapper mapper = new ObjectMapper();
        JsonNode frame = mapper.readTree(written.get(index));
        assertEquals(expectedValue.toString(), frame.get(key).asText(),
            "Frame " + index + ": " + key);
    }
}
```

Usage:

```java
@Test
void testEvalSendsCorrectRequest() throws Exception {
    MockTransport mock = new MockTransport(
        "{\"ch\":1,\"type\":\"stdout\",\"data\":\"hello\\n\"}",
        "{\"ch\":1,\"type\":\"complete\",\"exit_code\":0}"
    );
    BashClient client = BashClient.fromTransport(mock);

    EvalResult result = client.eval("echo hello");

    assertEquals("hello\n", result.getStdout());
    assertEquals(0, result.getExitCode());
    mock.assertSent(0, "type", "eval");
    mock.assertSent(0, "cmd", "echo hello");
}
```

---

## Test Coverage Requirements

### Minimum Coverage by Tier

| Tier        | Target   | What Must Be Covered                     |
|-------------|----------|------------------------------------------|
| Unit        | 100%     | Every encode/decode function, all edge cases, all error paths |
| Channel     | 100%     | Every public channel method, success + error paths |
| Integration | Core ops | Connect, auth, eval, state get/set, ping |

### Coverage by Module

| Module     | Unit Tests    | Channel Tests | Integration Tests |
|------------|---------------|---------------|-------------------|
| protocol   | 100%          | N/A           | N/A               |
| types      | 100%          | N/A           | N/A               |
| errors     | 100%          | N/A           | N/A               |
| transport  | Constructor   | Mock only     | Real transport    |
| channels   | N/A           | 100%          | Core paths        |
| client     | N/A           | Lifecycle     | Full lifecycle    |

### Edge Cases to Always Cover

- Empty strings and empty byte arrays
- Unicode characters (multi-byte UTF-8)
- Maximum-length frames (near 1 MB limit)
- Binary data with null bytes (via base64)
- JSON special characters in command strings (`"`, `\`, `\n`)
- Concurrent channel operations (async bindings)
- Timeout expiration
- Server disconnect during operation
- Multiple sequential operations (state not leaked)

---

## Running Tests

### Python

```bash
cd clients/python

# Install dev dependencies
pip install -e ".[dev]"

# Run all tests
pytest

# Run specific tier
pytest tests/test_protocol.py       # Unit tests
pytest tests/test_channels.py       # Channel tests
pytest tests/test_integration.py    # Integration tests

# Run with coverage
pytest --cov=bashclient --cov-report=term-missing

# Run with verbose output
pytest -v

# Run a single test
pytest tests/test_protocol.py::test_encode_frame_basic
```

### TypeScript

```bash
cd clients/typescript

# Install dependencies
npm install

# Run all tests
npm test

# Run specific test file
npx jest test/protocol.test.ts
npx jest test/channels.test.ts
npx jest test/integration.test.ts

# Run with coverage
npx jest --coverage

# Run with verbose output
npx jest --verbose

# Run a single test
npx jest -t "encodes frame"
```

### C

```bash
cd clients/c

# Build and run all tests
make check

# Build tests only (no run)
make tests

# Run specific test binary
./tests/test_protocol
./tests/test_channels
./tests/test_integration

# Run with valgrind (memory leak detection)
valgrind --leak-check=full ./tests/test_protocol

# Clean test artifacts
make clean-tests
```

### Java

```bash
cd clients/java

# Run all tests
mvn test

# Run specific test class
mvn test -Dtest=ProtocolTest
mvn test -Dtest=ChannelsTest
mvn test -Dtest=IntegrationTest

# Run with verbose output
mvn test -X

# Run a single test method
mvn test -Dtest="ProtocolTest#testEncodeFrame"

# Generate coverage report
mvn jacoco:report
# Report at: target/site/jacoco/index.html
```

---

## Writing New Tests

### When to Write Tests

- Adding a new channel operation: Add unit, channel, and integration tests
- Adding a new transport: Add unit tests for the transport
- Fixing a bug: Write a test that fails BEFORE the fix, passes after
- Refactoring: Ensure existing tests still pass; add tests for new code paths

### Test Structure

Follow the Arrange-Act-Assert pattern:

```python
# Python
def test_something():
    # Arrange: set up inputs and expected outputs
    input_data = b'{"ch":1,"type":"complete","exit_code":0}\n'
    
    # Act: call the function under test
    ch, data = decode_frame(input_data)
    
    # Assert: verify results
    assert ch == 1
    assert data["exit_code"] == 0
```

```typescript
// TypeScript
test("something", () => {
    // Arrange
    const input = '{"ch":1,"type":"complete","exit_code":0}\n';
    
    // Act
    const [ch, data] = decodeFrame(Buffer.from(input));
    
    // Assert
    expect(ch).toBe(1);
    expect(data.exit_code).toBe(0);
});
```

```c
/* C */
static void test_something(void) {
    /* Arrange */
    const char *input = "{\"ch\":1,\"type\":\"complete\",\"exit_code\":0}\n";
    
    /* Act */
    int ch;
    char *data;
    int rc = decode_frame(input, &ch, &data);
    
    /* Assert */
    assert(rc == 0);
    assert(ch == 1);
    assert(strstr(data, "\"exit_code\":0") != NULL);
    free(data);
}
```

```java
// Java
@Test
void testSomething() {
    // Arrange
    String input = "{\"ch\":1,\"type\":\"complete\",\"exit_code\":0}\n";
    
    // Act
    Frame frame = Protocol.decodeFrame(input);
    
    // Assert
    assertEquals(1, frame.getChannel());
    assertEquals(0, frame.getData().get("exit_code").asInt());
}
```

### Naming Conventions

| Language   | File Pattern               | Test Pattern                    |
|------------|----------------------------|---------------------------------|
| Python     | `test_<module>.py`         | `def test_<module>_<behavior>()` |
| TypeScript | `<module>.test.ts`         | `describe("<module>") / it("<behavior>")` |
| C          | `test_<module>.c`          | `void test_<module>_<behavior>(void)` |
| Java       | `<Module>Test.java`        | `@Test void test<Behavior>()`   |

### Test Isolation

- Each test must be independent (no shared mutable state)
- Tests must not depend on execution order
- Clean up any resources (connections, files, etc.) in teardown
- Use fresh mock transports for each channel test
- Integration tests should not assume prior state in the server

---

## Test Infrastructure

### Python: pytest

**Configuration** (`pyproject.toml`):

```toml
[project.optional-dependencies]
dev = ["pytest>=7.0", "pytest-asyncio>=0.20"]

[tool.pytest.ini_options]
asyncio_mode = "auto"
testpaths = ["tests"]
```

**Fixtures** (`tests/conftest.py`):

| Fixture                | Scope    | Purpose                           |
|------------------------|----------|-----------------------------------|
| `mock_transport`       | function | Factory for MockTransport         |
| `client_with_mock`     | function | BashClient wired to mock          |
| `bash_server_available`| session  | Skip marker for integration tests |
| `stdio_client`         | function | BashClient via stdio transport    |

### TypeScript: Jest

**Configuration** (`jest.config.js`):

```javascript
module.exports = {
    preset: "ts-jest",
    testEnvironment: "node",
    testMatch: ["**/test/**/*.test.ts"],
    coverageDirectory: "coverage",
    collectCoverageFrom: ["src/**/*.ts"],
};
```

**Test utilities** (`test/helpers.ts`):

| Helper                  | Purpose                              |
|-------------------------|--------------------------------------|
| `MockTransport`         | Mock transport class                 |
| `createMockClient()`    | BashClient factory with mock         |
| `bashServerAvailable()` | Check if bash-server binary exists   |
| `createStdioClient()`   | BashClient factory for integration   |

### C: make check

**Makefile targets**:

```makefile
TESTS = tests/test_protocol tests/test_channels tests/test_integration

check: $(TESTS)
	@for t in $(TESTS); do \
	    echo "Running $$t..."; \
	    ./$$t || exit 1; \
	done

tests/test_protocol: tests/test_protocol.c src/protocol.c
	$(CC) $(CFLAGS) -o $@ $^ -I include

tests/test_channels: tests/test_channels.c src/*.c tests/mock_transport.c
	$(CC) $(CFLAGS) -o $@ $^ -I include

tests/test_integration: tests/test_integration.c src/*.c
	$(CC) $(CFLAGS) -o $@ $^ -I include
```

**Test harness** (inline, no external framework):

```c
static int tests_run = 0;
static int tests_passed = 0;

#define RUN_TEST(fn) do { \
    printf("  %-40s ", #fn); \
    tests_run++; \
    fn(); \
    tests_passed++; \
    printf("PASS\n"); \
} while (0)

int main(void) {
    printf("test_protocol:\n");
    RUN_TEST(test_encode_frame_basic);
    RUN_TEST(test_decode_frame_basic);
    /* ... */
    printf("\n%d/%d tests passed\n", tests_passed, tests_run);
    return tests_run - tests_passed;
}
```

All C tests use `alarm()` timeouts to prevent hangs:

```c
int main(void) {
    alarm(10);  /* Kill test after 10 seconds */
    /* ... run tests ... */
}
```

### Java: JUnit 5

**Configuration** (`pom.xml`):

```xml
<dependencies>
    <dependency>
        <groupId>org.junit.jupiter</groupId>
        <artifactId>junit-jupiter</artifactId>
        <version>5.10.0</version>
        <scope>test</scope>
    </dependency>
</dependencies>
<build>
    <plugins>
        <plugin>
            <groupId>org.apache.maven.plugins</groupId>
            <artifactId>maven-surefire-plugin</artifactId>
            <version>3.1.2</version>
        </plugin>
    </plugins>
</build>
```

**Test annotations**:

| Annotation                | Usage                              |
|---------------------------|------------------------------------|
| `@Test`                   | Mark test method                   |
| `@Timeout(10)`            | Fail test after 10 seconds         |
| `@EnabledIf(...)`         | Conditional execution              |
| `@BeforeEach`             | Setup before each test             |
| `@AfterEach`              | Teardown after each test           |
| `@DisplayName("...")`     | Human-readable test name           |

---

## Common Test Patterns

### Pattern: Test Server Error Response

Every channel method must handle `{"type":"error",...}` responses:

```python
# Python
async def test_eval_server_error():
    mock = MockTransport([
        b'{"ch":1,"type":"error","message":"command not found"}\n',
    ])
    client = BashClient._from_transport(mock)
    client._authenticated = True

    with pytest.raises(ServerError, match="command not found"):
        await client.eval("nonexistent")
```

### Pattern: Test Timeout

```python
# Python
async def test_eval_timeout():
    mock = MockTransport([])  # No responses -- will EOF / timeout
    client = BashClient._from_transport(mock)
    client._authenticated = True

    with pytest.raises(TimeoutError):
        await client.eval("echo hello", timeout=0.1)
```

### Pattern: Test Multi-Frame Response

```python
# Python
async def test_eval_multi_chunk_stdout():
    mock = MockTransport([
        b'{"ch":1,"type":"stdout","data":"hello "}\n',
        b'{"ch":1,"type":"stdout","data":"world"}\n',
        b'{"ch":1,"type":"stderr","data":"warning\\n"}\n',
        b'{"ch":1,"type":"complete","exit_code":0}\n',
    ])
    client = BashClient._from_transport(mock)
    client._authenticated = True

    result = await client.eval("echo hello world")
    assert result.stdout == "hello world"
    assert result.stderr == "warning\n"
    assert result.exit_code == 0
```

### Pattern: Test Callback Invocation

```python
# Python
async def test_observe_callback_invoked():
    events = []
    mock = MockTransport([
        b'{"ch":3,"type":"start_ok"}\n',
        b'{"ch":3,"type":"pre_command","command":"echo hi"}\n',
        b'{"ch":3,"type":"post_command","command":"echo hi","exit_status":0}\n',
    ])
    client = BashClient._from_transport(mock)
    client._authenticated = True

    client.observe.on("pre_command", lambda e: events.append(("pre", e)))
    client.observe.on("post_command", lambda e: events.append(("post", e)))

    await client.observe.subscribe(level=0)
    # Process push events
    await asyncio.sleep(0.1)

    assert len(events) == 2
    assert events[0][0] == "pre"
    assert events[1][0] == "post"
```

### Pattern: Test Auth Before Operations

```python
# Python
async def test_eval_requires_auth():
    mock = MockTransport([])
    client = BashClient._from_transport(mock)
    # NOT authenticated

    with pytest.raises(AuthError, match="not authenticated"):
        await client.eval("echo hello")
```

### Pattern: Integration Test Fixture

```python
# Python
@pytest.fixture
async def connected_client():
    """Fixture: authenticated client via stdio transport."""
    proc = await asyncio.create_subprocess_exec(
        "bash-server", "--stdio",
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    # Read token from stderr
    token_line = await proc.stderr.readline()
    token = token_line.decode().strip().split()[-1]

    client = BashClient()
    await client.connect_stdio(proc)
    await client.auth(token)
    yield client
    await client.close()
```

---

## CI/CD Integration

### GitHub Actions Example

```yaml
name: Client Tests
on: [push, pull_request]

jobs:
  python:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-python@v5
        with:
          python-version: "3.8"
      - run: |
          cd clients/python
          pip install -e ".[dev]"
          pytest --cov=bashclient

  typescript:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-node@v4
        with:
          node-version: "16"
      - run: |
          cd clients/typescript
          npm install
          npm test -- --coverage

  c:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - run: |
          cd clients/c
          make check

  java:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - uses: actions/setup-java@v4
        with:
          java-version: "11"
          distribution: "temurin"
      - run: |
          cd clients/java
          mvn test
```

### Integration Tests in CI

Integration tests require a bash-server binary. In CI, either:

1. Build bash-server from source as a prior step
2. Download a pre-built binary
3. Skip integration tests (unit + channel tests provide good coverage)

```yaml
  integration:
    needs: [python, typescript, c, java]
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build bash-server
        run: |
          cd bash-server
          make
          export PATH="$PWD:$PATH"
      - name: Run integration tests (all bindings)
        run: |
          cd clients/python && pytest tests/test_integration.py
          cd ../typescript && npx jest test/integration.test.ts
          cd ../c && ./tests/test_integration
          cd ../java && mvn test -Dtest=IntegrationTest
```

---

## Debugging Test Failures

### Common Failure Modes

| Symptom                          | Likely Cause                       | Fix                              |
|----------------------------------|------------------------------------|----------------------------------|
| Protocol test fails              | JSON field name mismatch           | Check PROTOCOL.md for exact names |
| Channel test timeout             | Mock transport missing a response  | Add the expected response frame  |
| Integration test hangs           | bash-server not responding         | Add timeout, check server stderr |
| Integration test auth fails      | Token extraction wrong             | Check server stderr format       |
| Multi-chunk assembly wrong       | Off-by-one in chunk concatenation  | Check loop termination condition |
| Callback not invoked             | Push event type not in dispatch table | Update the push event set     |
| Wrong error type raised          | Error mapping incorrect            | Check error response format      |

### Debugging Tips

1. **Print wire traffic**: Add temporary logging to the mock transport
   or real transport to see exact JSON frames sent and received.

2. **Test one frame at a time**: If a multi-frame test fails, create
   a simpler test with just one response frame to isolate the issue.

3. **Compare across bindings**: If a test passes in Python but fails
   in TypeScript, compare the exact JSON frames -- there may be a
   field name mismatch (`exit_code` vs `exitCode` in the wire format).

4. **Check timeouts**: If a test hangs, add aggressive timeouts. In C,
   always use `alarm()`. In Python, use `pytest-timeout`. In Java,
   use `@Timeout`.

5. **Use real server for debugging**: If channel tests pass but
   integration tests fail, run `bash-server --stdio` manually and
   send NDJSON frames by hand to see what the server actually returns.

```bash
# Manual server interaction for debugging
bash-server --stdio 2>token.txt
# Read token from token.txt, then type:
{"ch":0,"type":"auth","token":"<paste-token-here>"}
# Server responds with auth_ok
{"ch":1,"type":"eval","cmd":"echo hello"}
# Server responds with stdout + complete
```
