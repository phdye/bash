/*
 * test_integration.c - Integration test with real bash-server --stdio.
 * Skips if bash-server is not in PATH.
 */

#include "bashclient.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

int main(void)
{
    alarm(30);

    /* Check if bash-server exists */
    if (system("which bash-server > /dev/null 2>&1") != 0) {
        printf("test_integration: SKIP (bash-server not found)\n");
        return 0;
    }

    const char *token = "tttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttttt";
    const char *argv[] = {"bash-server", "--stdio", "--token", token, NULL};

    bc_client_t *c = bc_connect_stdio(argv);
    if (!c) {
        printf("test_integration: FAIL (connect)\n");
        return 1;
    }

    int rc = bc_auth(c, token);
    if (rc != BC_OK) {
        printf("test_integration: FAIL (auth: %s)\n", bc_error(c));
        bc_close(c);
        return 1;
    }

    bc_eval_result_t result;
    rc = bc_eval(c, "echo integration_ok", &result);
    if (rc != BC_OK) {
        printf("test_integration: FAIL (eval: %s)\n", bc_error(c));
        bc_close(c);
        return 1;
    }

    if (!result.stdout_data || strstr(result.stdout_data, "integration_ok") == NULL) {
        printf("test_integration: FAIL (stdout: %s)\n",
               result.stdout_data ? result.stdout_data : "NULL");
        bc_eval_result_free(&result);
        bc_close(c);
        return 1;
    }

    printf("test_integration: PASS (stdout=%s exit=%d)\n",
           result.stdout_data, result.exit_code);
    bc_eval_result_free(&result);
    bc_close(c);
    return 0;
}
