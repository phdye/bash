# bc\_connect\_fd(3) -- Connect to bash-server via inherited file descriptor

# SYNOPSIS

    #include "bashclient.h"

    bc_client_t *bc_connect_fd(int fd);

# DESCRIPTION

Creates a client handle that communicates with a `bash-server(1)` instance
over an already-open file descriptor.  The descriptor `fd` must be a
bidirectional (read/write) file descriptor connected to a server that
speaks the bash-server protocol.

This transport mode is useful when:

- A parent process has already established the connection and passes the
  file descriptor to a child via `fork(2)` or `exec(2)`.
- The server was launched with `--fd N` and the client inherits the
  corresponding descriptor.
- Socket activation systems (e.g., systemd) pass pre-connected descriptors.

The library takes ownership of `fd` after a successful call.  The
descriptor will be closed when `bc_close(3)` is called.  On failure,
`fd` is **not** closed -- the caller retains responsibility.

# PARAMETERS

| Parameter | Type  | Description                                             |
|-----------|-------|---------------------------------------------------------|
| `fd`      | `int` | An open, bidirectional file descriptor connected to a bash-server instance. Must be >= 0. |

# RETURN VALUE

Returns a pointer to a newly allocated `bc_client_t` handle on success.

Returns **NULL** on error.  Check `errno` for the cause:

| errno    | Meaning                                |
|----------|----------------------------------------|
| `EBADF`  | `fd` is not a valid file descriptor.   |
| `EINVAL` | `fd` is negative.                      |
| `ENOMEM` | Memory allocation failed.              |

# ERRORS

The function may fail when:

- `fd` is negative or not a valid open file descriptor.
- `malloc(3)` fails to allocate the client handle.
- `fcntl(2)` fails when querying descriptor flags.

# EXAMPLES

# Using a descriptor inherited from a parent process

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    int fd;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <fd>\n", argv[0]);
        return 1;
    }

    fd = atoi(argv[1]);

    bc_client_t *client = bc_connect_fd(fd);
    if (!client) {
        perror("bc_connect_fd");
        return 1;
    }

    if (bc_auth(client, getenv("BASH_SERVER_TOKEN")) != BC_OK) {
        fprintf(stderr, "auth: %s\n", bc_error(client));
        bc_close(client);
        return 1;
    }

    bc_eval_result_t result;
    if (bc_eval(client, "echo hello from fd transport", &result) == BC_OK) {
        printf("%s", result.stdout_data);
        bc_eval_result_free(&result);
    }

    bc_close(client);
    return 0;
}
```

# Parent process passing fd to child

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/socket.h>

int main(void)
{
    int sv[2];

    /* Create a connected socket pair */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
        perror("socketpair");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* Child: launch bash-server on one end */
        close(sv[0]);
        char fd_str[16];
        snprintf(fd_str, sizeof(fd_str), "%d", sv[1]);
        execlp("bash-server", "bash-server",
               "--fd", fd_str, NULL);
        perror("exec");
        _exit(1);
    }

    /* Parent: use the other end */
    close(sv[1]);

    bc_client_t *client = bc_connect_fd(sv[0]);
    if (!client) {
        perror("bc_connect_fd");
        return 1;
    }

    if (bc_auth(client, getenv("BASH_SERVER_TOKEN")) != BC_OK) {
        fprintf(stderr, "auth: %s\n", bc_error(client));
        bc_close(client);
        return 1;
    }

    bc_eval_result_t result;
    if (bc_eval(client, "echo $$", &result) == BC_OK) {
        printf("server PID: %s", result.stdout_data);
        bc_eval_result_free(&result);
    }

    bc_close(client);
    return 0;
}
```

# SEE ALSO

`bc_connect(3)`, `bc_connect_stdio(3)`, `bc_connect_named_pipe(3)`,
`bc_auth(3)`, `bc_close(3)`, `bc_error(3)`,
`bash-server(1)`, `bash-server-client-c(7)`

# NOTES

- The library takes ownership of `fd` on success.  Do not close `fd`
  directly after a successful call; use `bc_close(3)` instead.

- On failure, `fd` is not closed.  The caller must close it to avoid
  a file descriptor leak.

- The descriptor must be bidirectional.  A unidirectional pipe (from
  `pipe(2)`) will not work -- use `bc_connect_stdio(3)` for pipe-based
  communication instead.

- The function does not validate that a bash-server is actually listening
  on the other end of `fd`.  Protocol errors will be detected on the
  first API call (e.g., `bc_auth(3)`).

- This function is commonly used together with the server's `--fd` option:
  the parent creates a `socketpair(2)`, passes one end to the server with
  `--fd`, and wraps the other end with `bc_connect_fd`.
