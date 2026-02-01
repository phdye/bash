/* Example: PTY session */
#include "bashclient.h"
#include <stdio.h>

static void on_output(const char *data, size_t len, void *ud)
{
    (void)ud;
    fwrite(data, 1, len, stdout);
    fflush(stdout);
}

static void on_exit(int code, void *ud)
{
    (void)ud;
    printf("\n[PTY exited with code %d]\n", code);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/tmp/bash-server/sock";
    const char *token = argc > 2 ? argv[2] : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    bc_client_t *c = bc_connect(path);
    if (!c || bc_auth(c, token) != BC_OK) { bc_close(c); return 1; }

    bc_pty_on_output(c, on_output, NULL);
    bc_pty_on_exit(c, on_exit, NULL);

    bc_pty_info_t info;
    bc_pty_spawn(c, 24, 80, NULL, 1, &info);
    printf("PTY spawned: pid=%d %dx%d\n", info.pid, info.rows, info.cols);

    bc_pty_write(c, "echo 'Hello PTY!'\n", 18);
    bc_poll(c, 500);

    bc_pty_write(c, "exit\n", 5);
    bc_poll(c, 2000);

    bc_pty_close(c);
    bc_close(c);
    return 0;
}
