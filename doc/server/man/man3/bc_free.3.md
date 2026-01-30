# bc\_free(3) -- Free a buffer returned by the client API

# SYNOPSIS

    #include "bashclient.h"

    void bc_free(void *ptr);

# DESCRIPTION

Frees a memory buffer that was allocated and returned by the
`libbashclient` API.  This function is equivalent to `free(3)` but is
provided for API consistency and to ensure that memory allocated by the
library is freed by the same allocator.

This is particularly important on Windows/Cygwin where different DLLs may
use different C runtime heap instances.  Using `bc_free` guarantees that the
correct heap is used regardless of how the library is linked.

It is safe to call `bc_free` with a NULL argument; the function returns
immediately without action.

# PARAMETERS

| Parameter | Type     | Description                                         |
|-----------|----------|-----------------------------------------------------|
| `ptr`     | `void *` | Pointer to memory allocated by the API, or NULL.    |

# RETURN VALUE

None (`void`).

# ERRORS

This function does not report errors.  Passing a pointer not allocated by
the library results in undefined behavior (same as `free(3)` with an
invalid pointer).

# EXAMPLES

# Freeing a string from the API

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    bc_client_t *client = bc_connect(NULL);
    if (!client) {
        perror("bc_connect");
        return 1;
    }

    if (bc_auth(client, getenv("BASH_SERVER_TOKEN")) != BC_OK) {
        fprintf(stderr, "auth: %s\n", bc_error(client));
        bc_close(client);
        return 1;
    }

    bc_eval_result_t result;
    if (bc_eval(client, "echo hello", &result) == BC_OK) {
        printf("%s", result.stdout_data);

        /* Free individual fields with bc_free */
        bc_free(result.stdout_data);
        bc_free(result.stderr_data);

        /* Or equivalently, use bc_eval_result_free */
    }

    bc_close(client);
    return 0;
}
```

# NULL safety

```c
#include "bashclient.h"

void example(void)
{
    void *ptr = NULL;

    /* Safe -- no-op when ptr is NULL */
    bc_free(ptr);
    bc_free(NULL);
}
```

# Correct usage in cleanup paths

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int process_commands(bc_client_t *client, const char **commands, int count)
{
    for (int i = 0; i < count; i++) {
        bc_eval_result_t result = {0};
        int rc = bc_eval(client, commands[i], &result);

        if (rc != BC_OK) {
            fprintf(stderr, "command failed: %s\n", bc_error(client));
            /* result fields may be partially set -- safe to free */
            bc_free(result.stdout_data);
            bc_free(result.stderr_data);
            return rc;
        }

        printf("$ %s\n%s", commands[i], result.stdout_data);

        if (result.stderr_data && result.stderr_data[0])
            fprintf(stderr, "%s", result.stderr_data);

        bc_free(result.stdout_data);
        bc_free(result.stderr_data);
    }

    return BC_OK;
}
```

# SEE ALSO

`bc_eval(3)`, `bc_eval_result_free(3)`, `bc_close(3)`,
`free(3)`, `bash-server-client-c(7)`

# NOTES

- For `bc_eval_result_t` structures, prefer `bc_eval_result_free(3)` which
  frees both `stdout_data` and `stderr_data` in one call and sets the
  pointers to NULL.  Use `bc_free` directly only when you need to free
  individual fields or other API-returned buffers.

- The `bc_error(3)` function returns a pointer to an internal static buffer
  that must NOT be freed with `bc_free`.

- On platforms where the library and application share the same C runtime
  heap (typical on Linux/Cygwin with static linking), `bc_free` and
  `free(3)` are interchangeable.  However, using `bc_free` is recommended
  for portability.

- Like `free(3)`, calling `bc_free` on a pointer that was not returned by
  the library, or calling it twice on the same pointer, results in
  undefined behavior.
