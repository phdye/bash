/*
 * bashclient.c - Client lifecycle, channel dispatch, convenience wrappers
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static bc_client_t *bc_alloc(void)
{
    bc_client_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->transport.fd_read = -1;
    c->transport.fd_write = -1;
    c->timeout_ms = BC_DEFAULT_TIMEOUT_MS;
    return c;
}

bc_client_t *bc_connect(const char *socket_path)
{
    bc_client_t *c = bc_alloc();
    if (!c) return NULL;
    if (bc_transport_open_unix(&c->transport, socket_path) < 0) {
        bc_set_error(c, "cannot connect to %s: %s", socket_path, strerror(errno));
        /* Still return the handle so caller can read error */
        return c;
    }
    return c;
}

bc_client_t *bc_connect_stdio(const char *const argv[])
{
    bc_client_t *c = bc_alloc();
    if (!c) return NULL;
    if (bc_transport_open_stdio(&c->transport, argv) < 0) {
        bc_set_error(c, "cannot start process");
        return c;
    }
    return c;
}

bc_client_t *bc_connect_fd(int fd)
{
    bc_client_t *c = bc_alloc();
    if (!c) return NULL;
    if (bc_transport_open_fd(&c->transport, fd) < 0) {
        bc_set_error(c, "cannot open fd %d", fd);
        return c;
    }
    return c;
}

bc_client_t *bc_connect_named_pipe(const char *pipe_name)
{
    bc_client_t *c = bc_alloc();
    if (!c) return NULL;
    if (bc_transport_open_pipe(&c->transport, pipe_name) < 0) {
        bc_set_error(c, "cannot connect to pipe %s", pipe_name);
        return c;
    }
    return c;
}

int bc_auth(bc_client_t *c, const char *token)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"ch\":0,\"type\":\"auth\",\"token\":\"%s\"}", token);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;

    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_CONTROL, &resp);
    if (rc != BC_OK) return rc;

    char *type = bc_json_get_string(resp, "type");
    if (!type) { free(resp); return BC_ERR_PROTOCOL; }

    if (strcmp(type, "auth_ok") == 0) {
        c->authenticated = 1;
        free(type); free(resp);
        return BC_OK;
    }

    char *msg = bc_json_get_string(resp, "message");
    bc_set_error(c, "auth failed: %s", msg ? msg : "unknown error");
    free(msg); free(type); free(resp);
    return BC_ERR_AUTH;
}

void bc_close(bc_client_t *c)
{
    if (!c) return;
    /* Try graceful disconnect */
    bc_send_msg(c, "{\"ch\":0,\"type\":\"disconnect\"}");
    bc_transport_close(&c->transport);
    free(c->linebuf);
    free(c);
}

void bc_free(void *ptr) { free(ptr); }

const char *bc_error(bc_client_t *c)
{
    return c ? c->errbuf : "null client";
}

int bc_ping(bc_client_t *c)
{
    int rc = bc_send_msg(c, "{\"ch\":0,\"type\":\"ping\"}");
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_CONTROL, &resp);
    if (rc != BC_OK) return rc;
    char *type = bc_json_get_string(resp, "type");
    int ok = type && strcmp(type, "pong") == 0;
    free(type); free(resp);
    return ok ? BC_OK : BC_ERR_PROTOCOL;
}

/* ---------- COMMAND ---------- */

