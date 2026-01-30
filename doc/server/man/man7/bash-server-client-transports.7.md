# BASH-SERVER-CLIENT-TRANSPORTS(7) -- Transport modes from the client perspective

# DESCRIPTION

The bash-server client bindings support four transport modes for
connecting to a bash-server instance.  Each transport provides the same
line-oriented read/write interface to the protocol layer, but differs
in how the underlying connection is established, where it is available,
and what security properties it provides.

This page describes each transport mode from the client perspective,
including when to use it, how to connect in each language, platform
availability, and security considerations.

# CONCEPTS

# Transport abstraction

All four client bindings implement a `Transport` abstraction (interface,
abstract class, or protocol) that provides:

- `connect()` -- Establish the connection.
- `read_line()` / `readLine()` -- Read one NDJSON line.
- `write()` -- Write an NDJSON line.
- `close()` -- Tear down the connection.
- `is_open` / `isOpen` / `isOpen()` -- Check connection status.

The `BashClient` class accepts any transport and is agnostic to the
underlying connection mechanism.  This design allows switching
transports without changing application logic.

# Unix domain socket

**When to use:** Production deployments on Linux, macOS, and Cygwin.
This is the default and recommended transport.

**How it works:** The client connects to an `AF_UNIX` stream socket at
a filesystem path.  The server listens on this path after creating
the socket file with restricted permissions (mode 0700 directory,
0600 socket).

**Socket path discovery:**

The server determines the socket path using a resolution chain.
Clients should follow the same chain to find the server:

1. CLI `--socket` argument (explicit path)
2. `$BASH_SERVER_SOCKET` environment variable
3. `socket=` in `~/.bash-serverrc`
4. `$XDG_RUNTIME_DIR/bash-server/sock`
5. `/tmp/bash-server-<uid>/sock` (fallback)

Clients typically receive the socket path from the environment or a
configuration file.

**Connection examples:**

Python:
```python
client = await BashClient.connect("/tmp/bash-server-1000/sock")
```

TypeScript:
```typescript
const client = await BashClient.connect("/tmp/bash-server-1000/sock");
```

C:
```c
bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock");
```

Java:
```java
BashClient client = BashClient.connect("/tmp/bash-server-1000/sock");
```

**Security:** The Unix socket is protected by filesystem permissions.
Only the owning user (uid match) can connect.  The server optionally
uses `SO_PEERCRED` to verify the peer's uid matches the server's uid
(disable with `--no-peercred` for Python client compatibility on some
systems).  After connecting, clients must still authenticate with the
server token.

**Platform availability:**

| Platform | Available | Notes                              |
|----------|-----------|------------------------------------|
| Linux    | yes       | Full SO_PEERCRED support           |
| macOS    | yes       | SO_PEERCRED via LOCAL_PEERCRED     |
| Cygwin   | yes       | SO_PEERCRED emulated, may need --no-peercred |
| Windows  | no        | Use Named Pipe instead             |

# stdio transport

**When to use:** Testing, CI/CD pipelines, sandboxed environments
where socket creation is restricted, or when you want a self-contained
client-server pair with no external socket file.

**How it works:** The client launches `bash-server --stdio` as a child
process and communicates over the child's stdin (client writes) and
stdout (client reads).  The server runs in single-session mode with
no socket file.

The client manages the subprocess lifecycle: spawning at connect time,
and terminating (or waiting for exit) at close time.

**Connection examples:**

Python:
```python
client = await BashClient.connect_stdio("bash-server", "--stdio")

# With additional arguments
client = await BashClient.connect_stdio(
    "bash-server", "--stdio", "--login", "--init", "setup.sh"
)
```

TypeScript:
```typescript
const client = await BashClient.connectStdio("bash-server", "--stdio");

// With additional arguments
const client = await BashClient.connectStdio(
    "bash-server", "--stdio", "--login"
);
```

C:
```c
const char *argv[] = {"bash-server", "--stdio", NULL};
bc_client_t *c = bc_connect_stdio(argv);

/* With additional arguments */
const char *argv2[] = {"bash-server", "--stdio", "--login", NULL};
bc_client_t *c2 = bc_connect_stdio(argv2);
```

Java:
```java
BashClient client = BashClient.connectStdio("bash-server", "--stdio");

// With additional arguments
BashClient client = BashClient.connectStdio(
    "bash-server", "--stdio", "--login"
);
```

**Authentication in stdio mode:** The server prints the authentication
token to stderr when started in `--stdio` mode.  The client can capture
this from the subprocess stderr stream.  Alternatively, use `--token`
to specify a known token, or `--auth-fd` to deliver the token over a
separate file descriptor.

