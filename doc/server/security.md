# bash-server Security Model

**Audience:** Security engineers, penetration testers, and deployment architects.

## Threat Model

### Assets

| Asset | Sensitivity | Description |
|-------|------------|-------------|
| Bash interpreter | High | Arbitrary command execution capability |
| Authentication token | High | Grants full command execution access |
| Socket file | Medium | Communication endpoint |
| Token file | High | Contains authentication credential |
| Named Pipe | Medium | Communication endpoint (Cygwin) |
| Server process | High | Runs with user privileges |

### Trust Boundaries

```
┌──────────────────────────────────────────────────────────┐
│                     Operating System                      │
│                                                          │
│  ┌─────────────────────────┐  ┌────────────────────────┐ │
│  │   Owner UID boundary    │  │  Other UIDs            │ │
│  │                         │  │                        │ │
│  │  bash-server process    │  │  No access to:         │ │
│  │  Token file (0600)      │  │   - Socket (0600)      │ │
│  │  Socket file (0600)     │  │   - Token file (0600)  │ │
│  │  Named Pipe (owner DACL)│  │   - Named Pipe (DACL)  │ │
│  │  bashclient process     │  │   - PID file           │ │
│  │                         │  │                        │ │
│  └─────────────────────────┘  └────────────────────────┘ │
│                                                          │
│  ┌──────────────────────────────────────────────────────┐ │
│  │                    root / SYSTEM                      │ │
│  │   Full access to all files and processes              │ │
│  └──────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────┘
```

### Threat Actors

| Actor | Capability | Mitigations |
|-------|-----------|-------------|
| **Same-user process** | Can read token file, connect to socket/pipe | Intended access model |
| **Different-user process** | Cannot read socket or token (0600 / DACL) | Unix file permissions, DACL |
| **Root/SYSTEM** | Full access to all resources | Out of scope (trusted) |
| **Network attacker** | No direct access (Unix socket / Named Pipe are local) | No TCP exposure |
| **Physical attacker** | Disk access | Not mitigated (standard OS assumption) |

## Authentication Design

### Token Properties

| Property | Value | Rationale |
|----------|-------|-----------|
| Entropy | 256 bits (32 bytes) | Exceeds brute-force threshold |
| Encoding | 64 lowercase hexadecimal characters | Safe for protocol transmission |
| Source | `/dev/urandom` | Cryptographically secure PRNG |
| Lifetime | Server process lifetime | Rotated on restart |
| Storage | File with mode `0600` | Owner-only access |
| Comparison | Constant-time (`protocol_secure_compare`) | Prevents timing attacks |

### Authentication Flow (v1 -- Text Protocol)

```
1. Server starts -> reads 32 bytes from /dev/urandom
2. Hex-encodes to 64-character string
3. Writes to <socket>.token with mode 0600 (O_CREAT|O_EXCL)
4. Zeros raw bytes on stack (memset)

5. Client reads <socket>.token
6. Client sends: AUTH <token>\n
7. Server compares with constant-time algorithm
8. If match: session authenticated, bash initialized
9. If no match: ERR returned, session remains unauthenticated
```

### Authentication Flow (v2 -- JSON Protocol)

```
1. Server starts -> generates token (same as v1)
2. Token stored to file (socket/named-pipe) or delivered via --auth-fd (stdio/fd)

3. Client sends JSON auth message on CHAN_CONTROL (channel 0):
     {"ch":0,"type":"auth","token":"<64-hex-chars>"}
4. Server compares with constant-time algorithm
5. If match: session authenticated, auth_ok response with capabilities:
     {"ch":0,"type":"auth_ok","capabilities":["state","command","observe","debug"]}
6. If no match: error response:
     {"ch":0,"type":"error","message":"invalid token"}
```

The v2 authentication uses the same token, same constant-time comparison,
and same initialization behavior as v1.  The only difference is the wire
encoding (JSON vs. text line).

### Auth-fd Token Delivery

In `--stdio` and `--fd` transport modes, no socket file exists, so the token
cannot be written to `<socket>.token`.  Instead, the token is written to
a file descriptor specified by `--auth-fd` (default: stderr):

