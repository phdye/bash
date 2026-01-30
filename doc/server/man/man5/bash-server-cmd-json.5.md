# bash-server-cmd-json(5) — bash-server COMMAND tree JSON serialization

# DESCRIPTION

This page documents the JSON serialization format used by
**bash-server**(1) to represent Bash's internal **COMMAND** abstract
syntax tree (AST). The format is implemented in **cmd_serialize.c**
and supports bidirectional conversion:

- **cmd_serialize(COMMAND\*)** — serialize a COMMAND tree to JSON.
- **cmd_deserialize(JSON)** — deserialize JSON back to a COMMAND tree.
- **cmd_free(COMMAND\*)** — free a deserialized COMMAND tree.

This format appears in the **"ast"** field of debug channel messages
(see **bash-server-json-messages**(5), CHAN_DEBUG) and in the
**"execute"** command on CHAN_COMMAND.

# FORMAT

# Top-level structure

Every COMMAND node serializes to a JSON object with these common
fields:

```json
{
  "type": "cm_TYPE",
  "flags": N,
  "line": N,
  "redirects": [...],
  "TYPE_KEY": { ... }
}
```

| Field | Type | Description |
|-------|------|-------------|
| **type** | string | Command type identifier (see below). |
| **flags** | integer | Command flags bitmask. |
| **line** | integer | Source line number. |
| **redirects** | array or null | List of redirect objects (see below). |
| *TYPE_KEY* | object | Type-specific payload (key name matches the type). |

# Command types

#### cm_simple

A simple command (program invocation or builtin).

