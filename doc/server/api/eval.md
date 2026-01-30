# EVAL(3) — Evaluate a Bash Command

## NAME

EVAL — execute a Bash command string and return captured output

## SYNOPSIS

```
EVAL <command>
```

## DESCRIPTION

The **EVAL** command executes the given Bash command string in a forked child
process, captures stdout and stderr via pipes, and returns the results as
base64-encoded payloads along with the command's exit code.

**Requires authentication.**  If the session is not authenticated, returns
`ERR not authenticated`.

### Execution Model

1. Two pipes are created (one for stdout, one for stderr).
2. The server `fork()`s a child process.
3. The child redirects stdout and stderr to the pipe write ends.
4. The child calls `parse_and_execute(command, "bash-server", SEVAL_NONINT | SEVAL_NOHIST)`.
5. The child calls `fflush(stdout)` and `fflush(stderr)`.
6. The child calls `_exit(last_command_exit_value)`.
7. The parent reads all data from both pipe read ends.
8. The parent calls `waitpid()` to collect the exit status.
9. The parent base64-encodes the output and sends the three response lines.

### Execution Flags

| Flag | Effect |
|------|--------|
| `SEVAL_NONINT` | Non-interactive evaluation.  No prompts, no job control. |
| `SEVAL_NOHIST` | Do not add the command to shell history. |

### Isolation

Each EVAL runs in an isolated child process.  This means:

- **No shared state:**  Variable assignments, function definitions, `cd`
  directory changes, and other side effects do not persist between
  EVAL calls.
- **Crash safety:**  A segfault or abort in the command does not affect
  the server.
- **Resource cleanup:**  File descriptors and memory are reclaimed by
  process exit.

### Output Limits

Each output stream (stdout, stderr) is capped at `SERVER_MAX_OUTPUT`
(1,048,576 bytes / 1 MB).  Output beyond this limit is silently truncated.

The `command` argument is limited to `SERVER_MAX_CMD` (65,536 bytes).

## ARGUMENTS

**command**
:   A Bash command string.  May contain shell metacharacters, pipes,
    redirections, subshells, compound commands, etc.  The string is passed
    to `parse_and_execute()` which processes it through the full Bash parser.
    Maximum length: 65,536 bytes.

## RESPONSES

EVAL always produces a three-line response sequence:

**`STDOUT [<base64>]`**
:   Base64-encoded stdout output.  If stdout is empty, the line is just
    `STDOUT` with no payload.

**`STDERR [<base64>]`**
:   Base64-encoded stderr output.  If stderr is empty, the line is just
    `STDERR` with no payload.

**`EXIT <code>`**
:   Integer exit code of the command.

### Error Responses

Instead of the three-line sequence, these single-line errors may be returned:

**`ERR not authenticated`**
:   The session has not completed AUTH.

**`ERR command required`**
:   The EVAL command was sent with an empty argument.

**`ERR pipe creation failed`**
:   The server failed to create internal pipes (`pipe()` returned -1).

**`ERR fork failed`**
:   The server failed to fork a child process (`fork()` returned -1).

## EXIT CODES

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1–125 | Command-specific failure |
| 126 | Command found but not executable |
| 127 | Command not found |
| 128+N | Command killed by signal N (e.g., 137 = SIGKILL, 139 = SIGSEGV) |

## EXAMPLES

Simple command:
```
→ EVAL echo hello
← STDOUT aGVsbG8K
← STDERR
← EXIT 0
```

Decoding: `aGVsbG8K` → `hello\n`

Command with stderr:
```
→ EVAL ls /nonexistent
← STDOUT
← STDERR bHM6IC4uLg==
← EXIT 2
```

Multi-command pipeline:
```
→ EVAL echo hello | tr a-z A-Z
← STDOUT SEVMTE8K
← STDERR
← EXIT 0
```

Command not found:
```
→ EVAL nonexistent_command
← STDOUT
← STDERR YmFzaC1zZXJ2ZXI6IG5vbmV4aXN0ZW50X2NvbW1hbmQ6IGNvbW1hbmQgbm90IGZvdW5k
← EXIT 127
```

Unauthenticated:
```
→ EVAL echo test
← ERR not authenticated
```

## NOTES

- The command string is `strdup()`'d before passing to `parse_and_execute()`,
  which frees it internally.
- The shell name in error messages is `"bash-server"`.
- The child process inherits the server's environment variables.
- There is no timeout on command execution.  Clients should implement
  their own timeout mechanism.

## SOURCE

- Handler: `handle_eval()` in `server_session.c:324`
- Output capture: `capture_output()` in `server_session.c:208`
- Pipe reading: `read_all_fd()` in `server_session.c:166`

## SEE ALSO

[auth.md](auth.md), [protocol_base64_encode.md](protocol_base64_encode.md),
[protocol_base64_decode.md](protocol_base64_decode.md)
