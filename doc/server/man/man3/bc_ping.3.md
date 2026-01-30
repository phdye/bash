# bc\_ping(3) -- Ping the bash-server for keepalive

# SYNOPSIS

    #include "bashclient.h"

    int bc_ping(bc_client_t *c);

# DESCRIPTION

Sends a ping request to the `bash-server(1)` instance on `CHAN_CONTROL`
(channel 0) and waits for a pong response.  This function serves as a
lightweight health check and keepalive mechanism.

The ping/pong exchange verifies that:

- The transport connection is still alive.
- The server process is responsive.
- The protocol framing is intact.

On the v2 protocol, the ping is sent as a JSON message
`{"type":"ping"}` on `CHAN_CONTROL`.  The server responds with
`{"type":"pong"}`.

The function blocks until the pong response is received or the timeout
expires.

# PARAMETERS

| Parameter | Type            | Description                              |
|-----------|-----------------|------------------------------------------|
| `c`       | `bc_client_t *` | Authenticated client handle.  Must not be NULL. |

# RETURN VALUE

Returns **`BC_OK`** (0) on success (pong received).

Returns an error code on failure:

| Code               | Value | Meaning                                   |
|--------------------|-------|-------------------------------------------|
| `BC_ERR_AUTH`      | -1    | Connection is not authenticated.          |
| `BC_ERR_PROTOCOL`  | -2    | Unexpected response from the server.      |
| `BC_ERR_TIMEOUT`   | -3    | Server did not respond within the timeout.|
| `BC_ERR_TRANSPORT` | -4    | Connection lost.                          |
| `BC_ERR_PARAM`     | -7    | `c` is NULL.                              |

# ERRORS

The function may fail when:

- `c` is NULL or not a valid client handle.
- The connection has not been authenticated with `bc_auth(3)`.
- The server does not respond within the timeout period.
- The connection is broken or reset.
- The server sends an unexpected response.

# EXAMPLES

# Simple health check

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

    if (bc_ping(client) == BC_OK)
        printf("server is alive\n");
    else
        fprintf(stderr, "ping failed: %s\n", bc_error(client));

    bc_close(client);
    return 0;
}
```

# Periodic keepalive loop

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int keepalive_loop(bc_client_t *client, int interval_sec, int max_failures)
{
    int consecutive_failures = 0;

    while (consecutive_failures < max_failures) {
        sleep(interval_sec);

        if (bc_ping(client) == BC_OK) {
            consecutive_failures = 0;
        } else {
            consecutive_failures++;
            fprintf(stderr, "ping failed (%d/%d): %s\n",
                    consecutive_failures, max_failures,
                    bc_error(client));
        }
    }

    return -1;  /* max failures reached */
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

    /* Ping every 30 seconds, give up after 3 consecutive failures */
    keepalive_loop(client, 30, 3);

    fprintf(stderr, "server unreachable, exiting\n");
    bc_close(client);
    return 1;
}
```

# Connection health check before command

```c
#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>

int safe_eval(bc_client_t *client, const char *cmd,
              bc_eval_result_t *result)
{
    /* Quick health check before sending a potentially expensive command */
    if (bc_ping(client) != BC_OK) {
        fprintf(stderr, "server not responsive: %s\n",
                bc_error(client));
        return BC_ERR_TRANSPORT;
    }

    return bc_eval(client, cmd, result);
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

    bc_eval_result_t result;
    if (safe_eval(client, "make -j12 all", &result) == BC_OK) {
        printf("%s", result.stdout_data);
        bc_eval_result_free(&result);
    }

    bc_close(client);
    return 0;
}
```

# SEE ALSO

`bc_auth(3)`, `bc_eval(3)`, `bc_error(3)`, `bc_close(3)`,
`bash-server(1)`, `bash-server-protocol(7)`,
`bash-server-client-c(7)`

# NOTES

- The ping operation requires an authenticated connection.  Calling
  `bc_ping` before `bc_auth(3)` returns `BC_ERR_AUTH`.

- The ping/pong exchange is a lightweight operation that does not execute
  any shell commands or modify server state.

- The default timeout for the pong response is 10 seconds.  If the server
  is under heavy load and cannot respond within this window, `bc_ping`
  returns `BC_ERR_TIMEOUT`.

- A successful ping does not guarantee that subsequent `bc_eval(3)` calls
  will succeed -- the server could become unresponsive between the ping
  and the eval.  Use ping for general health monitoring, not as a
  pre-flight check for individual commands.

- The function consumes exactly one pong message from the server.  If
  multiple pings are sent without reading responses (not possible through
  this API, but possible with raw protocol access), the responses queue up
  and may cause confusion.