int bc_eval(bc_client_t *c, const char *command, bc_eval_result_t *result)
{
    if (!result) return BC_ERR_PARAM;
    memset(result, 0, sizeof(*result));

    /* Escape command for JSON */
    size_t cmdlen = strlen(command);
    size_t bufsize = cmdlen * 2 + 128;
    char *buf = malloc(bufsize);
    if (!buf) return BC_ERR_NOMEM;

    char *escaped = malloc(cmdlen * 2 + 1);
    if (!escaped) { free(buf); return BC_ERR_NOMEM; }
    size_t ei = 0;
    for (size_t i = 0; i < cmdlen; i++) {
        if (command[i] == '"' || command[i] == '\\') escaped[ei++] = '\\';
        else if (command[i] == '\n') { escaped[ei++] = '\\'; escaped[ei++] = 'n'; continue; }
        else if (command[i] == '\t') { escaped[ei++] = '\\'; escaped[ei++] = 't'; continue; }
        escaped[ei++] = command[i];
    }
    escaped[ei] = '\0';

    snprintf(buf, bufsize, "{\"ch\":1,\"type\":\"eval\",\"command\":\"%s\"}", escaped);
    free(escaped);

    int rc = bc_send_msg(c, buf);
    free(buf);
    if (rc != BC_OK) return rc;

    /* Collect 3 responses: stdout, stderr, complete */
    for (int i = 0; i < 3; i++) {
        char *resp = NULL;
        rc = bc_recv_msg_channel(c, BC_CHAN_COMMAND, &resp);
        if (rc != BC_OK) { bc_eval_result_free(result); return rc; }

        char *type = bc_json_get_string(resp, "type");
        if (!type) { free(resp); continue; }

        if (strcmp(type, "stdout") == 0 || strcmp(type, "stderr") == 0) {
            char *enc = bc_json_get_string(resp, "encoding");
            char *data = bc_json_get_string(resp, "data");
            char *decoded = NULL;
            if (data && enc && strcmp(enc, "base64") == 0) {
                size_t dlen;
                decoded = bc_b64_decode(data, &dlen);
            } else if (data) {
                decoded = strdup(data);
            }
            if (strcmp(type, "stdout") == 0)
                result->stdout_data = decoded;
            else
                result->stderr_data = decoded;
            free(enc); free(data);
        } else if (strcmp(type, "complete") == 0) {
            result->exit_code = bc_json_get_int(resp, "exit_code", -1);
        } else if (strcmp(type, "error") == 0) {
            char *msg = bc_json_get_string(resp, "message");
            bc_set_error(c, "eval error: %s", msg ? msg : "unknown");
            free(msg); free(type); free(resp);
            bc_eval_result_free(result);
            return BC_ERR_SERVER;
        }

        free(type);
        free(resp);
    }

    return BC_OK;
}

void bc_eval_result_free(bc_eval_result_t *r)
{
    if (!r) return;
    free(r->stdout_data);
    free(r->stderr_data);
    r->stdout_data = NULL;
    r->stderr_data = NULL;
}

/* ---------- STATE ---------- */

int bc_state_get_var(bc_client_t *c, const char *name, bc_var_info_t *info)
{
    if (!info) return BC_ERR_PARAM;
    memset(info, 0, sizeof(*info));

    char buf[512];
    snprintf(buf, sizeof(buf),
             "{\"ch\":2,\"type\":\"get\",\"target\":\"var\",\"name\":\"%s\"}", name);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;

    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_STATE, &resp);
    if (rc != BC_OK) return rc;

    char *type = bc_json_get_string(resp, "type");
    if (type && strcmp(type, "error") == 0) {
        char *msg = bc_json_get_string(resp, "message");
        bc_set_error(c, "%s", msg ? msg : "state error");
        free(msg); free(type); free(resp);
        return BC_ERR_SERVER;
    }
    free(type);

    info->name = bc_json_get_string(resp, "name");
    if (!info->name) info->name = strdup(name);
    info->value = bc_json_get_string(resp, "value");
    if (!info->value) info->value = strdup("");

    free(resp);
    return BC_OK;
}

int bc_state_set_var(bc_client_t *c, const char *name, const char *value,
                     const char *const *attributes, int num_attrs)
{
    size_t bufsize = strlen(name) + strlen(value) + 256;
    char *buf = malloc(bufsize);
    if (!buf) return BC_ERR_NOMEM;

    snprintf(buf, bufsize,
             "{\"ch\":2,\"type\":\"set\",\"target\":\"var\","
             "\"name\":\"%s\",\"value\":\"%s\"}", name, value);

    int rc = bc_send_msg(c, buf);
    free(buf);
    if (rc != BC_OK) return rc;

    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_STATE, &resp);
    free(resp);
    return rc;
}

