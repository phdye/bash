# cmd\_serialize(3) — serialize bash COMMAND tree to JSON

# SYNOPSIS

    #include "server.h"
    #include "command.h"

    char *cmd_serialize(COMMAND *cmd);

# DESCRIPTION

Serializes a bash `COMMAND` tree into a JSON string representation.
The resulting JSON preserves the full AST structure, enabling
round-trip serialization/deserialization and client-side AST
inspection at debugger breakpoints.

The serialization is recursive: compound commands (connections, if,
for, while, case, group, subshell, function\_def) serialize their
child commands inline.

**Supported command types:**

| Type | JSON key | Contents |
|------|----------|----------|
| `cm_simple` | `"simple"` | flags, line, words (array), redirects |
| `cm_connection` | `"connection"` | connector, first, second (recursive) |
| `cm_for` | `"for"` | flags, line, name, map\_list, action |
| `cm_if` | `"if"` | flags, test, true\_case, false\_case |
| `cm_while` | `"while"` | flags, test, action |
| `cm_until` | `"while"` | flags, test, action (same key as while) |
| `cm_case` | `"case"` | flags, line, word, clauses array |
| `cm_group` | `"group"` | command (recursive) |
| `cm_subshell` | `"subshell"` | flags, line, command |
| `cm_function_def` | `"function_def"` | flags, line, name, source\_file, command |

Each COMMAND node includes: `"type"` (enum name string), `"flags"`,
`"line"`, optional `"redirects"`, and the type-specific sub-object.

Words are serialized as `{"word":"text","flags":N}` arrays.
Redirects include `instruction`, `redirector`, `flags`, `rflags`,
`filename` or `dest`, and optional `here_doc_eof`.

Uses a growable string buffer (`serbuf_t`) internally for efficient
JSON construction.

# PARAMETERS

- **cmd** — Pointer to a bash `COMMAND` structure.  May be NULL,
  in which case the string `"null"` is returned.

# RETURN VALUE

Returns a `malloc()`'d JSON string that the caller must `free()`.

Returns the string `"null"` (via `strdup`) if **cmd** is NULL.

Returns NULL if memory allocation fails during buffer initialization.

# SEE ALSO

`cmd_deserialize`(3), `cmd_free`(3), `debug_handle_message`(3)

# NOTES

- String values are JSON-escaped (quotes, backslashes, control
  characters).
- The `cm_until` type serializes with the same `"while"` sub-object
  key as `cm_while`.  The `"type"` field distinguishes them.
- Unsupported command types (e.g., `cm_arith`, `cm_cond`, `cm_coproc`)
  produce `"unsupported":true` in the output.
- Connection connectors are mapped: `';'` -> `";"`, `'|'` -> `"|"`,
  `'&'` -> `"&"`, `288` -> `"&&"` (AND\_AND), `289` -> `"||"` (OR\_OR).