```
TOKEN <64-hex-chars>\n
```

**Security properties of auth-fd delivery:**
- The fd is inherited from the parent process (not opened by name)
- The token never touches the filesystem in stdio/fd modes
- The parent process controls where the fd points (pipe, file, /dev/null)
- No other process can read the token unless it has access to the same fd

**Risk:** If `--auth-fd` is not specified and stderr is a terminal, the token
is printed to the terminal.  This is acceptable for development but not
for production deployments.  Always use `--auth-fd` with a pipe or file in
production.

### Constant-Time Comparison

```c
int protocol_secure_compare(const char *a, const char *b)
{
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    unsigned char result = 0;

    if (len_a != len_b)
        return 0;  // Length mismatch (leaks length, acceptable)

    for (size_t i = 0; i < len_a; i++)
        result |= (unsigned char)a[i] ^ (unsigned char)b[i];

    return result == 0;
}
```

**Properties:**
- XOR accumulation prevents early-exit timing leaks.
- Length comparison is not constant-time but lengths are fixed (64 chars).
- Total comparison time is proportional to token length, not match position.

**Known limitation:**  The `strlen()` calls reveal string length via timing.
Since the token is always 64 characters and the protocol line is parsed
before comparison, this does not provide usable information to an attacker.

## Access Control

### File System Permissions

| Resource | Mode | Owner | Purpose |
|----------|------|-------|---------|
| Socket directory | `0700` | Server UID | Restrict directory listing |
| Socket file | `0600` | Server UID | Restrict connection |
| Token file | `0600` | Server UID | Restrict token reading |
| PID file | umask | Server UID | Process management |

### Socket Security

The Unix domain socket is the primary communication channel:

- **No TCP exposure:**  `AF_UNIX` with `SOCK_STREAM` is local-only.
- **File permission enforcement:**  Mode `0600` prevents other users
  from connecting.
- **Stale socket handling:**  `unlink()` before `bind()` removes stale
  socket files from previous runs.

### Named Pipe Security (Cygwin)

Windows Named Pipes provide an alternative transport on Cygwin that bypasses
the `AF_UNIX`-over-TCP-loopback emulation.

**DACL (Discretionary Access Control List):**

The pipe is created with an owner-only security descriptor using the SDDL
string `D:(A;;GA;;;OW)`:

| Component | Meaning |
|-----------|---------|
| `D:` | DACL follows |
| `A` | Access Allowed ACE |
| `GA` | Generic All (read + write + execute) |
| `OW` | Owner SID |

This is the Windows equivalent of `chmod 0600` -- only the owner's SID can
open the pipe.  Other users (including Administrators, unless they take
ownership) cannot connect.

