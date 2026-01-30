# bc\_eval\_result\_free(3) -- Free strings in an eval result structure

# SYNOPSIS

    #include "bashclient.h"

    void bc_eval_result_free(bc_eval_result_t *r);

# DESCRIPTION

Frees the heap-allocated strings in a `bc_eval_result_t` structure that
was populated by `bc_eval(3)`.  Specifically, it frees `r->stdout_data`
and `r->stderr_data` using `bc_free(3)` and sets both pointers to NULL.

The function does **not** free the `bc_eval_result_t` structure itself,
since it is typically stack-allocated.

It is safe to call `bc_eval_result_free` with a NULL argument; the
function returns immediately without action.  It is also safe to call it
on a zero-initialized structure (both fields NULL).

The `bc_eval_result_t` structure:

```c
typedef struct {
    char *stdout_data;   /* malloc'd stdout output (NUL-terminated) */
    char *stderr_data;   /* malloc'd stderr output (NUL-terminated) */
    int   exit_code;     /* command exit status ($?) */
} bc_eval_result_t;
```

# PARAMETERS

| Parameter | Type                 | Description                                    |
|-----------|----------------------|------------------------------------------------|
| `r`       | `bc_eval_result_t *` | Pointer to the result structure to clean up, or NULL. |

# RETURN VALUE

None (`void`).

# ERRORS

This function does not report errors.  Passing a structure whose fields
were not allocated by `bc_eval(3)` results in undefined behavior.

# EXAMPLES

# Basic usage after bc_eval

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
    if (bc_eval(client, "date +%Y-%m-%d", &result) == BC_OK) {
        printf("today: %s", result.stdout_data);
        bc_eval_result_free(&result);
        /* result.stdout_data is now NULL */
        /* result.stderr_data is now NULL */
    }

    bc_close(client);
    return 0;
}
```

# Freeing in a loop

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

    const char *commands[] = {
        "hostname",
        "uptime",
        "df -h /",
        "free -m",
        NULL
    };

    for (int i = 0; commands[i]; i++) {
        bc_eval_result_t result;
        if (bc_eval(client, commands[i], &result) == BC_OK) {
            printf("=== %s ===\n%s", commands[i], result.stdout_data);
            bc_eval_result_free(&result);
        } else {
            fprintf(stderr, "%s failed: %s\n",
                    commands[i], bc_error(client));
        }
    }

    bc_close(client);
    return 0;
}
```

# Safe cleanup on error paths

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char *get_server_hostname(bc_client_t *client)
{
    bc_eval_result_t result = {0};  /* zero-init for safety */

    if (bc_eval(client, "hostname -f", &result) != BC_OK)
        return NULL;

    if (result.exit_code != 0) {
        bc_eval_result_free(&result);
        return NULL;
    }

    /* Take ownership of stdout_data */
    char *hostname = result.stdout_data;
    result.stdout_data = NULL;  /* prevent double-free */

    /* Strip trailing newline */
    size_t len = strlen(hostname);
    if (len > 0 && hostname[len-1] == '\n')
        hostname[len-1] = '\0';

    /* Free the rest (stderr_data) */
    bc_eval_result_free(&result);

    return hostname;  /* caller frees with bc_free */
}

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

    char *host = get_server_hostname(client);
    if (host) {
        printf("server hostname: %s\n", host);
        bc_free(host);
    }

    bc_close(client);
    return 0;
}
```

# Double-call safety

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

void example(bc_client_t *client)
{
    bc_eval_result_t result;

    if (bc_eval(client, "echo test", &result) == BC_OK) {
        printf("%s", result.stdout_data);
        bc_eval_result_free(&result);

        /*
         * Safe to call again -- stdout_data and stderr_data
         * are now NULL, and bc_free(NULL) is a no-op.
         */
        bc_eval_result_free(&result);
    }

    /* Also safe with NULL */
    bc_eval_result_free(NULL);
}
```

# SEE ALSO

`bc_eval(3)`, `bc_free(3)`, `bc_close(3)`,
`bash-server-client-c(7)`

# NOTES

- The function sets `stdout_data` and `stderr_data` to NULL after freeing
  them, making it safe to call `bc_eval_result_free` multiple times on the
  same structure.

- The `exit_code` field is not modified by this function.  It remains
  valid after the call (though typically no longer useful).

- The structure itself is not freed.  If you heap-allocate
  `bc_eval_result_t` (unusual), you must free the struct separately after
  calling `bc_eval_result_free`.

- If you need to retain one of the output strings beyond the result's
  lifetime, set the pointer to NULL before calling `bc_eval_result_free`
  to prevent it from being freed.  You are then responsible for freeing
  it later with `bc_free(3)`.

- On error paths from `bc_eval(3)`, the result fields are already set to
  NULL / 0, so calling `bc_eval_result_free` is safe but unnecessary.

- This function is equivalent to:

  ```c
  bc_free(r->stdout_data);  r->stdout_data = NULL;
  bc_free(r->stderr_data);  r->stderr_data = NULL;
  ```