int bc_state_unset_var(bc_client_t *c, const char *name)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"ch\":2,\"type\":\"unset\",\"target\":\"var\",\"name\":\"%s\"}", name);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_STATE, &resp);
    free(resp);
    return rc;
}

void bc_var_info_free(bc_var_info_t *info)
{
    if (!info) return;
    free(info->name);
    free(info->value);
    if (info->attributes) {
        for (int i = 0; i < info->num_attributes; i++)
            free(info->attributes[i]);
        free(info->attributes);
    }
    memset(info, 0, sizeof(*info));
}

int bc_state_get_func(bc_client_t *c, const char *name, char **definition)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"ch\":2,\"type\":\"get\",\"target\":\"function\",\"name\":\"%s\"}", name);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_STATE, &resp);
    if (rc != BC_OK) return rc;
    *definition = bc_json_get_string(resp, "value");
    free(resp);
    return BC_OK;
}

int bc_state_unset_func(bc_client_t *c, const char *name)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"ch\":2,\"type\":\"unset\",\"target\":\"function\",\"name\":\"%s\"}", name);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_STATE, &resp);
    free(resp);
    return rc;
}

int bc_state_get_alias(bc_client_t *c, const char *name, char **value)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"ch\":2,\"type\":\"get\",\"target\":\"alias\",\"name\":\"%s\"}", name);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_STATE, &resp);
    if (rc != BC_OK) return rc;
    *value = bc_json_get_string(resp, "value");
    free(resp);
    return BC_OK;
}

int bc_state_set_alias(bc_client_t *c, const char *name, const char *value)
{
    size_t bufsize = strlen(name) + strlen(value) + 128;
    char *buf = malloc(bufsize);
    if (!buf) return BC_ERR_NOMEM;
    snprintf(buf, bufsize,
             "{\"ch\":2,\"type\":\"set\",\"target\":\"alias\","
             "\"name\":\"%s\",\"value\":\"%s\"}", name, value);
    int rc = bc_send_msg(c, buf);
    free(buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_STATE, &resp);
    free(resp);
    return rc;
}

int bc_state_unset_alias(bc_client_t *c, const char *name)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"ch\":2,\"type\":\"unset\",\"target\":\"alias\",\"name\":\"%s\"}", name);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_STATE, &resp);
    free(resp);
    return rc;
}

int bc_state_set_trap(bc_client_t *c, const char *signal, const char *command)
{
    size_t bufsize = strlen(signal) + strlen(command) + 128;
    char *buf = malloc(bufsize);
    if (!buf) return BC_ERR_NOMEM;
    snprintf(buf, bufsize,
             "{\"ch\":2,\"type\":\"set\",\"target\":\"trap\","
             "\"name\":\"%s\",\"value\":\"%s\"}", signal, command);
    int rc = bc_send_msg(c, buf);
    free(buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_STATE, &resp);
    free(resp);
    return rc;
}

int bc_state_unset_trap(bc_client_t *c, const char *signal)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "{\"ch\":2,\"type\":\"unset\",\"target\":\"trap\",\"name\":\"%s\"}", signal);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_STATE, &resp);
    free(resp);
    return rc;
}

int bc_state_inspect(bc_client_t *c, const char *query, char **json_result)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"ch\":2,\"type\":\"inspect\",\"query\":\"%s\"}", query);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;
    return bc_recv_msg_channel(c, BC_CHAN_STATE, json_result);
}

/* ---------- OBSERVE ---------- */

int bc_observe_subscribe(bc_client_t *c, int level)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ch\":3,\"type\":\"subscribe\",\"level\":%d}", level);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_OBSERVE, &resp);
    free(resp);
    return rc;
}

int bc_observe_unsubscribe(bc_client_t *c)
{
    int rc = bc_send_msg(c, "{\"ch\":3,\"type\":\"unsubscribe\"}");
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_OBSERVE, &resp);
    free(resp);
    return rc;
}

void bc_observe_on_pre_command(bc_client_t *c, bc_pre_command_cb cb, void *ud)
{
    c->observe_pre_cb = cb; c->observe_pre_ud = ud;
}