**Security:** The subprocess runs as the same user.  stdin/stdout are
private to the parent-child pair.  No network or filesystem exposure.
This is the most isolated transport mode.

**Platform availability:**

| Platform | Available | Notes                              |
|----------|-----------|------------------------------------|
| Linux    | yes       | Full support                       |
| macOS    | yes       | Full support                       |
| Cygwin   | yes       | Full support                       |
| Windows  | yes       | Via process spawn                  |

# File descriptor transport

**When to use:** Container and systemd integration, or any scenario
where a parent process pre-opens the connection and passes it to the
client process via file descriptor inheritance.

**How it works:** The client receives an already-connected file
descriptor (typically via `fork()`/`exec()` with the fd kept open, or
via systemd socket activation).  The client wraps this fd in a
transport and communicates directly.

This is useful in orchestration scenarios:

- A supervisor process connects to bash-server and passes the fd to
  a worker process.
- systemd opens the socket and passes it to the service via
  `$LISTEN_FDS`.
- A container runtime pre-opens the connection.

The server side uses `--fd N` to accept connections on an inherited
file descriptor rather than creating its own socket.

**Connection examples:**

Python:
```python
import os
fd = int(os.environ["BASH_SERVER_FD"])
client = await BashClient.connect_fd(fd)
```

TypeScript:
```typescript
const fd = parseInt(process.env.BASH_SERVER_FD!, 10);
const client = await BashClient.connectFd(fd);
```

C:
```c
int fd = atoi(getenv("BASH_SERVER_FD"));
bc_client_t *c = bc_connect_fd(fd);
```

Java:
```java
// Java does not have a built-in FdTransport factory method
// on BashClient. Use the FdTransport class directly:
FdTransport t = new FdTransport();
t.connect(Integer.parseInt(System.getenv("BASH_SERVER_FD")));
BashClient client = new BashClient(t);
```

**Security:** Security depends on how the fd was obtained.  The fd
itself provides no authentication -- the caller must still send an
`auth` message with the server token.  The fd is private to the
process that received it (not accessible to other processes unless
explicitly shared via `SCM_RIGHTS`).

**Platform availability:**

| Platform | Available | Notes                              |
|----------|-----------|------------------------------------|
| Linux    | yes       | Full support, systemd integration  |
| macOS    | yes       | Full support                       |
| Cygwin   | yes       | Full support                       |
| Windows  | limited   | Requires POSIX fd emulation        |

# Named Pipe transport

**When to use:** Windows and Cygwin environments where Unix domain
sockets are not available or not preferred.  Named Pipes are the
native IPC mechanism on Windows.

**How it works:** The client connects to a Windows Named Pipe at a
path like `\\.\pipe\bash-server-<name>`.  The server creates the pipe
with a DACL (Discretionary Access Control List) that restricts access
to the pipe owner.

Named Pipes on Windows provide built-in impersonation and access
control.  The Cygwin implementation maps between Cygwin and Windows
paths transparently.

The server uses `--named-pipe [NAME]` to create a Named Pipe instead
of (or in addition to) a Unix socket.  The default pipe name is
derived from the server name or the user's SID.

**Pipe name discovery:**

The pipe name follows the same resolution pattern as the socket path:

1. CLI `--named-pipe NAME` argument
2. Configuration file setting
3. Default: `\\.\pipe\bash-server-<username>`

**Connection examples:**

Python:
```python
client = await BashClient.connect_named_pipe(
    "\\\\.\\pipe\\bash-server-myname"
)
```

TypeScript:
```typescript
const client = await BashClient.connectNamedPipe(
    "\\\\.\\pipe\\bash-server-myname"
);
```

C:
```c
bc_client_t *c = bc_connect_named_pipe(
    "\\\\.\\pipe\\bash-server-myname"
);
```

Java:
```java
BashClient client = BashClient.connectNamedPipe(
    "\\\\.\\pipe\\bash-server-myname"
);
```

**Security:** Windows Named Pipes support DACLs for access control.
The server creates the pipe with a DACL that allows only the creating
user to connect.  The token file is placed at a Windows-accessible
path for the client to read.  Token authentication is still required
after connecting.

**Platform availability:**

| Platform | Available | Notes                              |
|----------|-----------|------------------------------------|
| Linux    | no        | Not applicable                     |
| macOS    | no        | Not applicable                     |
| Cygwin   | yes       | Maps to Windows Named Pipes        |
| Windows  | yes       | Native support                     |

# Transport selection guide

