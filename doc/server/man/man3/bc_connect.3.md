# bc\_connect(3) -- Connect to bash-server via Unix domain socket

# SYNOPSIS

    #include "bashclient.h"

    bc_client_t *bc_connect(const char *socket_path);

# DESCRIPTION

Establishes a connection to a running `bash-server(1)` instance through a
Unix domain socket.  The function creates a `SOCK_STREAM` socket, connects
to the path specified by `socket_path`, and returns an opaque client handle
suitable for use with `bc_auth(3)`, `bc_eval(3)`, and other API functions.

The returned handle must eventually be released with `bc_close(3)`.

If `socket_path` is NULL, the function attempts to resolve the server
socket using the standard search order:

1. `$BASH_SERVER_SOCKET` environment variable
2. `~/.bash-serverrc` configuration file
3. `$XDG_RUNTIME_DIR/bash-server/sock`
4. `/tmp/bash-server-<uid>/sock`

After a successful connection, the caller must authenticate with
`bc_auth(3)` before issuing any commands.

# PARAMETERS

| Parameter     | Type           | Description                                    |
|---------------|----------------|------------------------------------------------|
| `socket_path` | `const char *` | Path to the server's Unix domain socket, or NULL to use the default search order. |

# RETURN VALUE

Returns a pointer to a newly allocated `bc_client_t` handle on success.

Returns **NULL** on error.  Call `errno` for the system error code, or use
`bc_error(3)` on a previously obtained handle.  Common `errno` values:

| errno          | Meaning                                       |
|----------------|-----------------------------------------------|
| `ENOENT`       | Socket path does not exist.                   |
| `ECONNREFUSED` | Server is not listening on the socket.        |
| `EACCES`       | Permission denied on the socket file.         |
| `ENOMEM`       | Memory allocation failed.                     |
| `ENAMETOOLONG` | Socket path exceeds `sizeof(sun_path)` limit. |

# ERRORS

The function may fail when:

- The `socket_path` exceeds the maximum length for a Unix domain socket
  address (`sizeof(struct sockaddr_un.sun_path) - 1`, typically 107 bytes).
- The socket file does not exist or the server is not running.
- The calling process lacks permission to access the socket file.
- `malloc(3)` fails to allocate the client handle.
- `socket(2)` or `connect(2)` fails for any system-level reason.
- No default socket path can be resolved (when `socket_path` is NULL).

# EXAMPLES

# Basic connection

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    bc_client_t *client;

    /* Connect using default socket path resolution */
    client = bc_connect(NULL);
    if (!client) {
        perror("bc_connect");
        return 1;
    }

    /* Authenticate before issuing commands */
    if (bc_auth(client, getenv("BASH_SERVER_TOKEN")) != BC_OK) {
        fprintf(stderr, "auth failed: %s\n", bc_error(client));
        bc_close(client);
        return 1;
    }

    /* ... use the connection ... */

    bc_close(client);
    return 0;
}
```

# Connection with explicit socket path

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    const char *sock = "/tmp/bash-server-1000/sock";
    bc_client_t *client;

    if (argc > 1)
        sock = argv[1];

    client = bc_connect(sock);
    if (!client) {
        fprintf(stderr, "bc_connect(%s): %s\n", sock, strerror(errno));
        return 1;
    }

    printf("connected to %s\n", sock);

    /* Authenticate */
    if (bc_auth(client, getenv("BASH_SERVER_TOKEN")) != BC_OK) {
        fprintf(stderr, "auth: %s\n", bc_error(client));
        bc_close(client);
        return 1;
    }

    /* Evaluate a command */
    bc_eval_result_t result;
    if (bc_eval(client, "echo hello", &result) == BC_OK) {
        printf("stdout: %s\n", result.stdout_data);
        printf("exit:   %d\n", result.exit_code);
        bc_eval_result_free(&result);
    }

    bc_close(client);
    return 0;
}
```

# Connection with retry loop

```c
#include "bashclient.h"
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

bc_client_t *connect_with_retry(const char *path, int max_attempts)
{
    bc_client_t *client;

    for (int i = 0; i < max_attempts; i++) {
        client = bc_connect(path);
        if (client)
            return client;

        if (errno != ECONNREFUSED && errno != ENOENT)
            break;  /* non-retryable error */

        fprintf(stderr, "attempt %d/%d: %s\n",
                i + 1, max_attempts, strerror(errno));
        sleep(1);
    }

    return NULL;
}
```

# SEE ALSO

`bc_connect_stdio(3)`, `bc_connect_fd(3)`, `bc_connect_named_pipe(3)`,
`bc_auth(3)`, `bc_close(3)`, `bc_error(3)`,
`bash-server(1)`, `bash-server-client-c(7)`

# NOTES

- The client handle is not thread-safe.  Each thread should use its own
  connection.

- The function performs a blocking `connect(2)` call.  If the server
  accept queue is full, the call may block until a slot is available or
  the connection is refused.

- The maximum socket path length is determined by `sun_path` in
  `struct sockaddr_un`, which is typically 108 bytes (107 usable characters
  plus the NUL terminator) on Linux/Cygwin.

- After `bc_connect` returns successfully, the connection is in an
  unauthenticated state.  All commands except `bc_auth(3)` will fail
  with `BC_ERR_AUTH` until authentication succeeds.

- The caller is responsible for ensuring the server is running before
  calling `bc_connect`.  Use `bc_connect_stdio(3)` to automatically
  launch a server subprocess.
