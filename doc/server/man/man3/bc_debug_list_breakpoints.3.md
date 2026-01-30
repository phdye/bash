# bc\_debug\_list\_breakpoints(3) — list all active breakpoints

# SYNOPSIS

```c
#include <bashclient.h>

typedef struct {
    int    id;           /* Server-assigned breakpoint ID */
    char  *kind;         /* "command", "line", or "function" */
    int    enabled;      /* Non-zero if breakpoint is active */
    int    hit_count;    /* Number of times this breakpoint fired */
    char  *pattern;      /* Pattern string (may be NULL for line bp) */
    int    line;         /* Line number (0 if not a line bp) */
    char  *condition;    /* Conditional expression (may be NULL) */
} bc_breakpoint_t;

int bc_debug_list_breakpoints(bc_client_t *c,
                              bc_breakpoint_t **bps,
                              int *count);
```

# DESCRIPTION

Retrieves the list of all breakpoints currently set in the
debugger for this session.  The function sends a
`list_breakpoints` request on `CHAN_DEBUG` (channel 4) and
allocates an array of `bc_breakpoint_t` structures with the
server's response.

The caller receives a heap-allocated array through `*bps` and
the number of elements through `*count`.  The caller must free
this array by calling `bc_breakpoint_list_free(*bps, *count)`.

If no breakpoints are set, `*bps` is set to NULL and `*count`
is set to 0.  This is not an error.

**Breakpoint fields:**

| Field | Description |
|-------|-------------|
| `id` | Unique breakpoint ID assigned by the server |
| `kind` | Type string: `"command"`, `"line"`, or `"function"` |
| `enabled` | Non-zero if the breakpoint is currently active |
| `hit_count` | Number of times this breakpoint has been triggered |
| `pattern` | Pattern string for command/function breakpoints (NULL for line) |
| `line` | Line number for line breakpoints (0 for other types) |
| `condition` | Conditional expression string, or NULL if unconditional |

# PARAMETERS

- **c** — Pointer to an authenticated `bc_client_t` connection.
  Must not be NULL.

- **bps** — Output pointer for the breakpoint array.  Must not be
  NULL.  Set to a heap-allocated array on success, or NULL if
  there are no breakpoints.

- **count** — Output pointer for the array length.  Must not be
  NULL.  Set to the number of breakpoints on success.

# RETURN VALUE

Returns `BC_OK` (0) on success, with `*bps` and `*count`
populated.

Returns a negative error code on failure (`*bps` and `*count`
are not modified):

| Code | Meaning |
|------|---------|
| `BC_ERR_PARAM` (-7) | NULL client, bps, or count pointer |
| `BC_ERR_AUTH` (-1) | Client not authenticated |
| `BC_ERR_PROTOCOL` (-2) | Unexpected server response |
| `BC_ERR_TIMEOUT` (-3) | Server did not respond in time |
| `BC_ERR_TRANSPORT` (-4) | Connection error |
| `BC_ERR_NOMEM` (-6) | Memory allocation failed |

# ERRORS

- Returns `BC_ERR_PARAM` if any output pointer is NULL.
- Returns `BC_ERR_NOMEM` if allocation of the array or string
  duplication fails.

# EXAMPLES

```c
#include <bashclient.h>
#include <stdio.h>

void show_breakpoints(bc_client_t *c)
{
    bc_breakpoint_t *bps = NULL;
    int count = 0;

    int rc = bc_debug_list_breakpoints(c, &bps, &count);
    if (rc != BC_OK) {
        fprintf(stderr, "list failed: %d\n", rc);
        return;
    }

    printf("Breakpoints (%d):\n", count);
    for (int i = 0; i < count; i++) {
        printf("  #%d [%s] %s", bps[i].id, bps[i].kind,
               bps[i].enabled ? "enabled" : "disabled");

        if (bps[i].pattern)
            printf(" pattern='%s'", bps[i].pattern);
        if (bps[i].line > 0)
            printf(" line=%d", bps[i].line);
        if (bps[i].condition)
            printf(" if(%s)", bps[i].condition);

        printf(" hits=%d\n", bps[i].hit_count);
    }

    bc_breakpoint_list_free(bps, count);
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");
    bc_debug_enable(c);

    bc_debug_add_breakpoint(c, "command", "echo", -1, NULL);
    bc_debug_add_breakpoint(c, "line", NULL, 10, "[ $x -gt 5 ]");
    bc_debug_add_breakpoint(c, "function", "cleanup", -1, NULL);

    show_breakpoints(c);

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

# SEE ALSO

`bc_breakpoint_list_free`(3), `bc_debug_add_breakpoint`(3),
`bc_debug_remove_breakpoint`(3), `debug_list_breakpoints`(3)

# NOTES

- The returned array is a snapshot of breakpoints at query time.
  Breakpoints may be added or removed between the query and
  subsequent operations.
- The `hit_count` reflects the total number of times the
  breakpoint has fired since it was created, including during
  the current command if execution is stopped at the breakpoint.
- String fields (`kind`, `pattern`, `condition`) are heap-allocated
  copies.  Always use `bc_breakpoint_list_free()` to avoid leaks.
- Thread safety: the client library is not thread-safe.