void bc_observe_on_post_command(bc_client_t *c, bc_post_command_cb cb, void *ud)
{
    c->observe_post_cb = cb; c->observe_post_ud = ud;
}

int bc_poll(bc_client_t *c, int timeout_ms)
{
    /* Simple poll: try reading, dispatch push, return count */
    (void)timeout_ms; /* TODO: select/poll with timeout */
    return 0;
}

/* ---------- DEBUG ---------- */

int bc_debug_enable(bc_client_t *c)
{
    int rc = bc_send_msg(c, "{\"ch\":4,\"type\":\"enable\"}");
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_DEBUG, &resp);
    free(resp);
    return rc;
}

int bc_debug_disable(bc_client_t *c)
{
    int rc = bc_send_msg(c, "{\"ch\":4,\"type\":\"disable\"}");
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_DEBUG, &resp);
    free(resp);
    return rc;
}

int bc_debug_status(bc_client_t *c, bc_debug_status_t *status)
{
    int rc = bc_send_msg(c, "{\"ch\":4,\"type\":\"status\"}");
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_DEBUG, &resp);
    if (rc != BC_OK) return rc;
    if (status) {
        status->active = bc_json_get_int(resp, "active", 0);
        status->mode = bc_json_get_string(resp, "mode");
        status->breakpoints = bc_json_get_int(resp, "breakpoints", 0);
        status->depth = bc_json_get_int(resp, "depth", 0);
    }
    free(resp);
    return BC_OK;
}

void bc_debug_status_free(bc_debug_status_t *s)
{
    if (s) { free(s->mode); s->mode = NULL; }
}

int bc_debug_add_breakpoint(bc_client_t *c, const char *kind,
                            const char *pattern, int line,
                            const char *condition)
{
    char buf[512];
    int n = snprintf(buf, sizeof(buf),
                     "{\"ch\":4,\"type\":\"break\",\"kind\":\"%s\"", kind);
    if (pattern)
        n += snprintf(buf + n, sizeof(buf) - n, ",\"pattern\":\"%s\"", pattern);
    if (line >= 0)
        n += snprintf(buf + n, sizeof(buf) - n, ",\"line\":%d", line);
    if (condition)
        n += snprintf(buf + n, sizeof(buf) - n, ",\"condition\":\"%s\"", condition);
    snprintf(buf + n, sizeof(buf) - n, "}");

    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_DEBUG, &resp);
    if (rc != BC_OK) return rc;
    int id = bc_json_get_int(resp, "id", -1);
    free(resp);
    return id;
}

int bc_debug_remove_breakpoint(bc_client_t *c, int bp_id)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "{\"ch\":4,\"type\":\"delete\",\"id\":%d}", bp_id);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_DEBUG, &resp);
    free(resp);
    return rc;
}

int bc_debug_list_breakpoints(bc_client_t *c, bc_breakpoint_t **bps, int *count)
{
    (void)bps; (void)count;
    /* TODO: parse breakpoint array */
    int rc = bc_send_msg(c, "{\"ch\":4,\"type\":\"list\"}");
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_DEBUG, &resp);
    free(resp);
    return rc;
}

void bc_breakpoint_list_free(bc_breakpoint_t *bps, int count)
{
    if (!bps) return;
    for (int i = 0; i < count; i++) {
        free(bps[i].kind);
        free(bps[i].pattern);
        free(bps[i].condition);
    }
    free(bps);
}

int bc_debug_continue(bc_client_t *c)
{
    int rc = bc_send_msg(c, "{\"ch\":4,\"type\":\"continue\"}");
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_DEBUG, &resp);
    free(resp);
    return rc;
}

int bc_debug_step(bc_client_t *c)
{
    int rc = bc_send_msg(c, "{\"ch\":4,\"type\":\"step\"}");
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_DEBUG, &resp);
    free(resp);
    return rc;
}

int bc_debug_next(bc_client_t *c)
{
    int rc = bc_send_msg(c, "{\"ch\":4,\"type\":\"next\"}");
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_DEBUG, &resp);
    free(resp);
    return rc;
}

