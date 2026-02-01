/* Example: connect, auth, eval a command */
#include "bashclient.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/tmp/bash-server/sock";
    const char *token = argc > 2 ? argv[2] : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    bc_client_t *c = bc_connect(path);
    if (!c) { fprintf(stderr, "connect failed\n"); return 1; }

    if (bc_auth(c, token) != BC_OK) {
        fprintf(stderr, "auth: %s\n", bc_error(c));
        bc_close(c); return 1;
    }

    bc_eval_result_t result;
    if (bc_eval(c, "echo 'Hello from bash-server!'", &result) == BC_OK) {
        printf("stdout: %s", result.stdout_data ? result.stdout_data : "");
        printf("stderr: %s", result.stderr_data ? result.stderr_data : "");
        printf("exit_code: %d\n", result.exit_code);
        bc_eval_result_free(&result);
    }

    bc_close(c);
    return 0;
}
