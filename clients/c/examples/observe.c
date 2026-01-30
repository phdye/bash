/* Example: subscribe to observe events */
#include "bashclient.h"
#include <stdio.h>

static void on_pre(const bc_pre_command_event_t *ev, void *ud)
{
    (void)ud;
    printf("[PRE]  seq=%d cmd=%s cwd=%s\n", ev->seq,
           ev->command ? ev->command : "", ev->cwd ? ev->cwd : "");
}

static void on_post(const bc_post_command_event_t *ev, void *ud)
{
    (void)ud;
    printf("[POST] seq=%d cmd=%s exit=%d dur=%dms\n", ev->seq,
           ev->command ? ev->command : "", ev->exit_status, ev->duration_ms);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/tmp/bash-server/sock";
    const char *token = argc > 2 ? argv[2] : "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";

    bc_client_t *c = bc_connect(path);
    if (!c || bc_auth(c, token) != BC_OK) { bc_close(c); return 1; }

    bc_observe_on_pre_command(c, on_pre, NULL);
    bc_observe_on_post_command(c, on_post, NULL);
    bc_observe_subscribe(c, 1);

    bc_eval_result_t r;
    bc_eval(c, "echo one", &r); bc_eval_result_free(&r);
    bc_eval(c, "echo two", &r); bc_eval_result_free(&r);

    bc_observe_unsubscribe(c);
    bc_close(c);
    return 0;
}
