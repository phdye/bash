# PROTOCOL_PARSE_COMMAND(3) — Parse a Protocol Command Line

## NAME

protocol_parse_command — split a protocol line into command keyword and argument

## SYNOPSIS

```c
#include "server.h"

int protocol_parse_command(const char *line, char *cmd, char *arg, size_t argsize);
```

## DESCRIPTION

Parses a protocol input line into a command keyword and an optional argument.

1. Skips leading whitespace.
2. Extracts the command keyword (up to 31 characters), converting to uppercase.
3. Skips whitespace between command and argument.
4. Copies the remainder as the argument, trimming trailing whitespace.

## PARAMETERS

**line**
:   Input line to parse (NUL-terminated, newline already stripped).

**cmd**
:   Output buffer for the command keyword.  Must be at least 32 bytes.
    Always NUL-terminated.  Always uppercase.

**arg**
:   Output buffer for the argument.  NUL-terminated.  May be empty string
    if no argument present.  Trailing whitespace is trimmed.

**argsize**
:   Size of the **arg** buffer in bytes.

## RETURN VALUE

Returns **0** on success.

Returns **-1** if the line is empty or contains only whitespace (no command
keyword extracted).

## NOTES

- Command keywords are case-insensitive on input: `auth`, `Auth`, and `AUTH`
  all produce `"AUTH"` in the cmd buffer.
- The argument preserves internal whitespace: `EVAL echo hello world` produces
  arg `"echo hello world"`.
- The command keyword is limited to 31 characters.  Longer keywords are
  truncated.
- Trailing whitespace on the argument is trimmed.

## EXAMPLES

| Input | cmd | arg |
|-------|-----|-----|
| `"AUTH token123"` | `"AUTH"` | `"token123"` |
| `"PING"` | `"PING"` | `""` |
| `"auth token"` | `"AUTH"` | `"token"` |
| `"  PING"` | `"PING"` | `""` |
| `""` | (error) | — |
| `"   "` | (error) | — |
| `"EVAL echo hello world"` | `"EVAL"` | `"echo hello world"` |
| `"AUTH token   "` | `"AUTH"` | `"token"` |
| `"AUTH   token"` | `"AUTH"` | `"token"` |

## SOURCE

`server_protocol.c:126`

## SEE ALSO

[protocol_read_line.md](protocol_read_line.md), [auth.md](auth.md), [eval.md](eval.md)
