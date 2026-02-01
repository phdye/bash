# bashclient Java Troubleshooting

## Common Issues and Solutions

**Package**: `org.gnu.bash.client`
**Java**: 11+
**License**: GNU General Public License v3.0 or later

---

## Table of Contents

1. [Build Issues](#build-issues)
2. [Dependency Issues](#dependency-issues)
3. [Connection Issues](#connection-issues)
4. [Authentication Issues](#authentication-issues)
5. [Timeout Issues](#timeout-issues)
6. [Thread Safety Issues](#thread-safety-issues)
7. [Platform-Specific Issues](#platform-specific-issues)
8. [Protocol Issues](#protocol-issues)
9. [Debugging Tips](#debugging-tips)
10. [FAQ](#faq)

---

## Build Issues

### Maven build fails with "source option 11 is not supported"

**Symptom:**

```
[ERROR] Source option 11 is not supported. Use 7 or later.
```

**Cause:** Maven is using an older JDK that does not support Java 11.

**Solution:**

```bash
# Check which Java Maven is using
mvn --version

# Set JAVA_HOME to a JDK 11+ installation
export JAVA_HOME=/usr/lib/jvm/java-11-openjdk
mvn package
```

Or add to `~/.mavenrc`:

```bash
JAVA_HOME=/usr/lib/jvm/java-11-openjdk
```

---

### Maven build fails with "Cannot find symbol"

**Symptom:** Compilation errors referencing bashclient classes.

**Cause:** Dependencies not resolved or stale cache.

**Solution:**

```bash
# Clean and rebuild
mvn clean package

# Force dependency refresh
mvn package -U

# Clear local repository cache
rm -rf ~/.m2/repository/org/gnu/bash
mvn package
```

---

### "Could not find artifact org.gnu.bash:bashclient"

**Symptom:** Downstream project cannot resolve the bashclient dependency.

**Cause:** bashclient is not published to a remote Maven repository. It must
be installed locally first.

**Solution:**

```bash
# Install to local Maven repository
cd /path/to/bash/bash-server/clients/java
mvn install

# Verify it's in the local repo
ls ~/.m2/repository/org/gnu/bash/bashclient/0.1.0/
```

---

### OutOfMemoryError during build

**Symptom:**

```
java.lang.OutOfMemoryError: Java heap space
```

**Solution:**

```bash
export MAVEN_OPTS="-Xmx512m -Xms256m"
mvn package
```

---

## Dependency Issues

### junixsocket native library not found

**Symptom:**

```
java.lang.UnsatisfiedLinkError: Could not load native library junixsocket-native
org.newsclub.net.unix.NativeUnixSocket.<clinit>
```

**Cause:** The junixsocket native library for your platform is missing.

**Solution:**

Ensure both `junixsocket-common` and `junixsocket-native-common` are in
your dependencies:

```xml
<dependency>
    <groupId>com.kohlschutter.junixsocket</groupId>
    <artifactId>junixsocket-common</artifactId>
    <version>2.9.1</version>
</dependency>
<dependency>
    <groupId>com.kohlschutter.junixsocket</groupId>
    <artifactId>junixsocket-native-common</artifactId>
    <version>2.9.1</version>
</dependency>
```

The `junixsocket-native-common` artifact bundles native libraries for most
platforms. For exotic architectures, you may need to build from source:

```bash
git clone https://github.com/kohlschutter/junixsocket.git
cd junixsocket
mvn install -DskipTests
```

---

### Jackson version conflict

**Symptom:**

```
java.lang.NoSuchMethodError: com.fasterxml.jackson.databind.ObjectMapper.createObjectNode()
```

or

```
java.lang.IncompatibleClassChangeError
```

**Cause:** Another library in your project uses a different Jackson version.

**Solution:**

Use Maven's dependency management to enforce a single Jackson version:

```xml
<dependencyManagement>
    <dependencies>
        <dependency>
            <groupId>com.fasterxml.jackson</groupId>
            <artifactId>jackson-bom</artifactId>
            <version>2.17.0</version>
            <type>pom</type>
            <scope>import</scope>
        </dependency>
    </dependencies>
</dependencyManagement>
```

Check for conflicts:

```bash
mvn dependency:tree -Dincludes=com.fasterxml.jackson.core
```

---

### SLF4J warning: "No SLF4J providers were found"

**Symptom:**

```
SLF4J: No SLF4J providers were found.
SLF4J: Defaulting to no-operation (NOP) logger implementation
```

**Cause:** junixsocket uses SLF4J for logging, but no SLF4J backend is present.

**Solution:** This warning is harmless. To silence it, add an SLF4J backend:

```xml
<dependency>
    <groupId>org.slf4j</groupId>
    <artifactId>slf4j-simple</artifactId>
    <version>2.0.12</version>
</dependency>
```

Or for production, use Logback or Log4j2.

---

## Connection Issues

### "Connection refused" on connect()

**Symptom:**

```
java.io.IOException: Connection refused
```

**Cause:** No bash-server is listening at the specified socket path.

**Solution:**

1. Verify bash-server is running:

```bash
ps aux | grep bash-server
```

2. Verify the socket file exists:

```bash
ls -la /tmp/bash-server-$(id -u)/sock
```

3. Verify the socket path matches:

```bash
# Check what socket the server is using
cat ~/.config/bash-server/<name>/socket
```

4. Start bash-server if not running:

```bash
./bash-server/bash-server --name myapp --no-peercred
```

---

### "No such file or directory" for socket path

**Symptom:**

```
java.io.FileNotFoundException: /tmp/bash-server-1000/sock (No such file or directory)
```

**Cause:** The socket file does not exist. Either the server is not running or
the path is wrong.

**Solution:**

```bash
# Find the actual socket
find /tmp -name "sock" -user $(whoami) 2>/dev/null

# Check XDG_RUNTIME_DIR
ls $XDG_RUNTIME_DIR/bash-server/*/sock

# Check server config
cat ~/.config/bash-server/*/socket
```

---

### "Permission denied" on socket

**Symptom:**

```
java.io.IOException: Permission denied
```

**Cause:** The socket file has restrictive permissions and the Java process
is running as a different user.

**Solution:**

- Run the Java client as the same user who started bash-server
- Check socket permissions: `ls -la /path/to/sock`
- The socket should be mode `0600` (owner read/write only)

---

### Connection works but immediately closes

**Symptom:** `connect()` succeeds but the first operation throws
`TransportException` with "Connection reset".

**Cause:** The server may have a maximum client limit reached, or the
server process crashed after accepting.

**Solution:**

```bash
# Check server max clients
# Default is typically unlimited, but check --max-clients

# Check server logs for errors
# Look for crash dumps or error messages

# Restart the server
./bash-server/bash-server --name myapp --no-peercred
```

---

## Authentication Issues

### "Authentication failed" (AuthException)

**Symptom:**

```
org.gnu.bash.client.BashClientException$AuthException: Authentication failed
```

**Cause:** The token provided does not match the server's token.

**Solution:**

1. Read the token from the correct file:

```bash
cat ~/.config/bash-server/<name>/token
```

2. Ensure no trailing whitespace or newline:

```java
String token = Files.readString(tokenPath).trim();
```

3. If the server was restarted, it generates a new token -- re-read the file.

---

### Token file not found

**Symptom:**

```
java.nio.file.NoSuchFileException: /home/user/.config/bash-server/myapp/token
```

**Cause:** The server may not have created the config directory, or the server
name is different.

**Solution:**

```bash
# List all server configs
ls ~/.config/bash-server/

# Check if the server writes to a different location
# Some servers use $XDG_RUNTIME_DIR instead
ls $XDG_RUNTIME_DIR/bash-server/
```

---

### Auth works for first command but subsequent calls fail

**Symptom:** `auth()` succeeds, first `eval()` works, then later calls throw
`TransportException`.

**Cause:** The server may have an idle timeout or the connection was
interrupted by network issues.

**Solution:**

- Implement reconnection logic (see the reconnecting wrapper recipe in GUIDE.md)
- Check server configuration for idle timeout settings
- Use `ping()` periodically to keep the connection alive

---

## Timeout Issues

### TimeoutException on eval()

**Symptom:**

```
org.gnu.bash.client.BashClientException$TimeoutException:
    No response within PT30S
```

**Cause:** The command took longer than 30 seconds to execute.

**Solution:**

Use a custom timeout for long-running commands:

```java
EvalResult result = client.command.eval("find / -name '*.log'",
    Duration.ofMinutes(5));
```

Or use streaming callbacks so you see progress:

```java
EvalResult result = client.command.eval("make -j12",
    out -> System.out.print(out),
    err -> System.err.print(err));
```

---

### TimeoutException on auth()

**Symptom:** `auth()` throws `TimeoutException`.

**Cause:** The server is not responding. Possible reasons:
- Server is overloaded
- Server process is hung
- Network issue between client and server

**Solution:**

```bash
# Check if server is responsive
echo "PING" | socat - UNIX-CONNECT:/path/to/sock

# Check server process state
ps aux | grep bash-server

# Restart the server if hung
kill $(pgrep bash-server)
./bash-server/bash-server --name myapp --no-peercred
```

---

### Intermittent timeouts

**Symptom:** Commands occasionally time out but usually succeed.

**Cause:** Server load spikes, garbage collection pauses, or system resource
contention.

**Solution:**

- Increase the default timeout for affected operations
- Monitor server-side resource usage
- Consider using a dedicated bash-server instance per client
- Add retry logic for transient failures

---

## Thread Safety Issues

### ConcurrentModificationException in callbacks

**Symptom:**

```
java.util.ConcurrentModificationException
    at java.util.ArrayList$Itr.next
```

**Cause:** Modifying a shared collection inside a callback without
synchronization.

**Solution:** Use thread-safe collections:

```java
// Bad
List<String> results = new ArrayList<>();
client.observe.onPostCommand(e -> results.add(e.get("command").asText()));

// Good
List<String> results = new CopyOnWriteArrayList<>();
client.observe.onPostCommand(e -> results.add(e.get("command").asText()));
```

---

### Response received on wrong channel

**Symptom:** `eval()` returns data that looks like a state response, or vice
versa.

**Cause:** Multiple threads are making calls on the same channel simultaneously
without external synchronization.

**Solution:** Synchronize access to the same channel from multiple threads:

```java
private final Object evalLock = new Object();

public EvalResult safeEval(BashClient client, String cmd)
        throws BashClientException {
    synchronized (evalLock) {
        return client.eval(cmd);
    }
}
```

Or use different channels from different threads (this is safe):

```java
// Thread 1: uses command channel
pool.submit(() -> client.eval("echo hello"));

// Thread 2: uses state channel (different channel -- safe)
pool.submit(() -> client.state.getVar("PATH"));
```

---

### Deadlock with callback + channel call

**Symptom:** Application hangs when a callback tries to call a channel method.

**Cause:** Callbacks run on the reader thread. If a callback calls a channel
method that blocks waiting for a response, it blocks the reader thread, which
prevents the response from being routed.

**Solution:** Never make blocking channel calls inside callbacks. Offload to
another thread:

```java
ExecutorService worker = Executors.newSingleThreadExecutor();

// Bad: blocks reader thread
client.observe.onPostCommand(event -> {
    client.state.getVar("PATH");  // DEADLOCK: blocks reader thread
});

// Good: offload to worker thread
client.observe.onPostCommand(event -> {
    worker.submit(() -> {
        try {
            client.state.getVar("PATH");  // Runs on worker thread
        } catch (BashClientException e) {
            e.printStackTrace();
        }
    });
});
```

---

## Platform-Specific Issues

### Linux

#### "Protocol not supported" error

**Symptom:**

```
java.net.SocketException: Protocol not supported
```

**Cause:** The Linux kernel or Java runtime does not support Unix domain sockets.
This is very rare on modern systems.

**Solution:**

```bash
# Verify kernel support
python3 -c "import socket; s=socket.socket(socket.AF_UNIX); print('OK')"

# Check Java version
java -version
# Must be 11+
```

#### SELinux blocking socket access

**Symptom:** Connection refused or permission denied, but file permissions
look correct.

**Solution:**

```bash
# Check SELinux denials
ausearch -m avc -ts recent | grep bash-server

# Temporarily set permissive (for testing only)
sudo setenforce 0

# Create a proper SELinux policy for production
```

---

### macOS

#### "Operation not permitted" on socket

**Symptom:** macOS refuses socket operations.

**Cause:** macOS sandbox or App Sandbox restrictions.

**Solution:**

- Run outside of a sandboxed environment
- Add socket path to sandbox entitlements (for App Store apps)
- Use stdio transport as an alternative

#### SO_PEERCRED not available

**Symptom:** Server rejects connection with peer credential error.

**Solution:** Start bash-server with `--no-peercred`:

```bash
./bash-server/bash-server --name myapp --no-peercred
```

---

### Cygwin

#### Socket path translation issues

**Symptom:** Connection fails even though the server is running.

**Cause:** Java uses Windows paths, but the bash-server socket uses Cygwin
POSIX paths.

**Solution:**

Use the Cygwin path as-is. junixsocket may need the Windows equivalent:

```bash
# Convert Cygwin path to Windows path
cygpath -w /tmp/bash-server-1000/sock
# C:\cygwin\tmp\bash-server-1000\sock
```

```java
// Use the Windows path for junixsocket on Cygwin
String winPath = "C:\\cygwin\\tmp\\bash-server-1000\\sock";
BashClient client = BashClient.connect(winPath);
```

Or use the Named Pipe transport instead:

```java
BashClient client = BashClient.connectNamedPipe("bash-server");
```

#### Cygwin DLL issues

**Symptom:** bash-server fails to start or crashes.

**Cause:** Missing Cygwin DLLs or version mismatch.

**Solution:**

```bash
# Check bash-server dependencies
ldd ./bash-server/bash-server.exe

# Ensure Cygwin bin is in PATH
export PATH=/usr/bin:$PATH
```

---

### Windows Native

#### Named Pipe "Access Denied"

**Symptom:**

```
java.io.FileNotFoundException: \\.\pipe\bash-server (Access is denied)
```

**Cause:** The Named Pipe DACL restricts access to the creating user.

**Solution:**

- Run the Java client as the same Windows user who started bash-server
- Check pipe existence: `dir \\.\pipe\ | findstr bash`

#### Named Pipe "File Not Found"

**Symptom:**

```
java.io.FileNotFoundException: \\.\pipe\bash-server
    (The system cannot find the file specified)
```

**Cause:** The pipe does not exist. Server may not be running or uses a
different pipe name.

**Solution:**

```powershell
# List all named pipes
Get-ChildItem \\.\pipe\ | Where-Object Name -like "*bash*"

# Start server with named pipe
./bash-server.exe --named-pipe --name bash-server
```

---

## Protocol Issues

### ProtocolException: "Failed to parse NDJSON"

**Symptom:**

```
org.gnu.bash.client.BashClientException$ProtocolException:
    Failed to parse NDJSON: Unexpected character ...
```

**Cause:** The server sent data that is not valid NDJSON. Possible reasons:
- Protocol version mismatch (client expects NDJSON, server sends binary v2)
- Server is sending v1 text protocol
- Corrupted data on the wire

**Solution:**

- Ensure the server auto-detects the client protocol correctly
- Start the server with NDJSON explicitly if needed
- Check for proxy or middleware that modifies the data stream

---

### Unexpected message on wrong channel

**Symptom:** Parsing errors or unexpected data in channel responses.

**Cause:** The server version may have a different channel numbering or
message format.

**Solution:**

- Verify client and server versions are compatible
- Check the server's protocol documentation
- Enable debug logging to see raw messages

---

## Debugging Tips

### Enable Wire Logging

Add a wrapper around the transport to log all wire traffic:

```java
public class LoggingTransport implements Transport {
    private final Transport delegate;

    public LoggingTransport(Transport delegate) {
        this.delegate = delegate;
    }

    @Override
    public InputStream getInputStream() throws IOException {
        return new FilterInputStream(delegate.getInputStream()) {
            @Override
            public int read(byte[] b, int off, int len) throws IOException {
                int n = super.read(b, off, len);
                if (n > 0) {
                    System.err.println("RECV: " +
                        new String(b, off, n, StandardCharsets.UTF_8));
                }
                return n;
            }
        };
    }

    // Similar wrapping for getOutputStream()...
}
```

### JVM Debugging Flags

```bash
# Enable detailed exception stack traces
java -XX:+ShowCodeDetailsInExceptionMessages -cp ... MainClass

# Enable socket debugging
java -Djava.net.preferIPv4Stack=true \
     -Djavax.net.debug=all \
     -cp ... MainClass

# Enable GC logging (to rule out GC pauses causing timeouts)
java -Xlog:gc*=info -cp ... MainClass
```

### Thread Dump

If the application hangs, take a thread dump:

```bash
# Find the Java process
jps -l

# Take a thread dump
jstack <pid>

# Or send SIGQUIT (Unix)
kill -3 <pid>
```

Look for the `bashclient-reader` thread to see if it's blocked or waiting.

### Heap Dump

If you suspect a memory leak:

```bash
jmap -dump:format=b,file=heap.hprof <pid>
```

Analyze with Eclipse MAT or VisualVM.

---

## FAQ

### Q: Can I use bashclient with Java 8?

**A:** No. The minimum requirement is Java 11. The library uses Java 11 features
including `String.isBlank()`, `Files.readString()`, and the `var` keyword
in internal code.

### Q: Is bashclient thread-safe?

**A:** Yes, with caveats. Different channels can be used from different threads
safely. Same-channel concurrent access is technically safe but may interleave
responses. See [Thread Safety Issues](#thread-safety-issues) for details.

### Q: Can I connect to multiple servers from one JVM?

**A:** Yes. Each `BashClient` instance is independent. You can create multiple
instances connected to different servers.

### Q: Why is there no connectFd() method?

**A:** Java does not have a straightforward way to inherit file descriptors
from a parent process. The fd transport mode in bash-server is designed for
C/C++ clients that can use `dup2()`. Java clients should use Unix socket,
stdio, or Named Pipe transports instead.

### Q: Can I use bashclient in an Android application?

**A:** Not directly. Android does not support Unix domain sockets through
junixsocket, and bash-server is not available on Android. The stdio transport
could theoretically work if you bundled a bash-server binary, but this is
not a supported configuration.

### Q: How do I handle server restarts?

**A:** When the server restarts, existing connections are broken. The client
will throw `TransportException` on the next operation. Implement reconnection
logic using the reconnecting wrapper pattern from the Guide.

### Q: What happens if the server crashes mid-command?

**A:** The reader thread will detect the broken connection (EOF or IOException)
and mark the client as disconnected. The pending `eval()` call will throw
`TransportException`. Resources are cleaned up on the next `close()` call.

### Q: Can I set a global default timeout?

**A:** Not currently. The default timeout is 30 seconds. For individual
operations, pass a `Duration` to the channel method. A future version may
support configurable default timeouts.

### Q: How do I log all commands for auditing?

**A:** Use the observe channel to subscribe to pre/post command events. See
the AuditTrail recipe in [GUIDE.md](GUIDE.md#recipe-7-command-audit-trail).

### Q: Is the NDJSON protocol required, or can I use binary v2?

**A:** The Java client uses NDJSON exclusively. The binary v2 framing is more
complex to implement and NDJSON provides the same functionality with simpler
parsing. The bash-server auto-detects the protocol based on the first byte.

---

## Getting Help

If your issue is not covered here:

1. Check the [Guide](GUIDE.md) for usage patterns
2. Check the [API Reference](API.md) for method documentation
3. Check the [Architecture](ARCHITECTURE.md) for design details
4. Review the bash-server documentation in `doc/server/`
5. Open an issue with:
   - Java version (`java -version`)
   - Operating system and version
   - Maven version (`mvn --version`)
   - Full stack trace
   - Steps to reproduce
