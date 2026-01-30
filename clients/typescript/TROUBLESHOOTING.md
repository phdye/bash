# Troubleshooting

## bashclient TypeScript/Node.js Troubleshooting Guide

Solutions for common issues when installing, building, connecting, and using
the `bashclient` package.

---

## Table of Contents

1. [Installation Issues](#installation-issues)
2. [Node.js Version Issues](#nodejs-version-issues)
3. [TypeScript Compilation](#typescript-compilation)
4. [Connection Failures](#connection-failures)
5. [Authentication Errors](#authentication-errors)
6. [Timeout Issues](#timeout-issues)
7. [Protocol Errors](#protocol-errors)
8. [Transport-Specific Issues](#transport-specific-issues)
9. [Platform-Specific Issues](#platform-specific-issues)
10. [Debugging Tips](#debugging-tips)
11. [FAQ](#faq)

---

## Installation Issues

### "Cannot find module 'bashclient'"

**Symptom**: `Error: Cannot find module 'bashclient'` at runtime.

**Causes and solutions**:

1. **Package not installed**: Run `npm install bashclient` in your project.

2. **Wrong directory**: Ensure you are running from the project root where
   `node_modules/` exists.

3. **Global vs local**: If installed globally (`npm install -g`), ensure your
   Node.js can resolve global packages. Prefer local installation.

4. **Verify installation**:
   ```bash
   ls node_modules/bashclient/
   node -e "console.log(require.resolve('bashclient'))"
   ```

### "npm ERR! code ERESOLVE" (Dependency Conflict)

**Symptom**: npm refuses to install due to peer dependency conflicts.

**Solution**: `bashclient` has zero external dependencies. This error likely
comes from other packages in your project. Try:

```bash
npm install bashclient --legacy-peer-deps
```

### Permission Errors During Install

**Symptom**: `EACCES: permission denied` during `npm install -g`.

**Solutions**:

1. Use sudo (not recommended):
   ```bash
   sudo npm install -g bashclient
   ```

2. Configure npm prefix (recommended):
   ```bash
   mkdir -p ~/.npm-global
   npm config set prefix '~/.npm-global'
   echo 'export PATH=~/.npm-global/bin:$PATH' >> ~/.bashrc
   source ~/.bashrc
   npm install -g bashclient
   ```

3. Use nvm (best):
   ```bash
   nvm install 20
   npm install -g bashclient  # No sudo needed with nvm
   ```

### Build from Source Fails

**Symptom**: `npm run build` fails with TypeScript errors.

**Solutions**:

1. Ensure TypeScript 4.7+ is installed:
   ```bash
   npx tsc --version
   ```

2. Clean and rebuild:
   ```bash
   npm run clean
   rm -rf node_modules/
   npm install
   npm run build
   ```

3. Check Node.js types are installed:
   ```bash
   ls node_modules/@types/node/
   ```

---

## Node.js Version Issues

### "SyntaxError: Unexpected token '?.'"

**Symptom**: Syntax error on optional chaining (`?.`) or nullish coalescing (`??`).

**Cause**: Node.js version is below 14 (optional chaining) or below 16
(required minimum).

**Solution**: Upgrade Node.js to 16 or later:

```bash
node --version  # Check current version
nvm install 20  # Install Node.js 20 LTS
nvm use 20
```

### "ReferenceError: AbortController is not defined"

**Symptom**: Error when using AbortController patterns.

**Cause**: `AbortController` requires Node.js 15+. Full support in 16+.

**Solution**: Upgrade to Node.js 16+.

### "TypeError: fs.createReadStream is not a function"

**Symptom**: Error when using fd transport.

**Cause**: Incorrect Node.js import or very old Node.js version.

**Solution**: Verify Node.js 16+ and correct import usage.

---

## TypeScript Compilation

### "Cannot find type definition file for 'node'"

**Symptom**: TypeScript errors about missing Node.js types.

**Solution**: Install Node.js type definitions:

```bash
npm install --save-dev @types/node
```

Ensure `tsconfig.json` includes:

```json
{
  "compilerOptions": {
    "types": ["node"]
  }
}
```

### "Module 'bashclient' has no exported member 'X'"

**Symptom**: TypeScript cannot find a specific export.

**Causes**:

1. **Typo in import**: Check spelling. Common mistakes:
   - `EvalResult` not `ExecResult`
   - `CHAN_CONTROL` not `CH_CONTROL`
   - `TimeoutError` not `TimedOutError`

2. **Version mismatch**: Update bashclient:
   ```bash
   npm update bashclient
   ```

3. **Stale type cache**: Delete `node_modules` and reinstall:
   ```bash
   rm -rf node_modules/ package-lock.json
   npm install
   ```

### "Type 'unknown' is not assignable to type 'X'"

**Symptom**: TypeScript strict mode rejects message field access.

**Cause**: The `Message` interface uses `[key: string]: unknown` for
extra fields.

**Solution**: Use type assertions or the typed channel methods:

```typescript
// Instead of accessing raw message fields:
const msg: Message = await client.send({ ch: 1, type: "eval", command: "ls" });
const stdout = msg.stdout as string;  // Type assertion needed

// Prefer typed channel methods:
const result = await client.command.eval("ls");
console.log(result.stdout);  // Properly typed as string
```

### "Property 'X' does not exist on type 'Y'"

**Symptom**: TypeScript error when accessing fields on event objects.

**Solution**: Ensure you are using the correct event type:

```typescript
// Correct types for each event
client.observe.on("pre_command", (e: PreCommandEvent) => {
  console.log(e.command);     // OK
  console.log(e.cwd);         // OK
  // console.log(e.exit_status);  // Error: not on PreCommandEvent
});

client.observe.on("post_command", (e: PostCommandEvent) => {
  console.log(e.exit_status);  // OK
  console.log(e.duration_ms);  // OK
  // console.log(e.cwd);       // Error: not on PostCommandEvent
});
```

---

## Connection Failures

### "ECONNREFUSED: Connection refused"

**Symptom**: `TransportError: connect ECONNREFUSED`

**Causes**:

1. **bash-server not running**: Start the server:
   ```bash
   bash-server &
   # or
   bash-server --name myserver &
   ```

2. **Wrong socket path**: Check where the server is listening:
   ```bash
   # Check server process
   ps aux | grep bash-server

   # Check socket file exists
   ls -la /tmp/bash-server-$(id -u)/sock
   ```

3. **Server crashed**: Check server logs and restart.

### "ENOENT: No such file or directory"

**Symptom**: `TransportError: connect ENOENT /path/to/socket`

**Causes**:

1. **Socket file does not exist**: The server is not running or uses a
   different socket path.

2. **Wrong path**: Verify the socket path:
   ```typescript
   // Explicit path
   const client = await BashClient.connect({
     socketPath: "/tmp/bash-server-1000/sock",
   });
   ```

3. **Permission issue**: Check socket file permissions:
   ```bash
   ls -la /tmp/bash-server-$(id -u)/
   ```

### "EACCES: Permission denied"

**Symptom**: Cannot connect to the socket.

**Cause**: Socket owned by a different user.

**Solution**: bash-server creates sockets with restricted permissions.
Ensure you are connecting as the same user that started the server.

### Connection Hangs (No Response)

**Symptom**: `BashClient.connect()` never resolves or rejects.

**Solutions**:

1. **Set a timeout**:
   ```typescript
   const client = await BashClient.connect({ timeout: 5000 });
   ```

2. **Check server health**: The server may be stuck. Try:
   ```bash
   # Send a manual ping
   echo '{"ch":0,"type":"ping"}' | nc -U /tmp/bash-server-$(id -u)/sock
   ```

3. **Restart the server**.

---

## Authentication Errors

### "Authentication failed: invalid token"

**Symptom**: `AuthError: Authentication failed: invalid token`

**Causes**:

1. **Stale token**: The server generates a new token on each start.
   If you cached a token, it may be outdated.

2. **Wrong token file**: Token auto-discovery reads from the file next to
   the socket. If the server was restarted, the token file should be updated.

3. **Manual token mismatch**: If you specified a token explicitly, verify it
   matches the server's token.

**Solutions**:

```typescript
// Let auto-discovery handle the token
const client = await BashClient.connect();  // Reads token from file

// Or read the token file manually
import * as fs from "fs";
const token = fs.readFileSync("/tmp/bash-server-1000/sock.token", "utf-8").trim();
const client2 = await BashClient.connect({ token });
```

### "Authentication failed: token required"

**Symptom**: Server requires a token but none was provided.

**Solution**: Ensure the token file exists next to the socket, or provide
the token explicitly.

### stdio Transport Auth Issues

**Symptom**: Auth fails with stdio transport.

**Note**: stdio transport should handle auth automatically. The server sends
its token on startup, and the client reads it. If this fails:

1. Check that `bash-server --stdio` starts correctly.
2. Check server stderr for error messages.
3. Try with explicit token from server output.

---

## Timeout Issues

### Command Evaluation Timeouts

**Symptom**: `TimeoutError: Timed out after Xms`

**Causes**:

1. **Command runs too long**: Increase the timeout:
   ```typescript
   const result = await client.command.eval("find / -name '*.log'", {
     timeout: 60000,  // 60 seconds
   });
   ```

2. **Command is blocking**: The command may be waiting for input. Ensure
   commands do not read from stdin unless you use PTY channel.

3. **Server is overloaded**: Reduce concurrent operations.

### Connection Timeouts

**Symptom**: Timeout during `BashClient.connect()`.

**Solutions**:

1. Increase connection timeout:
   ```typescript
   const client = await BashClient.connect({ timeout: 15000 });
   ```

2. Verify server is running and accepting connections.

3. Check network/socket accessibility.

### Choosing Timeout Values

| Operation         | Recommended Timeout | Notes                        |
|-------------------|---------------------|------------------------------|
| Connection        | 5000-10000 ms       | Includes auth handshake      |
| Simple commands   | 5000-10000 ms       | echo, pwd, whoami            |
| File operations   | 30000 ms            | find, grep over large trees  |
| Long-running      | 60000-300000 ms     | Builds, batch processing     |
| Ping              | 2000 ms             | Network health check         |

---

## Protocol Errors

### "Malformed JSON in response"

**Symptom**: `ProtocolError: Unexpected token ... in JSON`

**Causes**:

1. **Server bug**: The server sent invalid JSON. Report this as a bug.

2. **Binary data in response**: A command produced binary output that
   corrupted the JSON stream. Avoid commands that output raw binary.

3. **Version mismatch**: Client and server protocol versions differ.

### "Unexpected message type"

**Symptom**: `ProtocolError: Unexpected message type: X`

**Cause**: Server sent a message type the client does not recognize.

**Solution**: Update `bashclient` to the latest version.

### "Missing ch or type field"

**Symptom**: `ProtocolError: Invalid message: missing ch or type`

**Cause**: Server sent a JSON object without the required fields.

**Solution**: This indicates a server-side issue. Check server version
and logs.

---

## Transport-Specific Issues

### Unix Socket

**Socket file left behind after server crash**:

```bash
# Remove stale socket
rm /tmp/bash-server-$(id -u)/sock

# Restart server
bash-server &
```

**Too many open connections**:

Check `--max-clients` setting on the server. Default is typically sufficient.

### stdio Transport

**Server process fails to start**:

```typescript
try {
  const client = await BashClient.connectStdio({
    serverPath: "/usr/local/bin/bash-server",
  });
} catch (e) {
  if (e instanceof TransportError) {
    console.error("Failed to spawn server:", e.message);
    // Check: Is bash-server in PATH?
    // Check: Is bash-server executable?
    // Check: Does it have correct permissions?
  }
}
```

**Server exits immediately**:

Check server stderr for initialization errors:

```bash
bash-server --stdio 2>/tmp/server-stderr.log
cat /tmp/server-stderr.log
```

### Named Pipe Transport

**"Named Pipe not found"**:

1. Ensure bash-server was started with `--named-pipe`:
   ```bash
   bash-server --named-pipe myapp
   ```

2. Verify pipe exists (Cygwin):
   ```bash
   ls /tmp/.bash-server-pipe-*
   ```

**Named Pipe only works on Cygwin/Windows**: This transport is not available
on Linux or macOS.

---

## Platform-Specific Issues

### Linux

**"EMFILE: too many open files"**:

Increase the file descriptor limit:

```bash
ulimit -n 65536
```

### macOS

**Socket path too long**:

macOS limits Unix socket paths to 104 bytes. Use shorter paths:

```typescript
const client = await BashClient.connect({
  socketPath: "/tmp/bs/sock",  // Keep it short
});
```

### Windows (Cygwin)

**"bash-server: command not found"**:

Ensure bash-server is built and in the Cygwin PATH:

```bash
which bash-server
# If not found, add to PATH or use full path
export PATH=/usr/local/bin:$PATH
```

**Line ending issues**:

If scripts have CRLF line endings, bash-server may reject them:

```bash
sed -i 's/\r$//' script.sh
```

**Path translation**:

Cygwin and Windows paths differ. Use Cygwin paths when working in Cygwin:

```typescript
// Cygwin path (correct in Cygwin environment)
const client = await BashClient.connect({
  socketPath: "/tmp/bash-server-1000/sock",
});

// Windows path (wrong in Cygwin environment)
// socketPath: "C:\\cygwin\\tmp\\bash-server-1000\\sock"  // DON'T
```

---

## Debugging Tips

### Enable Verbose Logging

Create a wrapper to log all messages:

```typescript
import { BashClient, Message } from "bashclient";

async function debugConnect() {
  const client = await BashClient.connect();

  // Wrap send to log outgoing messages
  const originalSend = client.send.bind(client);
  client.send = async (msg: Message, timeout?: number) => {
    console.log("[SEND]", JSON.stringify(msg));
    const response = await originalSend(msg, timeout);
    console.log("[RECV]", JSON.stringify(response));
    return response;
  };

  return client;
}
```

### Check Server Logs

bash-server may log diagnostic information. Check:

```bash
# If server was started with logging
journalctl -u bash-server  # systemd
tail -f /var/log/bash-server.log  # if configured
```

### Test with Raw NDJSON

Test server connectivity directly:

```bash
# Connect to Unix socket and send a ping
(echo '{"ch":0,"type":"auth","token":"YOUR_TOKEN"}'; \
 echo '{"ch":0,"type":"ping"}') | \
  nc -U /tmp/bash-server-$(id -u)/sock
```

### Inspect Socket

```bash
# Check if socket exists and is a socket
file /tmp/bash-server-$(id -u)/sock
stat /tmp/bash-server-$(id -u)/sock

# Check permissions
ls -la /tmp/bash-server-$(id -u)/

# Check what process owns it
lsof /tmp/bash-server-$(id -u)/sock
```

### Node.js Inspector

Debug your TypeScript code with the Node.js inspector:

```bash
node --inspect-brk dist/your-script.js
```

Then connect Chrome DevTools to `chrome://inspect`.

---

## FAQ

### Q: Does bashclient work without TypeScript?

**A**: Yes. Install the package and use CommonJS require:

```javascript
const { BashClient } = require("bashclient");
```

The `dist/` directory contains compiled JavaScript with TypeScript declaration
files. TypeScript is only needed for building from source.

### Q: Can I use bashclient in the browser?

**A**: No. `bashclient` depends on Node.js built-in modules (`net`, `fs`,
`child_process`) that are not available in browser environments. It is
designed for server-side Node.js applications.

### Q: How do I connect to a remote bash-server?

**A**: bash-server uses Unix domain sockets, which are local-only. For
remote access, use SSH port forwarding:

```bash
# Forward remote socket to local
ssh -L /tmp/remote-bash:/tmp/bash-server-1000/sock user@remote

# Connect to the forwarded socket
const client = await BashClient.connect({
  socketPath: "/tmp/remote-bash",
});
```

### Q: Can I have multiple connections to the same server?

**A**: Yes. Each `BashClient.connect()` call creates an independent session
with its own shell state. Multiple clients can connect to the same server
concurrently (up to `--max-clients` limit).

### Q: How do I handle server restarts?

**A**: The existing connection will fail with `TransportError`. Create a
new connection:

```typescript
try {
  await client.command.eval("echo test");
} catch (e) {
  if (e instanceof TransportError) {
    // Server restarted - reconnect
    client = await BashClient.connect();
  }
}
```

See the "Reconnection" recipe in [GUIDE.md](GUIDE.md#recipes).

### Q: Is bashclient thread-safe?

**A**: Node.js is single-threaded, so thread safety is not a concern.
However, avoid overlapping operations on the same channel (e.g., two
concurrent `eval` calls). Use `await` to serialize channel operations.
Different channels can be used concurrently.

### Q: What is the maximum command size?

**A**: The protocol supports messages up to `FRAME_MAX_PAYLOAD` (10 MiB).
In practice, commands should be much smaller. For large scripts, write them
to a file first and source them.

### Q: How do I pass binary data through bash-server?

**A**: bash-server uses JSON (text) for communication. Binary data should
be base64-encoded. Use the PTY channel for raw terminal I/O.
