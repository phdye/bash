# bc\_breakpoint\_list\_free(3) — free a breakpoint array

# SYNOPSIS

```c
#include <bashclient.h>

typedef struct {
    int    id;
    char  *kind;
    int    enabled;
    int    hit_count;
    char  *pattern;
    int    line;
    char  *condition;
} bc_breakpoint_t;

void bc_breakpoint_list_free(bc_breakpoint_t *bps, int count);
```

# DESCRIPTION

Frees a breakpoint array returned by `bc_debug_list_breakpoints()`.
For each element in the array, the function frees the heap-allocated
string fields (`kind`, `pattern`, `condition`), then frees the array
itself.

It is safe to call this function with `bps` set to NULL (the
function is a no-op in that case), which handles the empty
breakpoint list case from `bc_debug_list_breakpoints()`.

The function does **not** set the caller's pointer to NULL; the
caller should do so explicitly if needed.

# PARAMETERS

- **bps** — Pointer to a `bc_breakpoint_t` array, or NULL.  If
  non-NULL, must have been allocated by
  `bc_debug_list_breakpoints()`.

- **count** — Number of elements in the array.  Ignored if `bps`
  is NULL.  Must be >= 0.

# RETURN VALUE

None.

# ERRORS

This function does not report errors.  A NULL `bps` pointer is
silently accepted.

# EXAMPLES

# Standard usage pattern

```c
#include <bashclient.h>
#include <stdio.h>

void print_breakpoints(bc_client_t *c)
{
    bc_breakpoint_t *bps = NULL;
    int count = 0;

    if (bc_debug_list_breakpoints(c, &bps, &count) != BC_OK)
        return;

    for (int i = 0; i < count; i++)
        printf("#%d %s\n", bps[i].id, bps[i].kind);

    bc_breakpoint_list_free(bps, count);
    bps = NULL;  /* Optional: prevent dangling pointer */
}
```

# Empty list is safe

```c
#include <bashclient.h>

void example(void)
{
    /* NULL pointer with count=0 is safe */
    bc_breakpoint_list_free(NULL, 0);

    /* Also safe with any count when bps is NULL */
    bc_breakpoint_list_free(NULL, 42);
}
```

# Find and remove pattern

```c
#include <bashclient.h>
#include <string.h>

/* Remove all breakpoints matching a pattern */
int remove_matching(bc_client_t *c, const char *match)
{
    bc_breakpoint_t *bps = NULL;
    int count = 0, removed = 0;

    if (bc_debug_list_breakpoints(c, &bps, &count) != BC_OK)
        return -1;

    for (int i = 0; i < count; i++) {
        if (bps[i].pattern && strstr(bps[i].pattern, match)) {
            bc_debug_remove_breakpoint(c, bps[i].id);
            removed++;
        }
    }

    bc_breakpoint_list_free(bps, count);
    return removed;
}
```

# SEE ALSO

`bc_debug_list_breakpoints`(3), `bc_debug_status_free`(3),
`bc_debug_add_breakpoint`(3)

# NOTES

- Always pair `bc_debug_list_breakpoints()` with
  `bc_breakpoint_list_free()` to avoid memory leaks.
- The function frees each string field individually (using
  `free()`) before freeing the array.  NULL string fields
  (e.g., `pattern` for line breakpoints, `condition` for
  unconditional breakpoints) are skipped safely.
- After calling this function, the `bps` pointer is invalid.
  Do not access any element or field after freeing.
- The integer fields (`id`, `enabled`, `hit_count`, `line`)
  require no cleanup.
