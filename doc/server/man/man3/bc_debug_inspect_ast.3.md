# bc\_debug\_inspect\_ast(3) — get JSON serialization of the current COMMAND tree

# SYNOPSIS

```c
#include <bashclient.h>

int bc_debug_inspect_ast(bc_client_t *c, char **json_ast);
```

# DESCRIPTION

Retrieves a JSON serialization of the COMMAND tree for the
command currently stopped at a breakpoint.  The function sends an
`inspect_ast` request on `CHAN_DEBUG` (channel 4) and returns the
server's response as a heap-allocated JSON string.

The JSON structure represents the bash internal `COMMAND` tree,
which is the parsed representation of the command.  This provides
deeper insight than the command string alone, showing the full
structure of pipelines, redirections, compound commands, and their
subcomponents.

The caller receives a heap-allocated string through `*json_ast`
and must free it using `free()` (or `bc_free()` if provided by
the library).

This function should be called when the debugger is stopped at a
breakpoint.  If called when not stopped, the server returns an
error.

**JSON structure overview:**

The top-level JSON object contains a `type` field identifying the
command type, plus type-specific fields:

| Command type | JSON `type` value | Key fields |
|-------------|-------------------|------------|
| Simple | `"simple"` | `words`, `redirects`, `assigns` |
| Pipeline | `"pipeline"` | `commands`, `negated` |
| Connection (&&, \|\|) | `"connection"` | `first`, `second`, `connector` |
| Group `{ }` | `"group"` | `command` |
| Subshell `( )` | `"subshell"` | `command` |
| For loop | `"for"` | `name`, `words`, `action` |
| While/Until | `"while"` / `"until"` | `test`, `action` |
| If | `"if"` | `test`, `true_case`, `false_case` |
| Case | `"case"` | `word`, `clauses` |
| Function def | `"function"` | `name`, `body` |

# PARAMETERS

- **c** — Pointer to an authenticated `bc_client_t` connection.
  Must not be NULL.

- **json_ast** — Output pointer for the JSON string.  Must not be
  NULL.  On success, set to a heap-allocated null-terminated
  string that the caller must free.

# RETURN VALUE

Returns `BC_OK` (0) on success, with `*json_ast` populated.

Returns a negative error code on failure (`*json_ast` is not
modified):

| Code | Meaning |
|------|---------|
| `BC_ERR_PARAM` (-7) | NULL client or json_ast pointer |
| `BC_ERR_AUTH` (-1) | Client not authenticated |
| `BC_ERR_PROTOCOL` (-2) | Unexpected server response |
| `BC_ERR_TIMEOUT` (-3) | Server did not respond in time |
| `BC_ERR_TRANSPORT` (-4) | Connection error |
| `BC_ERR_SERVER` (-5) | Not stopped at a breakpoint |
| `BC_ERR_NOMEM` (-6) | Memory allocation failed |

# ERRORS

- Returns `BC_ERR_SERVER` if the debugger is not stopped at a
  breakpoint (no COMMAND tree is available).
- Returns `BC_ERR_NOMEM` if the JSON string cannot be allocated.

# EXAMPLES

# Inspect AST at a breakpoint

```c
#include <bashclient.h>
#include <stdio.h>
#include <stdlib.h>

static void inspect_handler(const bc_break_hit_event_t *ev,
                            void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;
    char *ast = NULL;

    printf("Stopped at line %d: %s\n", ev->line, ev->command);

    int rc = bc_debug_inspect_ast(c, &ast);
    if (rc == BC_OK) {
        printf("AST:\n%s\n", ast);
        free(ast);
    } else {
        fprintf(stderr, "inspect_ast failed: %d\n", rc);
    }

    bc_debug_continue(c);
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    bc_debug_enable(c);
    bc_debug_add_breakpoint(c, "command", "echo", -1, NULL);
    bc_debug_on_break_hit(c, inspect_handler, c);

    bc_eval(c, "echo hello | grep h", NULL, NULL, NULL);

    while (bc_poll(c, 5000) >= 0)
        ;

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

**Example JSON output for `echo hello | grep h`:**

```json
{
  "type": "pipeline",
  "negated": false,
  "commands": [
    {
      "type": "simple",
      "words": ["echo", "hello"],
      "redirects": [],
      "assigns": []
    },
    {
      "type": "simple",
      "words": ["grep", "h"],
      "redirects": [],
      "assigns": []
    }
  ]
}
```

# Conditional inspection

```c
#include <bashclient.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_redirects(const bc_break_hit_event_t *ev,
                            void *ud)
{
    bc_client_t *c = (bc_client_t *)ud;
    char *ast = NULL;

    if (bc_debug_inspect_ast(c, &ast) == BC_OK) {
        /* Check if command has output redirections */
        if (strstr(ast, "\"redirects\":[{")) {
            printf("WARNING: command has redirections: %s\n",
                   ev->command);
        }
        free(ast);
    }

    bc_debug_continue(c);
}
```

# SEE ALSO

`bc_debug_on_break_hit`(3), `bc_debug_enable`(3),
`bc_debug_step`(3), `cmd_serialize`(3), `cmd_deserialize`(3)

# NOTES

- The JSON output matches the format produced by `cmd_serialize()`
  on the server side.  See `cmd_serialize`(3) for the complete
  schema of all 10 command types.
- The AST reflects the parsed command structure before execution.
  Variable expansions and glob patterns are not yet resolved.
- The returned string can be large for complex compound commands.
  There is no server-side size limit, but the client allocates
  memory proportional to the JSON size.
- This function is only valid while stopped at a breakpoint.
  Between breakpoints, there is no pending COMMAND to inspect.
- The JSON is pretty-printed by the server for readability.
- Thread safety: the client library is not thread-safe.
