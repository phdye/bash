# bc\_debug\_status(3) — query debugger state

# SYNOPSIS

```c
#include <bashclient.h>

typedef struct {
    int    active;       /* Non-zero if debugger is enabled */
    char  *mode;         /* Current mode: "run", "step", "next", "finish" */
    int    breakpoints;  /* Number of active breakpoints */
    int    depth;        /* Current execution nesting depth */
} bc_debug_status_t;

int bc_debug_status(bc_client_t *c, bc_debug_status_t *status);
```

# DESCRIPTION

Queries the current state of the debugger for this session.  The
function sends a `status` request on `CHAN_DEBUG` (channel 4) and
populates the caller-provided `bc_debug_status_t` structure with
the server's response.

**Status fields:**

| Field | Description |
|-------|-------------|
| `active` | Non-zero if the debugger is currently enabled |
| `mode` | Current execution mode string (heap-allocated) |
| `breakpoints` | Total number of breakpoints currently set |
| `depth` | Nesting depth of the currently executing command (0 = top level) |

**Execution modes:**

| Mode | Description |
|------|-------------|
| `"run"` | Normal execution, stopping only at breakpoints |
| `"step"` | Single-stepping into subcommands |
| `"next"` | Stepping at the same nesting depth |
| `"finish"` | Running until current function/block returns |

The caller must free the status structure by calling
`bc_debug_status_free()` after use.

# PARAMETERS

- **c** — Pointer to an authenticated `bc_client_t` connection.
  Must not be NULL.

- **status** — Pointer to a `bc_debug_status_t` structure to
  populate.  Must not be NULL.  The `mode` field is
  heap-allocated and must be freed via `bc_debug_status_free()`.

# RETURN VALUE

Returns `BC_OK` (0) on success, with `status` populated.

Returns a negative error code on failure (status is not modified):

| Code | Meaning |
|------|---------|
| `BC_ERR_PARAM` (-7) | NULL client or status pointer |
| `BC_ERR_AUTH` (-1) | Client not authenticated |
| `BC_ERR_PROTOCOL` (-2) | Unexpected server response |
| `BC_ERR_TIMEOUT` (-3) | Server did not respond in time |
| `BC_ERR_TRANSPORT` (-4) | Connection error |
| `BC_ERR_NOMEM` (-6) | Memory allocation failed |

# ERRORS

- Returns `BC_ERR_PARAM` if either `c` or `status` is NULL.
- Returns `BC_ERR_NOMEM` if `strdup()` of the mode string fails.

# EXAMPLES

```c
#include <bashclient.h>
#include <stdio.h>

void show_debug_status(bc_client_t *c)
{
    bc_debug_status_t st;

    int rc = bc_debug_status(c, &st);
    if (rc != BC_OK) {
        fprintf(stderr, "status query failed: %d\n", rc);
        return;
    }

    printf("Debugger: %s\n", st.active ? "active" : "inactive");
    if (st.active) {
        printf("  Mode:        %s\n", st.mode);
        printf("  Breakpoints: %d\n", st.breakpoints);
        printf("  Depth:       %d\n", st.depth);
    }

    bc_debug_status_free(&st);
}

int main(void)
{
    bc_client_t *c = bc_connect("/tmp/bash-server-1000/sock", NULL);
    if (!c) return 1;
    bc_auth(c, "mytoken");

    /* Status before enabling */
    show_debug_status(c);

    bc_debug_enable(c);
    bc_debug_add_breakpoint(c, "command", "echo", -1, NULL);

    /* Status after enabling with breakpoint */
    show_debug_status(c);

    bc_debug_disable(c);
    bc_disconnect(c);
    return 0;
}
```

# SEE ALSO

`bc_debug_status_free`(3), `bc_debug_enable`(3),
`bc_debug_disable`(3), `bc_debug_list_breakpoints`(3)

# NOTES

- The `mode` string is heap-allocated via `strdup()`.  Always
  call `bc_debug_status_free()` to avoid memory leaks.
- When the debugger is not active, `mode` is set to `"run"`,
  `breakpoints` is 0, and `depth` is 0.
- The `depth` field reflects the nesting level at the point the
  status was queried.  If execution is stopped at a breakpoint,
  this is the depth of the stopped command.
- Thread safety: the client library is not thread-safe.