| Scenario                      | Recommended transport | Reason                          |
|-------------------------------|----------------------|---------------------------------|
| Production server             | Unix socket          | Best security, standard path    |
| Unit testing                  | stdio                | No external state, isolated     |
| CI/CD pipeline                | stdio                | Self-contained, no cleanup      |
| Container orchestration       | fd                   | Parent pre-opens connection     |
| systemd service               | fd                   | Socket activation               |
| Windows/Cygwin production     | Named Pipe           | Native Windows IPC              |
| Cross-platform development    | Unix socket + Named Pipe | Use both, detect platform   |
| Embedded/scripting            | stdio                | Simplest to set up              |

# Platform availability matrix

| Transport     | Linux | macOS | Cygwin | Windows |
|---------------|-------|-------|--------|---------|
| Unix socket   | yes   | yes   | yes    | no      |
| stdio         | yes   | yes   | yes    | yes     |
| fd            | yes   | yes   | yes    | limited |
| Named Pipe    | no    | no    | yes    | yes     |

# Security comparison

| Transport     | Access control       | Network exposure | Token delivery         |
|---------------|----------------------|------------------|------------------------|
| Unix socket   | Filesystem perms + SO_PEERCRED | none    | File in socket dir     |
| stdio         | Process isolation    | none             | stderr or --auth-fd    |
| fd            | Depends on provider  | none             | Passed with fd         |
| Named Pipe    | Windows DACL         | none             | Token file at known path|

All transports require token-based authentication after the connection
is established.  The transport-level access control is a defense-in-
depth measure that restricts who can connect, while token authentication
verifies the client's identity.

# Custom transports

All bindings support creating a `BashClient` from a pre-existing
transport object.  This allows implementing custom transports (e.g.,
over TCP, SSH tunnels, or message queues) by implementing the transport
interface:

Python:
```python
class MyTransport(Transport):
    async def connect(self, target): ...
    async def read_line(self) -> str: ...
    async def write(self, data: str): ...
    async def close(self): ...
    @property
    def is_open(self) -> bool: ...

transport = MyTransport()
await transport.connect("my-target")
client = await BashClient.from_transport(transport)
```

TypeScript:
```typescript
class MyTransport implements Transport {
    async connect(target: string): Promise<void> { ... }
    async readLine(): Promise<string> { ... }
    async write(data: string): Promise<void> { ... }
    async close(): Promise<void> { ... }
    get isOpen(): boolean { ... }
}

const transport = new MyTransport();
await transport.connect("my-target");
const client = BashClient.fromTransport(transport);
```

C:
```c
/* The C binding does not support custom transports.
   Use bc_connect_fd() with a pre-connected fd instead. */
```

Java:
```java
public class MyTransport implements Transport {
    public void connect(String target) throws IOException { ... }
    public String readLine() throws IOException { ... }
    public void write(String data) throws IOException { ... }
    public void close() throws IOException { ... }
    public boolean isOpen() { ... }
}

MyTransport t = new MyTransport();
t.connect("my-target");
BashClient client = new BashClient(t);
```

# Connection resilience

None of the bindings currently implement automatic reconnection.  If
the transport connection is lost:

- The reader loop/thread terminates.
- Pending operations fail with `TransportError`.
- The `is_connected` / `isConnected` property returns `false`.

Applications that need reconnection should implement it at the
application level:

```python
async def resilient_eval(socket_path, token, command):
    for attempt in range(3):
        try:
            async with BashClient.connect(socket_path) as client:
                await client.auth(token)
                return await client.eval(command)
        except TransportError:
            if attempt == 2:
                raise
            await asyncio.sleep(1)
```

# Token file locations

When using Unix socket or Named Pipe transports, the server writes
the authentication token to a file that the client can read:

| Transport   | Token file location                            |
|-------------|------------------------------------------------|
| Unix socket | `<socket_dir>/token` (same directory as socket)|
| Named Pipe  | Via `server_winpipe_token_path()` resolution   |
| stdio       | Printed to stderr, or via `--auth-fd`          |
| fd          | Passed alongside the fd by the parent process  |

Typical client token reading:

```python
import os

socket_path = "/tmp/bash-server-1000/sock"
token_path = os.path.join(os.path.dirname(socket_path), "token")
with open(token_path) as f:
    token = f.read().strip()

async with BashClient.connect(socket_path) as client:
    await client.auth(token)
```

# SEE ALSO

**bash-server-transports**(7),
**bash-server-client-api**(7),
**bash-server-client-channels**(7),
**bash-server**(1),
**bash-server-auth**(7),
**server_socket_create**(3),
**server_winpipe_create**(3)

# AUTHORS

GNU Bash is Copyright (C) Free Software Foundation, Inc.
The bash-server extension was developed as part of the Cygwin Bash project.

# COPYRIGHT

This is free software; see the GNU General Public License v3 or later
for copying conditions.  There is NO warranty.
