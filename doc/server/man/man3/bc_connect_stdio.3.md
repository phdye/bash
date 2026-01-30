# bc\_connect\_stdio(3) -- Connect to bash-server via stdio subprocess

# SYNOPSIS

    #include "bashclient.h"

    bc_client_t *bc_connect_stdio(const char *const argv[]);

# DESCRIPTION

Launches a `bash-server(1)` subprocess and connects to it via standard
input/output pipes.  The `argv` array specifies the command and arguments
to execute; it must be NULL-terminated.

The function forks and executes the specified command using `execvp(3)`,
redirecting the child's stdin and stdout through a pipe pair.  The child
process is expected to speak the bash-server protocol on its stdio
(i.e., the server should be invoked with `--stdio`).

This transport mode is useful for:

- Launching a server on demand without a pre-existing socket.
- Running the server over SSH (`{"ssh", "host", "bash-server", "--stdio", NULL}`).
- Embedding bash-server in a parent process without socket setup.

The returned handle must eventually be released with `bc_close(3)`, which
also terminates the child subprocess.

# PARAMETERS

| Parameter | Type                  | Description                                      |
|-----------|-----------------------|--------------------------------------------------|
| `argv`    | `const char *const *` | NULL-terminated argument array for the server process. The first element is the program to execute. |

# RETURN VALUE

Returns a pointer to a newly allocated `bc_client_t` handle on success.

Returns **NULL** on error.  Check `errno` for the cause:

| errno      | Meaning                                       |
|------------|-----------------------------------------------|
| `EINVAL`   | `argv` is NULL or `argv[0]` is NULL.          |
| `ENOMEM`   | Memory allocation failed.                     |
| `EMFILE`   | Too many open file descriptors.               |
| `ENOENT`   | The program specified in `argv[0]` was not found. |

# ERRORS

The function may fail when:

- `argv` is NULL or empty.
- `pipe(2)` fails to create the communication pipes.
- `fork(2)` fails due to resource limits.
- `execvp(3)` fails in the child process (the parent detects this via
  a closed pipe and returns NULL).
- `malloc(3)` fails to allocate the client handle.

# EXAMPLES

# Launch a local bash-server

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    const char *argv[] = {"bash-server", "--stdio", NULL};
    bc_client_t *client;

    client = bc_connect_stdio(argv);
    if (!client) {
        perror("bc_connect_stdio");
        return 1;
    }

    /* Authenticate -- stdio mode reads token from --auth-fd or config */
    if (bc_auth(client, getenv("BASH_SERVER_TOKEN")) != BC_OK) {
        fprintf(stderr, "auth failed: %s\n", bc_error(client));
        bc_close(client);
        return 1;
    }

    /* Evaluate a command */
    bc_eval_result_t result;
    if (bc_eval(client, "uname -a", &result) == BC_OK) {
        printf("%s", result.stdout_data);
        bc_eval_result_free(&result);
    }

    bc_close(client);
    return 0;
}
```

# Launch bash-server over SSH

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    const char *host = argc > 1 ? argv[1] : "localhost";
    const char *cmd[] = {
        "ssh", "-o", "BatchMode=yes",
        host, "bash-server", "--stdio",
        NULL
    };

    bc_client_t *client = bc_connect_stdio(cmd);
    if (!client) {
        fprintf(stderr, "failed to connect via ssh to %s: %s\n",
                host, strerror(errno));
        return 1;
    }

    if (bc_auth(client, getenv("BASH_SERVER_TOKEN")) != BC_OK) {
        fprintf(stderr, "auth: %s\n", bc_error(client));
        bc_close(client);
        return 1;
    }

    bc_eval_result_t result;
    if (bc_eval(client, "hostname; whoami", &result) == BC_OK) {
        printf("host output:\n%s", result.stdout_data);
        bc_eval_result_free(&result);
    }

    bc_close(client);
    return 0;
}
```

# Launch with custom initialization

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    const char *argv[] = {
        "bash-server", "--stdio",
        "--login", "--init", "/etc/bash_profile",
        NULL
    };

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

    /* The server's shell environment now has login profile loaded */
    bc_eval_result_t result;
    if (bc_eval(client, "echo $PATH", &result) == BC_OK) {
        printf("PATH=%s", result.stdout_data);
        bc_eval_result_free(&result);
    }

    bc_close(client);
    return 0;
}
```

# SEE ALSO

`bc_connect(3)`, `bc_connect_fd(3)`, `bc_connect_named_pipe(3)`,
`bc_auth(3)`, `bc_close(3)`, `bc_error(3)`,
`bash-server(1)`, `bash-server-client-c(7)`

# NOTES

- The child subprocess is launched via `fork(2)` + `execvp(3)`, so the
  program in `argv[0]` must be in `$PATH` or specified as an absolute path.

- When `bc_close(3)` is called, the child process receives `SIGTERM`.
  If it does not exit within a short grace period, `SIGKILL` is sent.

- The stdio transport uses two pipes (one for each direction).  The child's
  stderr is inherited from the parent and is not captured by the client
  library.

- Authentication is still required after connecting.  The server in
  `--stdio` mode typically reads the token from `--auth-fd` or prints it
  to stderr on startup.

- Unlike `bc_connect(3)`, this function creates a private server instance.
  Multiple calls to `bc_connect_stdio` create independent server processes.

- The `argv` array is not modified or freed by the library.  The caller
  retains ownership.
