# cmd\_deserialize(3) — deserialize JSON string to COMMAND tree

# SYNOPSIS

    #include "server.h"
    #include "command.h"

    COMMAND *cmd_deserialize(const char *json);

# DESCRIPTION

Deserializes a JSON string into a bash `COMMAND` tree.  Reconstructs
the full AST from JSON produced by `cmd_serialize()`, allocating all
necessary structures (`COMMAND`, `SIMPLE_COM`, `CONNECTION`, `FOR_COM`,
`IF_COM`, `WHILE_COM`, `CASE_COM`, `GROUP_COM`, `SUBSHELL_COM`,
`FUNCTION_DEF`, `WORD_LIST`, `WORD_DESC`, `REDIRECT`, `PATTERN_LIST`).

The resulting COMMAND tree is compatible with `execute_command()` for
pre-parsed command execution, or can be inspected and modified by
clients.

Uses a simplified JSON parser internally (`json_find_key()`,
`json_skip_value()`, `json_extract_string()`, `json_extract_int()`)
that handles nested objects, arrays, strings, numbers, booleans, and
null values.

**Deserialization dispatch:**

The `"type"` field determines which sub-object key to look for and
which type-specific deserializer to call:

| Type string | Sub-object key | Deserializer |
|-------------|---------------|--------------|
| `"cm_simple"` | `"simple"` | `deserialize_simple()` |
| `"cm_connection"` | `"connection"` | `deserialize_connection()` |
| `"cm_for"` | `"for"` | `deserialize_for()` |
| `"cm_if"` | `"if"` | `deserialize_if()` |
| `"cm_while"` | `"while"` | `deserialize_while()` |
| `"cm_until"` | `"while"` | `deserialize_while()` (with cm\_until type) |
| `"cm_case"` | `"case"` | `deserialize_case()` |
| `"cm_group"` | `"group"` | `deserialize_group()` |
| `"cm_subshell"` | `"subshell"` | `deserialize_subshell()` |
| `"cm_function_def"` | `"function_def"` | `deserialize_function_def()` |

# PARAMETERS

- **json** — JSON string to deserialize.  Must be a valid JSON object
  as produced by `cmd_serialize()`, or the string `"null"`.  Must not
  be NULL.

# RETURN VALUE

Returns a `malloc()`'d `COMMAND` structure that the caller must free
with `cmd_free()`.

Returns NULL if:
- **json** is NULL
- The JSON string is `"null"` or does not start with `{`
- The `"type"` field is missing or unrecognized
- Memory allocation fails

# SEE ALSO

`cmd_serialize`(3), `cmd_free`(3)

# NOTES

- The JSON parser is simplified and assumes well-formed JSON as
  produced by `cmd_serialize()`.  Arbitrary or adversarial JSON input
  may cause undefined behavior.
- String values are unescaped during extraction (`\"` -> `"`,
  `\\` -> `\`, `\n` -> newline, etc.).
- The deserialized tree uses `calloc()` for all structures, so
  unset fields default to zero/NULL.
- Connection connector names are mapped back to integer values:
  `";"` -> `';'`, `"|"` -> `'|'`, `"&"` -> `'&'`,
  `"&&"` -> `288`, `"||"` -> `289`.
