# bc\_connect\_named\_pipe(3) -- Connect to bash-server via Windows Named Pipe

# SYNOPSIS

    #include "bashclient.h"

    bc_client_t *bc_connect_named_pipe(const char *pipe_name);

# DESCRIPTION

Establishes a connection to a running `bash-server(1)` instance through a
Windows Named Pipe.  This transport is available only on Cygwin and provides
an alternative to Unix domain sockets for inter-process communication on
Windows.

The `pipe_name` parameter specifies the Named Pipe path, which must follow
the Windows Named Pipe naming convention:

    \\.\pipe\<name>

On Cygwin, this is typically written as:

    //./pipe/<name>

If `pipe_name` is NULL, the function attempts to resolve the pipe name from
the server configuration using the standard search order (see
`bc_connect(3)` for details).

The Named Pipe connection uses the server's DACL security descriptor for
access control.  The calling process must have permission to open the pipe
for read/write access.

After a successful connection, the caller must authenticate with
`bc_auth(3)` before issuing any commands.

# PARAMETERS

| Parameter   | Type           | Description                                          |
|-------------|----------------|------------------------------------------------------|
| `pipe_name` | `const char *` | Windows Named Pipe path (e.g., `//./pipe/bash-server`), or NULL for default. |

# RETURN VALUE

Returns a pointer to a newly allocated `bc_client_t` handle on success.

Returns **NULL** on error.  Check `errno` for the cause:

| errno          | Meaning                                        |
|----------------|------------------------------------------------|
| `ENOENT`       | The named pipe does not exist.                 |
| `ECONNREFUSED` | The server is not listening on the pipe.       |
| `EACCES`       | Access denied by the pipe's security descriptor.|
| `ENOMEM`       | Memory allocation failed.                      |
| `ENOSYS`       | Named Pipes are not supported on this platform. |

# ERRORS

The function may fail when:

- The named pipe does not exist or the server is not running.
- The calling process does not have the required DACL permissions.
- The pipe is in a busy state and all instances are connected.
- `malloc(3)` fails to allocate the client handle.
- The function is called on a non-Cygwin platform (returns `ENOSYS`).

# EXAMPLES

# Connect to a Named Pipe server

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    const char *pipe = "//./pipe/bash-server";
    bc_client_t *client;

    client = bc_connect_named_pipe(pipe);
    if (!client) {
        perror("bc_connect_named_pipe");
        return 1;
    }

    if (bc_auth(client, getenv("BASH_SERVER_TOKEN")) != BC_OK) {
        fprintf(stderr, "auth: %s\n", bc_error(client));
        bc_close(client);
        return 1;
    }

    bc_eval_result_t result;
    if (bc_eval(client, "cygpath -w /home", &result) == BC_OK) {
        printf("Windows path: %s", result.stdout_data);
        bc_eval_result_free(&result);
    }

    bc_close(client);
    return 0;
}
```

# Connect with custom pipe name

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    const char *pipe_name;
    bc_client_t *client;

    if (argc > 1)
        pipe_name = argv[1];
    else
        pipe_name = "//./pipe/bash-server-myapp";

    client = bc_connect_named_pipe(pipe_name);
    if (!client) {
        fprintf(stderr, "bc_connect_named_pipe(%s): %s\n",
                pipe_name, strerror(errno));
        return 1;
    }

    if (bc_auth(client, getenv("BASH_SERVER_TOKEN")) != BC_OK) {
        fprintf(stderr, "auth failed: %s\n", bc_error(client));
        bc_close(client);
        return 1;
    }

    /* Run multiple commands */
    const char *commands[] = {"pwd", "echo $SHELL", "id", NULL};
    for (int i = 0; commands[i]; i++) {
        bc_eval_result_t result;
        if (bc_eval(client, commands[i], &result) == BC_OK) {
            printf("$ %s\n%s", commands[i], result.stdout_data);
            bc_eval_result_free(&result);
        } else {
            fprintf(stderr, "eval failed: %s\n", bc_error(client));
        }
    }

    bc_close(client);
    return 0;
}
```

# SEE ALSO

`bc_connect(3)`, `bc_connect_stdio(3)`, `bc_connect_fd(3)`,
`bc_auth(3)`, `bc_close(3)`, `bc_error(3)`,
`server_winpipe_create(3)`, `server_winpipe_accept(3)`,
`bash-server(1)`, `bash-server-client-c(7)`,
`bash-server-transports(7)`

# NOTES

- This function is only available on Cygwin.  On other platforms, the
  function returns NULL with `errno` set to `ENOSYS`.  Use `bc_connect(3)`
  for portable Unix domain socket connections.

- The Named Pipe transport uses the same protocol as the Unix socket
  transport.  After connection, all API functions (`bc_auth`, `bc_eval`,
  etc.) work identically regardless of transport.

- The server must have been started with `--named-pipe <name>` to accept
  Named Pipe connections.

- Windows Named Pipes have a maximum instance count configured on the
  server side.  If all instances are busy, the connection will fail with
  `ECONNREFUSED`.  Retry after a short delay if needed.

- The pipe path uses forward slashes on Cygwin (`//./pipe/name`) rather
  than backslashes (`\\.\pipe\name`).  Both forms are accepted.

- Named Pipe security is governed by a DACL set by the server at pipe
  creation time.  By default, only the same user can connect.  See
  `bash-server-security(7)` for details.
