# bc\_eval(3) -- Evaluate a shell command on bash-server

# SYNOPSIS

    #include "bashclient.h"

    int bc_eval(bc_client_t *c, const char *command,
                bc_eval_result_t *result);

# DESCRIPTION

Sends a shell command string to the `bash-server(1)` instance for
evaluation and collects the output.  The command is executed in the
server's persistent shell environment, so variables, functions, and
working directory are preserved across calls.

The function sends an `eval` message on `CHAN_COMMAND` (channel 1) and
then reads response messages until a `complete` message is received.
During execution, `stdout` and `stderr` messages are accumulated into
the result structure.

The `result` parameter points to a caller-provided `bc_eval_result_t`
structure that is populated on success:

```c
typedef struct {
    char *stdout_data;   /* malloc'd stdout output (NUL-terminated) */
    char *stderr_data;   /* malloc'd stderr output (NUL-terminated) */
    int   exit_code;     /* command exit status ($?) */
} bc_eval_result_t;
```

The `stdout_data` and `stderr_data` fields are heap-allocated strings
that must be freed by the caller using `bc_eval_result_free(3)` or
`bc_free(3)`.  They are always NUL-terminated and are set to an empty
`malloc`'d string (`""`) if no output was produced.

The `exit_code` field contains the exit status of the command, equivalent
to `$?` in the shell.  A value of 0 indicates success.

# PARAMETERS

| Parameter | Type                 | Description                                    |
|-----------|----------------------|------------------------------------------------|
| `c`       | `bc_client_t *`      | Authenticated client handle.                   |
| `command` | `const char *`       | Shell command to evaluate.  Must not be NULL.  |
| `result`  | `bc_eval_result_t *` | Output structure populated on success.  Must not be NULL. |

# RETURN VALUE

Returns **`BC_OK`** (0) on success.  The `result` structure is populated
with stdout, stderr, and the exit code.

Note that `BC_OK` indicates the eval protocol exchange completed
successfully -- the command itself may have failed.  Check
`result->exit_code` for the command's exit status.

Returns an error code on failure:

| Code               | Value | Meaning                                    |
|--------------------|-------|--------------------------------------------|
| `BC_ERR_AUTH`      | -1    | Connection is not authenticated.           |
| `BC_ERR_PROTOCOL`  | -2    | Unexpected or malformed server response.   |
| `BC_ERR_TIMEOUT`   | -3    | Server did not respond within the timeout. |
| `BC_ERR_TRANSPORT` | -4    | Connection lost during command execution.  |
| `BC_ERR_SERVER`    | -5    | Server reported an internal error.         |
| `BC_ERR_NOMEM`     | -6    | Memory allocation failed.                  |
| `BC_ERR_PARAM`     | -7    | `c`, `command`, or `result` is NULL.       |

On error, the `result` fields are set to NULL / 0 and do not need to be
freed.

# ERRORS

The function may fail when:

- `c`, `command`, or `result` is NULL.
- The connection has not been authenticated with `bc_auth(3)`.
- The server does not respond within the timeout period.
- The connection is broken or reset during execution.
- Memory allocation fails while accumulating output.
- The server sends an unrecognized or malformed response.
- The server reports an internal error (e.g., fork failure).

# EXAMPLES

# Basic command evaluation

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
    int rc = bc_eval(client, "ls -la /tmp", &result);

    if (rc != BC_OK) {
        fprintf(stderr, "eval failed: %s\n", bc_error(client));
        bc_close(client);
        return 1;
    }

    printf("stdout:\n%s", result.stdout_data);

    if (result.stderr_data[0])
        fprintf(stderr, "stderr:\n%s", result.stderr_data);

    printf("exit code: %d\n", result.exit_code);

    bc_eval_result_free(&result);
    bc_close(client);
    return 0;
}
```

# Multiple commands with persistent state

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int run(bc_client_t *client, const char *cmd)
{
    bc_eval_result_t result;
    int rc = bc_eval(client, cmd, &result);

    if (rc != BC_OK) {
        fprintf(stderr, "eval error: %s\n", bc_error(client));
        return -1;
    }

    if (result.stdout_data[0])
        printf("%s", result.stdout_data);
    if (result.stderr_data[0])
        fprintf(stderr, "%s", result.stderr_data);

    int exit_code = result.exit_code;
    bc_eval_result_free(&result);
    return exit_code;
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

    /* State persists across evaluations */
    run(client, "cd /tmp");
    run(client, "export MY_VAR=hello");
    run(client, "echo $MY_VAR from $(pwd)");
    /* Output: "hello from /tmp" */

    bc_close(client);
    return 0;
}
```

# Checking command exit code

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

    /* BC_OK means the protocol exchange succeeded, NOT that the
     * command succeeded.  Check exit_code for command status. */
    if (bc_eval(client, "grep -q root /etc/passwd", &result) == BC_OK) {
        if (result.exit_code == 0)
            printf("root user found\n");
        else
            printf("root user not found (exit %d)\n", result.exit_code);

        bc_eval_result_free(&result);
    } else {
        fprintf(stderr, "protocol error: %s\n", bc_error(client));
    }

    bc_close(client);
    return 0;
}
```

# Error handling with stderr

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int compile_file(bc_client_t *client, const char *source)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "gcc -Wall -o /tmp/a.out '%s' 2>&1", source);

    bc_eval_result_t result;
    int rc = bc_eval(client, cmd, &result);

    if (rc != BC_OK) {
        fprintf(stderr, "eval failed: %s\n", bc_error(client));
        return -1;
    }

    if (result.exit_code != 0) {
        fprintf(stderr, "compilation failed:\n%s%s",
                result.stdout_data, result.stderr_data);
    } else {
        printf("compilation succeeded\n");
    }

    int exit_code = result.exit_code;
    bc_eval_result_free(&result);
    return exit_code;
}
```

# SEE ALSO

`bc_eval_result_free(3)`, `bc_auth(3)`, `bc_ping(3)`,
`bc_error(3)`, `bc_close(3)`, `bc_free(3)`,
`bash-server(1)`, `bash-server-protocol(7)`,
`bash-server-channels(7)`, `bash-server-client-c(7)`

# NOTES

- The command string is sent as-is to the server's Bash interpreter.
  Shell metacharacters, pipes, redirections, and compound commands all
  work as expected.

- The server executes commands by forking a child process.  stdout and
  stderr are captured via pipes and returned in the result structure.
  The command runs in the server's persistent shell environment.

- `BC_OK` indicates that the protocol exchange completed, not that the
  shell command succeeded.  Always check `result->exit_code` for the
  command's actual exit status.

- The `stdout_data` and `stderr_data` fields are always non-NULL after
  a successful call (they point to `malloc`'d empty strings if no output
  was produced).  This makes it safe to use them with `printf` without
  NULL checks.

- There is no hard limit on output size, but very large outputs (multiple
  megabytes) may cause memory pressure.  For commands that produce large
  output, consider redirecting to a file in the server's filesystem.

- The function blocks until the command completes.  Long-running commands
  will block the calling thread for the duration.  There is currently no
  asynchronous evaluation API.

- Commands are executed sequentially.  Sending a second `bc_eval` while
  the first is still running is not supported and results in
  `BC_ERR_PROTOCOL`.

- The command's exit code is the value of `$?` after execution.  For
  pipelines, this is the exit code of the last command in the pipe
  (unless `pipefail` is set in the server's shell).
