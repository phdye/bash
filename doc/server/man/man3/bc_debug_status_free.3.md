# bc\_debug\_status\_free(3) — free a debug status structure

# SYNOPSIS

```c
#include <bashclient.h>

typedef struct {
    int    active;
    char  *mode;
    int    breakpoints;
    int    depth;
} bc_debug_status_t;

void bc_debug_status_free(bc_debug_status_t *s);
```

# DESCRIPTION

Frees the heap-allocated strings inside a `bc_debug_status_t`
structure that was populated by `bc_debug_status()`.  Specifically,
this function frees the `mode` string and sets it to NULL.

The function does **not** free the `bc_debug_status_t` structure
itself, as it is typically stack-allocated by the caller.

It is safe to call `bc_debug_status_free()` on a zero-initialized
structure or on a structure that has already been freed (the
function checks for NULL before freeing).

# PARAMETERS

- **s** — Pointer to a `bc_debug_status_t` structure to clean up.
  If NULL, the function is a no-op.

# RETURN VALUE

None.

# ERRORS

This function does not report errors.  A NULL parameter is
silently ignored.

# EXAMPLES

# Stack-allocated status

```c
#include <bashclient.h>
#include <stdio.h>

void check_mode(bc_client_t *c)
{
    bc_debug_status_t st = {0};  /* Zero-init for safety */

    if (bc_debug_status(c, &st) == BC_OK) {
        printf("mode: %s\n", st.mode);
        bc_debug_status_free(&st);
    }
    /* After free: st.mode == NULL, other fields unchanged */
}
```

# Safe double-free

```c
#include <bashclient.h>

void example(bc_client_t *c)
{
    bc_debug_status_t st;
    bc_debug_status(c, &st);

    /* First free */
    bc_debug_status_free(&st);

    /* Second free is safe — mode is already NULL */
    bc_debug_status_free(&st);
}
```

# Conditional cleanup pattern

```c
#include <bashclient.h>
#include <stdio.h>
#include <string.h>

int is_stepping(bc_client_t *c)
{
    bc_debug_status_t st;
    int stepping = 0;

    if (bc_debug_status(c, &st) == BC_OK) {
        stepping = (strcmp(st.mode, "step") == 0 ||
                    strcmp(st.mode, "next") == 0);
        bc_debug_status_free(&st);
    }

    return stepping;
}
```

# SEE ALSO

`bc_debug_status`(3), `bc_breakpoint_list_free`(3)

# NOTES

- Always pair `bc_debug_status()` with `bc_debug_status_free()`
  to avoid memory leaks.
- The function only frees the `mode` string.  The integer fields
  (`active`, `breakpoints`, `depth`) require no cleanup.
- After calling `bc_debug_status_free()`, the `mode` pointer is
  set to NULL.  The structure can be safely reused with another
  `bc_debug_status()` call.
- The function follows the convention of other `_free` functions
  in the library: NULL pointers are silently ignored, and freed
  pointers are set to NULL for double-free safety.