**Security properties:**
- No `SO_PEERCRED` handshake needed (no race condition)
- Pipe name is always prefixed with `bash-server-` (e.g., `\\.\pipe\bash-server-myname`)
- The pipe namespace (`\\.\pipe\`) is system-global; any process can enumerate pipe names (but not connect without DACL permission)
- Token file for named pipe mode is stored at `$XDG_RUNTIME_DIR/bash-server/<name>.token` or `/tmp/bash-server-<uid>/<name>.token` (mode `0600`)

**Risk:** The pipe name is visible to all processes on the system.  An attacker
who knows the pipe name and has the token could connect if they also satisfy
the DACL check.  Since the DACL restricts to the owner, this requires same-user
access (same as the Unix socket model).

### Cygwin-Specific: SO_PEERCRED

On Cygwin, `AF_UNIX` sockets use TCP loopback internally.  Cygwin adds
a credential handshake (secret + `ucred` exchange) during connect/accept.

**Default mode (peercred enabled):**
- Cygwin verifies the connecting process is on the same machine.
- `getpeereid()` returns the peer's UID/GID.
- Not used by bash-server (token auth is preferred).

**`--no-peercred` mode:**
- Disables the credential handshake via `setsockopt(SO_PEERCRED, NULL, 0)`.
- Required for Python clients due to non-blocking connect race condition.
- `getpeereid()` no longer returns valid data.
- **Security impact:**  Minimal.  The credential handshake only verified
  same-machine origin (already guaranteed by Unix socket) and provided
  peer UID (unused by bash-server).  Token authentication remains the
  primary access control.

**Recommendation:** Use `--named-pipe` instead of `--no-peercred` when
possible.  Named Pipes avoid the `SO_PEERCRED` issue entirely and provide
native Windows security via DACLs.

## Multi-Transport Threat Considerations

Each transport mode has different security characteristics:

| Transport | Endpoint security | Token delivery | Fork isolation | Concurrency |
|-----------|------------------|---------------|----------------|-------------|
| Socket | File permissions (0600) | Token file (0600) | Fork-per-session | Multiple |
| Stdio | Parent-controlled fd | Auth-fd (no file) | Single session | Single |
| Fd | Parent-controlled fd | Auth-fd (no file) | Single session | Single |
| Named Pipe | DACL (owner-only) | Token file (0600) | Sequential (same process) | Sequential |

**Key differences:**

- **Stdio/fd modes** are the most secure for token delivery because the token
  never touches the filesystem.  The parent process has full control over the
  communication channel.

- **Socket mode** provides the best isolation (fork-per-session) but requires
  a filesystem-visible token file.

- **Named pipe mode** provides the most robust Cygwin security (native DACL,
  no `SO_PEERCRED` issues) but does not fork -- a malicious client that hangs
  the session blocks all subsequent clients.

## Execution Isolation

### Fork-per-Command Model

Each `EVAL` command runs in a forked child process:

```
Server Process                 Child Process
─────────────                 ─────────────
fork() ────────────────────► dup2(pipes)
                               parse_and_execute(cmd)
                               _exit(exit_code)
waitpid(child) ◄──────────── (exit)
```

**Isolation properties:**
- A crashing command (SIGSEGV) does not crash the server.
- Resource leaks (file descriptors, memory) are cleaned up by process exit.
- A `fork bomb` in a command does not directly consume server resources
  (though system-wide resource limits apply).
- Each command starts with a clean copy of the server's environment.

**Non-isolation properties:**
- Commands run with the server's UID, GID, and supplementary groups.
- Commands inherit the server's environment variables.
- Commands can modify the filesystem (limited by UID permissions).
- Commands can send signals to the server process (same UID).
- No chroot, namespace, seccomp, or cgroup isolation.

### Command Execution Flags

Commands are executed with `parse_and_execute()` using:

| Flag | Effect |
|------|--------|
| `SEVAL_NONINT` | Non-interactive evaluation (no prompt, no job control) |
| `SEVAL_NOHIST` | Do not record in history |

## Output Handling

### Size Limits

| Limit | Value | Behavior when exceeded |
|-------|-------|----------------------|
| Command length | 65,536 bytes | Protocol line truncated |
| stdout capture | 1 MB | Silently truncated |
| stderr capture | 1 MB | Silently truncated |
| Protocol line | 8,192 bytes | Line truncated |
| v2 frame payload | 1 MB | Frame rejected |

### Base64 Encoding

Output is base64-encoded before transmission.  This prevents:
- Protocol injection via newline characters in output.
- Binary data corruption.
- Encoding ambiguity.

**Overhead:**  Base64 increases payload size by approximately 33%.
Combined with the 1 MB capture limit, the maximum base64-encoded
STDOUT or STDERR line is approximately 1.37 MB.

## Denial of Service Considerations

| Attack Vector | Impact | Mitigation |
|---------------|--------|------------|
| Long-running command | Blocks session (fork-per-session isolates others) | External timeout (client-side) |
| Many connections | Fork exhaustion | `max_clients` limit (default 10) |
| Large output | Memory consumption (up to 2 MB) | `SERVER_MAX_OUTPUT` cap |
| fork bomb via EVAL | System resource exhaustion | OS-level ulimits (`nproc`) |
| Socket exhaustion | File descriptor starvation | OS-level ulimits (`nofile`) |
| Repeated auth failures | CPU time (negligible) | No rate limiting implemented |
| Named pipe hang | Blocks all subsequent clients | No timeout (use socket mode for multi-client) |
| PTY session hang | Blocks session | PTY close with SIGKILL after 5s timeout |
| Debug breakpoint hang | Blocks EVAL execution | Client disconnect releases the block |

**Recommendations for production deployment:**
- Run under a process manager with restart capabilities.
- Set `ulimit -u` (nproc) to limit fork bombs.
- Set `ulimit -n` (nofile) appropriately.
- Consider running in a container or namespace for isolation.
- Implement client-side timeouts for EVAL commands.
- Use socket mode (not named pipe) for multi-client deployments.

## Sensitive Data Handling

### Token Lifecycle

| Phase | Token State | Protection |
|-------|-------------|------------|
| Generation | 32 bytes on stack | Zeroed after hex encoding |
| Hex string | Heap-allocated (server lifetime) | Process memory protection |
| File storage | `<socket>.token` (0600) | File permissions |
| Auth-fd delivery | Written to inherited fd | Parent-controlled channel |
| Transmission | Plaintext over Unix socket/pipe | Socket permissions / DACL |
| Comparison | Constant-time XOR | Timing attack resistance |
| Shutdown | File unlinked | No persistent storage |

### Memory Considerations

- The raw 32-byte entropy is zeroed on the stack after hex encoding.
- The hex token string remains in process memory for the server's lifetime
  (needed for AUTH comparisons).
- On process exit, the OS reclaims all memory.
- No swap protection (`mlock`) is implemented.

## Security Checklist

### Deployment

- [ ] Server runs as a non-root, dedicated user account
- [ ] Socket directory has mode `0700`
- [ ] Socket file has mode `0600`
- [ ] Token file has mode `0600`
- [ ] `ulimit -u` limits maximum processes
- [ ] `ulimit -n` limits open file descriptors
- [ ] Process manager configured for restart-on-failure
- [ ] Client-side timeouts implemented for EVAL commands

### Transport-Specific

- [ ] **Socket mode:** Token file is on a local filesystem (not network-mounted)
- [ ] **Stdio/fd mode:** `--auth-fd` points to a secure channel (pipe, not terminal)
- [ ] **Named pipe mode:** Only used for single-client or sequential access patterns
- [ ] **Named pipe mode:** Token file is on a local filesystem

### Cygwin-Specific

- [ ] `--no-peercred` used only if Python/non-C clients are needed
- [ ] Prefer `--named-pipe` over `--no-peercred` when possible
- [ ] `/tmp` has appropriate sticky bit

### Monitoring

- [ ] Server process monitored for unexpected termination
- [ ] PID file presence monitored (daemon mode)
- [ ] Socket file presence monitored
- [ ] Stale socket/token cleanup scripted

## Known Limitations

1. **No encryption:**  Data travels in plaintext over the Unix socket or
   Named Pipe.  Acceptable for local-only communication; not suitable for
   network transport without an additional encryption layer.

2. **No authorization:**  All authenticated clients have equal access.
   No per-user or per-command access control.  All v2 channels are available
   to any authenticated client.

3. **No audit logging:**  Commands executed via EVAL are not logged by
   the server.  Shell history is explicitly disabled (`SEVAL_NOHIST`).
   Observability events (v2 CHAN_OBSERVE) provide runtime monitoring but
   are not persisted.

4. **Single-threaded blocking:**  A malicious client can hold a session
   by keeping a connection open without sending QUIT.  In named pipe mode,
   this blocks all subsequent clients.

5. **No rate limiting:**  Unlimited AUTH attempts are permitted.

6. **Token in process arguments:**  `bashclient --auth TOKEN` exposes
   the token in `/proc/<pid>/cmdline` (visible to same-user processes).
   Mitigation: read token from file instead of passing on command line.

7. **No swap protection:**  The token in server memory may be swapped
   to disk.  Use `mlock()` in security-critical deployments.

8. **Named pipe name enumeration:**  Windows Named Pipe names are visible
   to all processes via `\\.\pipe\` enumeration.  The DACL prevents
   unauthorized access but not discovery.

9. **Debug channel trust:**  The debug channel (CHAN_DEBUG) allows
   breakpoint-paused AST inspection, which exposes internal command
   structures.  Any authenticated client can use the debug channel.
