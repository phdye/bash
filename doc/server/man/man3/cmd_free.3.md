# cmd\_free(3) — recursively free a deserialized COMMAND tree

# SYNOPSIS

    #include "server.h"
    #include "command.h"

    void cmd_free(COMMAND *cmd);

# DESCRIPTION

Recursively frees a `COMMAND` tree produced by `cmd_deserialize()`.
Walks the tree and frees all nested structures including:

- `COMMAND` nodes
- `SIMPLE_COM`, `CONNECTION`, `FOR_COM`, `IF_COM`, `WHILE_COM`,
  `CASE_COM`, `GROUP_COM`, `SUBSHELL_COM`, `FUNCTION_DEF`
- `WORD_LIST` chains and their `WORD_DESC` entries (including
  `word` strings)
- `REDIRECT` chains (including `redirectee.filename` words and
  `here_doc_eof` strings)
- `PATTERN_LIST` chains (including patterns and action commands)
- `source_file` strings in `FUNCTION_DEF`

**Do not** use this function on COMMAND trees produced by the bash
parser (`parse_and_execute`); those use bash's internal memory
management.  This function is only for trees allocated by
`cmd_deserialize()`.

# PARAMETERS

- **cmd** — Pointer to the root COMMAND node to free.  May be NULL,
  in which case the function returns immediately (no-op).

# RETURN VALUE

None.

# SEE ALSO

`cmd_deserialize`(3), `cmd_serialize`(3)

# NOTES

- This function calls the internal `cmd_free_recursive()` which
  dispatches to type-specific free routines based on `cmd->type`.
- Safe to call with NULL at any level of nesting (all recursive
  calls check for NULL before dereferencing).
- Connection commands free both `first` and `second` children
  recursively.
