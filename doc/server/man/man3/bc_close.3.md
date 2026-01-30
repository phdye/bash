# bc\_close(3) -- Close connection and free client resources

# SYNOPSIS

    #include "bashclient.h"

    void bc_close(bc_client_t *c);

# DESCRIPTION

Closes the connection to the `bash-server(1)` instance and frees all
resources associated with the client handle `c`.

The function performs the following steps in order:

1. Sends a disconnect message on `CHAN_CONTROL` (best-effort; errors are
   ignored since the connection may already be broken).
2. Closes the underlying transport (socket, pipe, or file descriptor).
3. For stdio transport, terminates the child subprocess (`SIGTERM`,
   followed by `SIGKILL` after a grace period if needed).
4. Frees all internal buffers and the handle itself.

After `bc_close` returns, the pointer `c` is invalid and must not be
used.

It is safe to call `bc_close` with a NULL argument; the function returns
immediately without action.

# PARAMETERS

| Parameter | Type            | Description                                   |
|-----------|-----------------|-----------------------------------------------|
| `c`       | `bc_client_t *` | Client handle to close, or NULL.              |

# RETURN VALUE

None (`void`).

# ERRORS

This function does not report errors.  Any errors during the disconnect
message or transport shutdown are silently ignored.

# EXAMPLES

# Basic usage

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
    if (bc_eval(client, "date", &result) == BC_OK) {
        printf("%s", result.stdout_data);
        bc_eval_result_free(&result);
    }

    bc_close(client);
    return 0;
}
```

# Cleanup helper with NULL safety

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    bc_client_t *client;
    char *buffer;
    FILE *logfile;
} app_context_t;

void cleanup(app_context_t *ctx)
{
    /* bc_close is safe to call with NULL */
    bc_close(ctx->client);
    ctx->client = NULL;

    free(ctx->buffer);
    ctx->buffer = NULL;

    if (ctx->logfile) {
        fclose(ctx->logfile);
        ctx->logfile = NULL;
    }
}

int main(void)
{
    app_context_t ctx = {0};

    ctx.client = bc_connect(NULL);
    if (!ctx.client) {
        perror("bc_connect");
        cleanup(&ctx);
        return 1;
    }

    if (bc_auth(ctx.client, getenv("BASH_SERVER_TOKEN")) != BC_OK) {
        fprintf(stderr, "auth: %s\n", bc_error(ctx.client));
        cleanup(&ctx);
        return 1;
    }

    /* ... use the connection ... */

    cleanup(&ctx);
    return 0;
}
```

# Closing a stdio-transport connection

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    const char *argv[] = {"bash-server", "--stdio", NULL};

    bc_client_t *client = bc_connect_stdio(argv);
    if (!client) {
        perror("bc_connect_stdio");
        return 1;
    }

    if (bc_auth(client, getenv("BASH_SERVER_TOKEN")) != BC_OK) {
        fprintf(stderr, "auth: %s\n", bc_error(client));
        bc_close(client);
        return 1;
    }

    bc_eval_result_t result;
    if (bc_eval(client, "echo goodbye", &result) == BC_OK) {
        printf("%s", result.stdout_data);
        bc_eval_result_free(&result);
    }

    /* This terminates the bash-server subprocess */
    bc_close(client);

    /* client pointer is now invalid -- do not use */
    return 0;
}
```

# SEE ALSO

`bc_connect(3)`, `bc_connect_stdio(3)`, `bc_connect_fd(3)`,
`bc_connect_named_pipe(3)`, `bc_free(3)`, `bc_eval_result_free(3)`,
`bash-server(1)`, `bash-server-client-c(7)`

# NOTES

- Always call `bc_close` when done with a connection.  Failing to do so
  leaks file descriptors and memory.  For stdio connections, the server
  subprocess continues running until killed.

- The function sends a best-effort disconnect message to the server so
  that it can clean up the session.  If the connection is already broken,
  the disconnect message is silently dropped.

- For stdio transport, the child process is terminated with `SIGTERM`.
  If the child does not exit within 2 seconds, `SIGKILL` is sent.

- Calling `bc_close` does **not** free any `bc_eval_result_t` structures
  previously returned by `bc_eval(3)`.  Those must be freed separately
  with `bc_eval_result_free(3)`.

- The function is idempotent with respect to NULL: calling `bc_close(NULL)`
  is a no-op.  However, calling `bc_close` twice on the same non-NULL
  handle is undefined behavior (double-free).

- After `bc_close`, do not call `bc_error(3)` on the same handle -- the
  internal error buffer has been freed.
