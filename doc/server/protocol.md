# bash-server Wire Protocol Specification

**Version:** 1.0  
**Transport:** Unix domain socket (`AF_UNIX`, `SOCK_STREAM`)  
**Encoding:** UTF-8 text, LF-terminated lines  
**Payload encoding:** Base64 (RFC 4648)

## Protocol Overview

The bash-server protocol is a synchronous, request-response text protocol
operating over a Unix domain socket.  Each message is a single line terminated
by a newline character (`\n`).  Carriage returns (`\r`) are silently stripped
on input, allowing both LF and CRLF line endings from clients.

### Connection Lifecycle

```
Client                              Server
  |                                    |
  |--- connect() ------------------->  |
  |                                    |  (accept)
  |--- AUTH <token>\n -------------->  |
  |<-- OK\n -------------------------  |  (or ERR <reason>\n)
  |                                    |
  |--- EVAL <command>\n ------------>  |
  |<-- STDOUT [<base64>]\n ----------  |
  |<-- STDERR [<base64>]\n ----------  |
  |<-- EXIT <code>\n ----------------  |
  |                                    |
  |--- PING\n ---------------------->  |
  |<-- PONG\n -----------------------  |
  |                                    |
  |--- QUIT\n ---------------------->  |
  |<-- BYE\n ------------------------  |
  |                                    |
  |        (connection closed)         |
```

### Session State Machine

```
          connect()
              │
              ▼
      ┌───────────────┐
      │ UNAUTHENTICATED│
      └───────┬───────┘
              │
         AUTH (valid)
              │
              ▼
      ┌───────────────┐
      │ AUTHENTICATED  │◄──── EVAL / PING
      └───────┬───────┘
              │
            QUIT
              │
              ▼
      ┌───────────────┐
      │   CLOSED       │
      └───────────────┘
```

**State transitions:**

| Current State | Command | Valid | Next State |
|--------------|---------|-------|------------|
| UNAUTHENTICATED | `AUTH` (valid) | Yes | AUTHENTICATED |
| UNAUTHENTICATED | `AUTH` (invalid) | Yes | UNAUTHENTICATED |
| UNAUTHENTICATED | `PING` | Yes | UNAUTHENTICATED |
| UNAUTHENTICATED | `EVAL` | Yes (error) | UNAUTHENTICATED |
| UNAUTHENTICATED | `QUIT` | Yes | CLOSED |
| AUTHENTICATED | `AUTH` | Yes (no-op) | AUTHENTICATED |
| AUTHENTICATED | `EVAL` | Yes | AUTHENTICATED |
| AUTHENTICATED | `PING` | Yes | AUTHENTICATED |
| AUTHENTICATED | `QUIT` | Yes | CLOSED |
| Any | (EOF/disconnect) | — | CLOSED |
| Any | (unknown command) | Yes (error) | (unchanged) |

## Message Format

### Request Format

```
COMMAND [ARGUMENT]\n
```

- **COMMAND**: Case-insensitive ASCII keyword (converted to uppercase internally).
  Maximum 31 characters.
- **ARGUMENT**: Optional.  Everything after the first whitespace delimiter
  through end of line (trailing whitespace trimmed).  Maximum 65,536 bytes.
- Lines are terminated by `\n` (LF).  `\r` (CR) characters are silently discarded.

### Response Format

```
STATUS [PAYLOAD]\n
```

- **STATUS**: Uppercase ASCII keyword.
- **PAYLOAD**: Optional, status-specific data.  May be base64-encoded binary.
- Lines are terminated by `\n` (LF).

## Commands

### AUTH — Authenticate

Authenticates the client session using a shared secret token.

**Request:**
```
AUTH <token>
```

**Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `token` | string | Yes | 64-character hex-encoded authentication token |

**Responses:**

| Response | Condition |
|----------|-----------|
| `OK` | Authentication successful |
| `OK already authenticated` | Session already authenticated (idempotent) |
| `ERR token required` | No token provided |
| `ERR invalid token` | Token does not match server's token |

**Behavior:**
- Token comparison uses constant-time algorithm to prevent timing attacks.
- On first successful AUTH, the server initializes the Bash interpreter
  (builtins, traps, signals, variables, tilde expansion, job control stubs,
  input system, flags, shell options, bashopts).
- Subsequent AUTH commands on an already-authenticated session return
  `OK already authenticated` without re-initialization.

**Security notes:**
- The token is transmitted in plaintext over the socket.  The socket's
  filesystem permissions (mode `0600`) provide the access control.
- See [security.md](security.md) for the complete threat model.

### EVAL — Evaluate Command

Executes a Bash command string and returns captured output.

**Request:**
```
EVAL <command>
```

**Parameters:**

| Parameter | Type | Required | Description |
|-----------|------|----------|-------------|
| `command` | string | Yes | Bash command to evaluate (max 65,536 bytes) |

**Responses (multi-line sequence):**

The server sends exactly three response lines for each EVAL:

```
STDOUT [<base64-encoded-stdout>]
STDERR [<base64-encoded-stderr>]
EXIT <exit-code>
```

