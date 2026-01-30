# bc\_error(3) -- Get last error message from client handle

# SYNOPSIS

    #include "bashclient.h"

    const char *bc_error(bc_client_t *c);

# DESCRIPTION

Returns a human-readable error message describing the last error that
occurred on the client handle `c`.  The returned string provides diagnostic
detail beyond what the numeric error codes (`BC_ERR_*`) convey.

The returned pointer refers to an internal static buffer within the client
handle.  The caller must NOT free it.  The contents of the buffer are
overwritten on the next API call that modifies error state on the same
handle.

If no error has occurred, the function returns an empty string (`""`),
not NULL.

# PARAMETERS

| Parameter | Type            | Description                                 |
|-----------|-----------------|---------------------------------------------|
| `c`       | `bc_client_t *` | Client handle to query.  Must not be NULL.  |

# RETURN VALUE

Returns a pointer to a NUL-terminated string describing the last error.

Returns an empty string (`""`) if no error has occurred.

The returned pointer is valid until the next API call on the same handle
or until `bc_close(3)` is called.  Do NOT free the returned pointer.

# ERRORS

This function does not fail.  If `c` is NULL, behavior is undefined.

# EXAMPLES

# Displaying error messages

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    bc_client_t *client = bc_connect("/nonexistent/path");
    if (!client) {
        /* bc_error requires a handle -- use perror for connect failures */
        perror("bc_connect");
        return 1;
    }

    int rc = bc_auth(client, "wrong_token");
    if (rc != BC_OK) {
        /* bc_error provides detailed server response */
        fprintf(stderr, "auth failed (rc=%d): %s\n", rc, bc_error(client));
        bc_close(client);
        return 1;
    }

    bc_close(client);
    return 0;
}
```

# Logging errors for diagnostics

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

void log_error(bc_client_t *client, const char *operation, int rc)
{
    time_t now = time(NULL);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S",
             localtime(&now));

    fprintf(stderr, "[%s] %s failed (rc=%d): %s\n",
            timebuf, operation, rc, bc_error(client));
}

int main(void)
{
    bc_client_t *client = bc_connect(NULL);
    if (!client) {
        perror("bc_connect");
        return 1;
    }

    int rc = bc_auth(client, getenv("BASH_SERVER_TOKEN"));
    if (rc != BC_OK) {
        log_error(client, "bc_auth", rc);
        bc_close(client);
        return 1;
    }

    bc_eval_result_t result;
    rc = bc_eval(client, "exit 1", &result);
    if (rc != BC_OK) {
        log_error(client, "bc_eval", rc);
    } else {
        printf("exit_code=%d\n", result.exit_code);
        bc_eval_result_free(&result);
    }

    bc_close(client);
    return 0;
}
```

# Checking for no error

```c
#include "bashclient.h"
#include <stdio.h>
#include <string.h>

void check_status(bc_client_t *client)
{
    const char *err = bc_error(client);

    if (err[0] == '\0') {
        printf("no errors\n");
    } else {
        printf("last error: %s\n", err);
    }
}
```

# Copy error before next API call

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
    bc_client_t *client = bc_connect(NULL);
    if (!client) {
        perror("bc_connect");
        return 1;
    }

    if (bc_auth(client, getenv("BASH_SERVER_TOKEN")) != BC_OK) {
        /*
         * Copy the error string before calling bc_close,
         * which invalidates the internal buffer.
         */
        char *saved_error = strdup(bc_error(client));
        bc_close(client);
        fprintf(stderr, "auth: %s\n", saved_error);
        free(saved_error);
        return 1;
    }

    bc_close(client);
    return 0;
}
```

# SEE ALSO

`bc_auth(3)`, `bc_eval(3)`, `bc_ping(3)`, `bc_close(3)`,
`bash-server-client-c(7)`

# NOTES

- The returned pointer is to an internal buffer that is overwritten by the
  next API call on the same handle.  If you need to preserve the error
  message across calls, copy it with `strdup(3)`.

- `bc_error` returns an empty string (not NULL) when no error has occurred.
  This makes it safe to pass directly to `printf` or `fprintf` without a
  NULL check.

- The error buffer is part of the client handle and is freed by
  `bc_close(3)`.  Do not use the pointer after closing the handle.

- For connection functions that return NULL (e.g., `bc_connect`), there is
  no handle to query.  Use `errno` and `strerror(3)` instead.

- The error message format is not standardized and may change between
  library versions.  Do not parse the string programmatically -- use the
  numeric return codes for control flow.

- The buffer size is fixed internally.  Very long server error messages
  may be truncated.