int bc_debug_finish(bc_client_t *c)
{
    int rc = bc_send_msg(c, "{\"ch\":4,\"type\":\"finish\"}");
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_DEBUG, &resp);
    free(resp);
    return rc;
}

int bc_debug_skip(bc_client_t *c)
{
    int rc = bc_send_msg(c, "{\"ch\":4,\"type\":\"skip\"}");
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_DEBUG, &resp);
    free(resp);
    return rc;
}

int bc_debug_inspect_ast(bc_client_t *c, char **json_ast)
{
    int rc = bc_send_msg(c, "{\"ch\":4,\"type\":\"inspect_ast\"}");
    if (rc != BC_OK) return rc;
    return bc_recv_msg_channel(c, BC_CHAN_DEBUG, json_ast);
}

void bc_debug_on_break_hit(bc_client_t *c, bc_break_hit_cb cb, void *ud)
{
    c->debug_break_cb = cb; c->debug_break_ud = ud;
}

/* ---------- PTY ---------- */

int bc_pty_spawn(bc_client_t *c, int rows, int cols,
                 const char *shell, int strip_ansi, bc_pty_info_t *info)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf),
                     "{\"ch\":5,\"type\":\"spawn\",\"rows\":%d,\"cols\":%d", rows, cols);
    if (shell)
        n += snprintf(buf + n, sizeof(buf) - n, ",\"shell\":\"%s\"", shell);
    if (strip_ansi)
        n += snprintf(buf + n, sizeof(buf) - n, ",\"strip_ansi\":true");
    snprintf(buf + n, sizeof(buf) - n, "}");

    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;

    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_PTY, &resp);
    if (rc != BC_OK) return rc;

    if (info) {
        info->rows = bc_json_get_int(resp, "rows", rows);
        info->cols = bc_json_get_int(resp, "cols", cols);
        info->pid = bc_json_get_int(resp, "pid", 0);
        info->strip_ansi = bc_json_get_int(resp, "strip_ansi", 0);
    }
    free(resp);
    return BC_OK;
}

int bc_pty_write(bc_client_t *c, const char *data, size_t len)
{
    char *encoded = bc_b64_encode(data, len);
    if (!encoded) return BC_ERR_NOMEM;

    size_t bufsize = strlen(encoded) + 64;
    char *buf = malloc(bufsize);
    if (!buf) { free(encoded); return BC_ERR_NOMEM; }

    snprintf(buf, bufsize,
             "{\"ch\":5,\"type\":\"input\",\"data\":\"%s\",\"encoding\":\"base64\"}",
             encoded);
    int rc = bc_send_msg(c, buf);
    free(encoded); free(buf);
    return rc;
}

int bc_pty_resize(bc_client_t *c, int rows, int cols)
{
    char buf[64];
    snprintf(buf, sizeof(buf),
             "{\"ch\":5,\"type\":\"resize\",\"rows\":%d,\"cols\":%d}", rows, cols);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_PTY, &resp);
    free(resp);
    return rc;
}

int bc_pty_signal(bc_client_t *c, const char *name)
{
    char buf[128];
    snprintf(buf, sizeof(buf),
             "{\"ch\":5,\"type\":\"signal\",\"signal\":\"%s\"}", name);
    int rc = bc_send_msg(c, buf);
    if (rc != BC_OK) return rc;
    char *resp = NULL;
    rc = bc_recv_msg_channel(c, BC_CHAN_PTY, &resp);
    free(resp);
    return rc;
}

int bc_pty_close(bc_client_t *c)
{
    return bc_send_msg(c, "{\"ch\":5,\"type\":\"close\"}");
}

void bc_pty_on_output(bc_client_t *c, bc_pty_output_cb cb, void *ud)
{
    c->pty_output_cb = cb; c->pty_output_ud = ud;
}

void bc_pty_on_exit(bc_client_t *c, bc_pty_exit_cb cb, void *ud)
{
    c->pty_exit_cb = cb; c->pty_exit_ud = ud;
}
