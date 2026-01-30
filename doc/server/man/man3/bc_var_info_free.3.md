# bc\_var\_info\_free(3) — free strings in a variable info structure

# SYNOPSIS

```c
#include "bashclient.h"

typedef struct {
    char  *name;
    char  *value;
    char **attributes;
    int    num_attributes;
} bc_var_info_t;

void bc_var_info_free(bc_var_info_t *info);
```

# DESCRIPTION

Releases all dynamically allocated memory within a `bc_var_info_t`
structure that was previously populated by `bc_state_get_var()`.

The function performs the following operations in order:

1. Frees `info->name` via `free()` and sets it to NULL.
2. Frees `info->value` via `free()` and sets it to NULL.
3. Frees each string in the `info->attributes` array
   (`info->attributes[0]` through
   `info->attributes[info->num_attributes - 1]`).
4. Frees the `info->attributes` array itself and sets it to NULL.
5. Sets `info->num_attributes` to 0.

The function does **not** free the `bc_var_info_t` structure
itself.  This allows the structure to be stack-allocated or
embedded in a larger structure, which is the typical usage
pattern.

If **info** is NULL, the function returns immediately without
action.  If any individual pointer within the structure is already
NULL, it is safely skipped.

# PARAMETERS

| Parameter | Type | Description |
|-----------|------|-------------|
| `info` | `bc_var_info_t *` | Pointer to the structure whose contents should be freed. May be NULL (no-op). The structure itself is not freed. |

# RETURN VALUE

None.

# ERRORS

This function cannot fail.  It is safe to call with a NULL
pointer or with a structure whose fields are already NULL.

# EXAMPLES

# Basic usage with stack-allocated struct

```c
bc_var_info_t info;
int rc = bc_state_get_var(client, "HOME", &info);
if (rc == BC_OK) {
    printf("HOME = %s\n", info.value);
    bc_var_info_free(&info);
}
/* info.name, info.value, info.attributes are now NULL */
```

# Safe double-free prevention

```c
bc_var_info_t info;
int rc = bc_state_get_var(client, "SHELL", &info);
if (rc == BC_OK) {
    bc_var_info_free(&info);
    /* Safe to call again — all pointers are NULL */
    bc_var_info_free(&info);
}
```

# Usage in a loop

```c
const char *vars[] = { "HOME", "PATH", "SHELL", "USER", NULL };
bc_var_info_t info;

for (int i = 0; vars[i]; i++) {
    int rc = bc_state_get_var(client, vars[i], &info);
    if (rc == BC_OK) {
        printf("%s=%s\n", info.name, info.value);
        bc_var_info_free(&info);
    }
}
```

# Heap-allocated struct

```c
bc_var_info_t *info = calloc(1, sizeof(*info));
int rc = bc_state_get_var(client, "TERM", info);
if (rc == BC_OK) {
    printf("TERM=%s\n", info->value);
    bc_var_info_free(info);  /* frees contents, not the struct */
}
free(info);  /* caller frees the struct itself */
```

# SEE ALSO

`bc_state_get_var`(3), `bc_state_set_var`(3),
`bc_state_unset_var`(3), `bc_free`(3),
`bc_connect`(3), `bash-server-client-c`(7),
`bash-server-channels`(7)

# NOTES

- Always use `bc_var_info_free()` rather than manually freeing
  individual fields.  The function ensures all pointers are
  NULLed out, preventing use-after-free and double-free bugs.
- After calling `bc_var_info_free()`, the structure can be
  safely reused for another `bc_state_get_var()` call without
  re-initialization.
- The strings within `bc_var_info_t` are allocated with standard
  `malloc()`.  Do not use `bc_free()` on them individually; use
  this function instead.
- This function is not thread-safe if multiple threads access
  the same `bc_var_info_t` concurrently.  Each thread should
  use its own structure.