```json
{
  "type": "cm_simple",
  "flags": N,
  "line": N,
  "redirects": [...],
  "simple": {
    "flags": N,
    "line": N,
    "words": [
      {"word": "echo", "flags": 0},
      {"word": "hello", "flags": 0}
    ],
    "redirects": [...]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| **flags** | integer | Simple command flags. |
| **line** | integer | Source line number. |
| **words** | array | Ordered list of word objects. |
| **redirects** | array or null | Command-level redirections. |

#### cm_connection

Two commands joined by a connector (`;`, `|`, `&`, `&&`, `||`).

```json
{
  "type": "cm_connection",
  "flags": N,
  "line": N,
  "redirects": null,
  "connection": {
    "connector": ";",
    "first": { ... },
    "second": { ... }
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| **connector** | string | One of: `";"`, `"|"`, `"&"`, `"&&"`, `"||"`. |
| **first** | object | Left-hand COMMAND node. |
| **second** | object or null | Right-hand COMMAND node. |

#### cm_for

A **for** loop.

```json
{
  "type": "cm_for",
  "flags": N,
  "line": N,
  "redirects": [...],
  "for": {
    "flags": N,
    "line": N,
    "name": "i",
    "map_list": ["a", "b", "c"],
    "action": { ... }
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| **flags** | integer | For command flags. |
| **line** | integer | Source line number. |
| **name** | string | Loop variable name. |
| **map_list** | array or null | Word list to iterate over. Null for `for x; do` (uses `$@`). |
| **action** | object | Body COMMAND node. |

#### cm_if

An **if/elif/else** construct.

```json
{
  "type": "cm_if",
  "flags": N,
  "line": N,
  "redirects": [...],
  "if": {
    "flags": N,
    "test": { ... },
    "true_case": { ... },
    "false_case": { ... }
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| **flags** | integer | If command flags. |
| **test** | object | Condition COMMAND node. |
| **true_case** | object | Then-branch COMMAND node. |
| **false_case** | object or null | Else-branch COMMAND node (null if no else). |

#### cm_while

A **while** loop.

```json
{
  "type": "cm_while",
  "flags": N,
  "line": N,
  "redirects": [...],
  "while": {
    "flags": N,
    "test": { ... },
    "action": { ... }
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| **flags** | integer | While command flags. |
| **test** | object | Loop condition COMMAND node. |
| **action** | object | Loop body COMMAND node. |

#### cm_until

An **until** loop. Identical structure to cm_while.

```json
{
  "type": "cm_until",
  "flags": N,
  "line": N,
  "redirects": [...],
  "until": {
    "flags": N,
    "test": { ... },
    "action": { ... }
  }
}
```

#### cm_case

A **case** statement.

```json
{
  "type": "cm_case",
  "flags": N,
  "line": N,
  "redirects": [...],
  "case": {
    "flags": N,
    "line": N,
    "word": "variable",
    "clauses": [
      {
        "patterns": ["pattern1", "pattern2"],
        "action": { ... },
        "flags": N
      }
    ]
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| **flags** | integer | Case command flags. |
| **line** | integer | Source line number. |
| **word** | string | The word being matched. |
| **clauses** | array | List of pattern clause objects. |

Each clause object:

| Field | Type | Description |
|-------|------|-------------|
| **patterns** | array | List of pattern strings. |
| **action** | object or null | COMMAND node for this case arm. |
| **flags** | integer | Clause flags (e.g., fall-through `;&`). |

#### cm_group

A brace group `{ ...; }`.

```json
{
  "type": "cm_group",
  "flags": N,
  "line": N,
  "redirects": [...],
  "group": {
    "command": { ... }
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| **command** | object | The grouped COMMAND node. |

#### cm_subshell

A subshell group `( ... )`.

```json
{
  "type": "cm_subshell",
  "flags": N,
  "line": N,
  "redirects": [...],
  "subshell": {
    "flags": N,
    "line": N,
    "command": { ... }
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| **flags** | integer | Subshell flags. |
| **line** | integer | Source line number. |
| **command** | object | The subshell COMMAND node. |

#### cm_function_def

A function definition.

```json
{
  "type": "cm_function_def",
  "flags": N,
  "line": N,
  "redirects": [...],
  "function_def": {
    "flags": N,
    "line": N,
    "name": "myfunc",
    "source_file": "script.sh",
    "command": { ... }
  }
}
```

| Field | Type | Description |
|-------|------|-------------|
| **flags** | integer | Function definition flags. |
| **line** | integer | Source line number. |
| **name** | string | Function name. |
| **source_file** | string or null | Source file where the function is defined. |
| **command** | object | Function body COMMAND node. |

# Redirect objects

Redirections are represented as:

```json
{
  "instruction": N,
  "redirector": N,
  "flags": N,
  "rflags": N,
  "filename": "output.txt"
}
```

Or for file-descriptor redirections:

```json
{
  "instruction": N,
  "redirector": N,
  "flags": N,
  "rflags": N,
  "dest": N
}
```

Or for here-documents:

```json
{
  "instruction": N,
  "redirector": N,
  "flags": N,
  "rflags": N,
  "filename": "<here-doc content>",
  "here_doc_eof": "EOF"
}
```

| Field | Type | Description |
|-------|------|-------------|
| **instruction** | integer | Redirect instruction code (e.g., r_output_direction). |
| **redirector** | integer | Source file descriptor. |
| **flags** | integer | Redirect flags. |
| **rflags** | integer | Additional redirect flags. |
| **filename** | string | Target filename (or here-doc content). |
| **dest** | integer | Target file descriptor (for fd-to-fd redirects). |
| **here_doc_eof** | string | Here-document delimiter string. |

# Word objects

Words in command argument lists:

```json
{"word": "<text>", "flags": N}
```

| Field | Type | Description |
|-------|------|-------------|
| **word** | string | The word text. |
| **flags** | integer | Word flags (quoting, expansion state, etc.). |

# Null values

Missing or absent commands, redirect lists, and optional fields are
represented as JSON **null**. For example, a cm_if with no else branch
has `"false_case": null`.

# EXAMPLES

# Simple command: `echo hello`

```json
{
  "type": "cm_simple",
  "flags": 0,
  "line": 1,
  "redirects": null,
  "simple": {
    "flags": 0,
    "line": 1,
    "words": [
      {"word": "echo", "flags": 0},
      {"word": "hello", "flags": 0}
    ],
    "redirects": null
  }
}
```

# Pipeline: `ls | grep foo`

```json
{
  "type": "cm_connection",
  "flags": 0,
  "line": 1,
  "redirects": null,
  "connection": {
    "connector": "|",
    "first": {
      "type": "cm_simple",
      "flags": 0,
      "line": 1,
      "redirects": null,
      "simple": {
        "flags": 0,
        "line": 1,
        "words": [{"word": "ls", "flags": 0}],
        "redirects": null
      }
    },
    "second": {
      "type": "cm_simple",
      "flags": 0,
      "line": 1,
      "redirects": null,
      "simple": {
        "flags": 0,
        "line": 1,
        "words": [
          {"word": "grep", "flags": 0},
          {"word": "foo", "flags": 0}
        ],
        "redirects": null
      }
    }
  }
}
```

# If statement: `if test -f x; then echo yes; fi`

```json
{
  "type": "cm_if",
  "flags": 0,
  "line": 1,
  "redirects": null,
  "if": {
    "flags": 0,
    "test": {
      "type": "cm_simple",
      "flags": 0,
      "line": 1,
      "redirects": null,
      "simple": {
        "flags": 0,
        "line": 1,
        "words": [
          {"word": "test", "flags": 0},
          {"word": "-f", "flags": 0},
          {"word": "x", "flags": 0}
        ],
        "redirects": null
      }
    },
    "true_case": {
      "type": "cm_simple",
      "flags": 0,
      "line": 1,
      "redirects": null,
      "simple": {
        "flags": 0,
        "line": 1,
        "words": [
          {"word": "echo", "flags": 0},
          {"word": "yes", "flags": 0}
        ],
        "redirects": null
      }
    },
    "false_case": null
  }
}
```

# Redirect: `echo hello > output.txt`

```json
{
  "type": "cm_simple",
  "flags": 0,
  "line": 1,
  "redirects": [
    {
      "instruction": 1,
      "redirector": 1,
      "flags": 0,
      "rflags": 0,
      "filename": "output.txt"
    }
  ],
  "simple": {
    "flags": 0,
    "line": 1,
    "words": [
      {"word": "echo", "flags": 0},
      {"word": "hello", "flags": 0}
    ],
    "redirects": null
  }
}
```

# SEE ALSO

**bash-server**(1),
**bash-server-json-messages**(5),
**bash-server-v2-protocol**(5),
**bash**(1)

# AUTHORS

GNU Bash is written by Brian Fox and Chet Ramey. The bash-server
extension and this documentation were developed as part of the
Cygwin bash-server project.

# COPYRIGHT

Copyright (C) 2024-2025 Free Software Foundation, Inc. License GPLv3+:
GNU GPL version 3 or later <https://www.gnu.org/licenses/gpl.html>.
This is free software; you are free to change and redistribute it.
There is NO WARRANTY, to the extent permitted by law.
