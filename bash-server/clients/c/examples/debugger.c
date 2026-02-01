/* Example: breakpoints and stepping */
#include "bashclient.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/tmp/bash-server/sock";
    const char *token = argc > 2 ? argv[2] : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    bc_client_t *c = bc_connect(path);
    if (!c || bc_auth(c, token) != BC_OK) { bc_close(c); return 1; }

    bc_debug_enable(c);
    int bp_id = bc_debug_add_breakpoint(c, "command", "echo", -1, NULL);
    printf("Breakpoint set: id=%d\n", bp_id);

    bc_debug_status_t status;
    bc_debug_status(c, &status);
    printf("Debug active=%d mode=%s bps=%d\n",
           status.active, status.mode ? status.mode : "", status.breakpoints);
    bc_debug_status_free(&status);

    bc_debug_remove_breakpoint(c, bp_id);
    bc_debug_disable(c);
    bc_close(c);
    return 0;
}