| Response | Description |
|----------|-------------|
| `STDOUT <base64>` | Base64-encoded stdout output. Omitted payload if empty. |
| `STDOUT` | Empty stdout (no payload). |
| `STDERR <base64>` | Base64-encoded stderr output. Omitted payload if empty. |
| `STDERR` | Empty stderr (no payload). |
| `EXIT <code>` | Integer exit code (0–255). 128+N for signal N. |

**Error responses (instead of the above sequence):**

| Response | Condition |
|----------|-----------|
| `ERR not authenticated` | Session not yet authenticated |
| `ERR command required` | Empty command string |
| `ERR pipe creation failed` | Internal pipe() failure |
| `ERR fork failed` | Internal fork() failure |

**Execution model:**

1. The server `fork()`s a child process.
2. The child redirects stdout/stderr to pipes.
3. The child calls `parse_and_execute()` with flags `SEVAL_NONINT | SEVAL_NOHIST`
   (non-interactive, no history recording).
4. The child flushes and exits with `last_command_exit_value`.
5. The parent reads pipe output, base64-encodes it, and sends the three
   response lines.
6. The parent calls `waitpid()` to collect the child's exit status.

**Output limits:**
- Each stream (stdout, stderr) is capped at 1 MB (`SERVER_MAX_OUTPUT`).
  Output beyond this limit is silently truncated.

**Exit codes:**
- `0`: Success
- `1–125`: Command-specific failure
- `126`: Command found but not executable
- `127`: Command not found
- `128+N`: Command terminated by signal N (e.g., 137 = SIGKILL)

### PING — Health Check

Tests connectivity without requiring authentication.

**Request:**
```
PING
```

**Response:**
```
PONG
```

**Behavior:**
- Available in both authenticated and unauthenticated states.
- No side effects.

### QUIT — Disconnect

Gracefully terminates the session.

**Request:**
```
QUIT
```

**Response:**
```
BYE
```

**Behavior:**
- The server sends `BYE` and then the session handler returns, causing
  the connection to be closed and session resources freed.

## Response Codes Summary

| Code | Meaning | Context |
|------|---------|---------|
| `OK` | Success | AUTH |
| `ERR` | Error (followed by reason) | Any command |
| `PONG` | Health check response | PING |
| `BYE` | Session closing | QUIT |
| `STDOUT` | Command stdout output (base64) | EVAL |
| `STDERR` | Command stderr output (base64) | EVAL |
| `EXIT` | Command exit code | EVAL |

## Base64 Encoding

Output payloads use standard Base64 encoding per RFC 4648:

- Alphabet: `A-Za-z0-9+/`
- Padding: `=` (standard padding)
- No line wrapping (single contiguous string)
- Decode output preserves binary data including NUL bytes

## Buffer Limits

| Constant | Value | Purpose |
|----------|-------|---------|
| `SERVER_MAX_LINE` | 8,192 bytes | Maximum protocol line length |
| `SERVER_MAX_TOKEN` | 256 bytes | Maximum token buffer |
| `SERVER_MAX_CMD` | 65,536 bytes | Maximum command string |
| `SERVER_MAX_OUTPUT` | 1,048,576 bytes (1 MB) | Maximum captured output per stream |

## Error Handling

### Connection Errors
- If the client disconnects mid-session, `protocol_read_line()` returns -1
  (EOF), causing `session_handle()` to exit its command loop and clean up.
- Write errors (broken pipe) are detected by `protocol_write_line()` and
  propagated as -1 returns.  SIGPIPE is ignored at the process level.

### Protocol Errors
- Unknown commands receive `ERR unknown command: <CMD>`.
- Empty or whitespace-only lines receive `ERR invalid command`.
- Malformed arguments receive command-specific error responses.

### Concurrency
- The server is single-threaded.  Clients are handled sequentially.
- While one client is connected, subsequent connection attempts block in
  the kernel's listen backlog (`SOMAXCONN`).
- Long-running EVAL commands block the server from accepting new clients.

## Wire Examples

### Successful Command Execution

```
→ AUTH a1b2c3d4...  (64 hex chars)
← OK
→ EVAL echo hello
← STDOUT aGVsbG8K
← STDERR
← EXIT 0
→ QUIT
← BYE
```

### Failed Authentication

```
→ AUTH wrong_token
← ERR invalid token
→ EVAL echo test
← ERR not authenticated
→ QUIT
← BYE
```

### Command with Stderr

```
→ AUTH a1b2c3d4...
← OK
→ EVAL ls /nonexistent
← STDOUT
← STDERR bHM6IGNhbm5vdCBhY2Nlc3MgJy9ub25leGlzdGVudCc6IE5vIHN1Y2ggZmlsZSBvciBkaXJlY3Rvcnk=
← EXIT 2
```

### Multi-Command Session

```
→ AUTH a1b2c3d4...
← OK
→ EVAL export FOO=bar
← STDOUT
← STDERR
← EXIT 0
→ EVAL echo $FOO
← STDOUT YmFyCg==
← STDERR
← EXIT 0
→ PING
← PONG
→ QUIT
← BYE
```

Note: Because each EVAL forks a child process, variable assignments in one
EVAL do **not** persist to subsequent EVALs.  Each command executes in an
isolated child environment.
