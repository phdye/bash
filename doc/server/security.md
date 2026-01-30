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
| **Same-user process** | Can read token file, connect to socket | Intended access model |
| **Different-user process** | Cannot read socket or token (0600) | Unix file permissions |
| **Root/SYSTEM** | Full access to all resources | Out of scope (trusted) |
| **Network attacker** | No direct access (Unix socket is local) | No TCP exposure |
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

### Authentication Flow

```
1. Server starts → reads 32 bytes from /dev/urandom
2. Hex-encodes to 64-character string
3. Writes to <socket>.token with mode 0600 (O_CREAT|O_EXCL)
4. Zeros raw bytes on stack (memset)

5. Client reads <socket>.token
6. Client sends: AUTH <token>\n
7. Server compares with constant-time algorithm
8. If match: session authenticated, bash initialized
9. If no match: ERR returned, session remains unauthenticated
```

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

The Unix domain socket is the sole communication channel:

- **No TCP exposure:**  `AF_UNIX` with `SOCK_STREAM` is local-only.
- **File permission enforcement:**  Mode `0600` prevents other users
  from connecting.
- **Stale socket handling:**  `unlink()` before `bind()` removes stale
  socket files from previous runs.

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
| Long-running command | Blocks server (single-threaded) | External timeout (client-side) |
| Many connections | Queued in listen backlog | `max_clients` (reserved, not enforced) |
| Large output | Memory consumption (up to 2 MB) | `SERVER_MAX_OUTPUT` cap |
| fork bomb via EVAL | System resource exhaustion | OS-level ulimits (`nproc`) |
| Socket exhaustion | File descriptor starvation | OS-level ulimits (`nofile`) |
| Repeated auth failures | CPU time (negligible) | No rate limiting implemented |

**Recommendations for production deployment:**
- Run under a process manager with restart capabilities.
- Set `ulimit -u` (nproc) to limit fork bombs.
- Set `ulimit -n` (nofile) appropriately.
- Consider running in a container or namespace for isolation.
- Implement client-side timeouts for EVAL commands.

## Sensitive Data Handling

### Token Lifecycle

| Phase | Token State | Protection |
|-------|-------------|------------|
| Generation | 32 bytes on stack | Zeroed after hex encoding |
| Hex string | Heap-allocated (server lifetime) | Process memory protection |
| File storage | `<socket>.token` (0600) | File permissions |
| Transmission | Plaintext over Unix socket | Socket permissions |
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

### Monitoring

- [ ] Server process monitored for unexpected termination
- [ ] PID file presence monitored (daemon mode)
- [ ] Socket file presence monitored
- [ ] Stale socket/token cleanup scripted

## Known Limitations

1. **No encryption:**  Data travels in plaintext over the Unix socket.
   Acceptable for local-only communication; not suitable for network
   transport without an additional encryption layer.

2. **No authorization:**  All authenticated clients have equal access.
   No per-user or per-command access control.

3. **No audit logging:**  Commands executed via EVAL are not logged by
   the server.  Shell history is explicitly disabled (`SEVAL_NOHIST`).

4. **Single-threaded blocking:**  A malicious client can hold the server
   by keeping a connection open without sending QUIT.

5. **No rate limiting:**  Unlimited AUTH attempts are permitted.

6. **Token in process arguments:**  `bashclient --auth TOKEN` exposes
   the token in `/proc/<pid>/cmdline` (visible to same-user processes).
   Mitigation: read token from file instead of passing on command line.

7. **No swap protection:**  The token in server memory may be swapped
   to disk.  Use `mlock()` in security-critical deployments.
