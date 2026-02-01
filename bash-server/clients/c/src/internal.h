/*
 * internal.h - Private types for bashclient
 */

#ifndef BASHCLIENT_INTERNAL_H
#define BASHCLIENT_INTERNAL_H

#include "bashclient.h"
#include <stdio.h>
#include <sys/types.h>

#define BC_ERRBUF_SIZE 256
#define BC_LINEBUF_SIZE (BC_FRAME_MAX_PAYLOAD + 64)

/* Transport type */
typedef enum {
    BC_TRANSPORT_UNIX,
    BC_TRANSPORT_STDIO,
    BC_TRANSPORT_FD,
    BC_TRANSPORT_PIPE,
} bc_transport_type_t;

/* Transport */
typedef struct {
    bc_transport_type_t type;
    int fd_read;
    int fd_write;
    pid_t child_pid;     /* for stdio transport */
    FILE *fp_read;       /* buffered read */
} bc_transport_t;

/* Client */
struct bc_client {
    bc_transport_t transport;
    char errbuf[BC_ERRBUF_SIZE];
    char *linebuf;
    size_t linebuf_size;
    int authenticated;
    int timeout_ms;

    /* Callbacks */
    bc_pre_command_cb  observe_pre_cb;
    void              *observe_pre_ud;
    bc_post_command_cb observe_post_cb;
    void              *observe_post_ud;
    bc_break_hit_cb    debug_break_cb;
    void              *debug_break_ud;
    bc_pty_output_cb   pty_output_cb;
    void              *pty_output_ud;
    bc_pty_exit_cb     pty_exit_cb;
    void              *pty_exit_ud;
};

/* Internal helpers */
int  bc_transport_open_unix(bc_transport_t *t, const char *path);
int  bc_transport_open_stdio(bc_transport_t *t, const char *const argv[]);
int  bc_transport_open_fd(bc_transport_t *t, int fd);
int  bc_transport_open_pipe(bc_transport_t *t, const char *pipe_name);
int  bc_transport_readline(bc_transport_t *t, char *buf, size_t bufsize);
int  bc_transport_write(bc_transport_t *t, const char *data, size_t len);
void bc_transport_close(bc_transport_t *t);

/* JSON helpers (minimal, no external deps) */
char *bc_json_get_string(const char *json, const char *key);
int   bc_json_get_int(const char *json, const char *key, int def);
long long bc_json_get_llong(const char *json, const char *key, long long def);
char *bc_json_get_object(const char *json, const char *key);
char *bc_json_get_array(const char *json, const char *key);

/* Base64 */
char *bc_b64_encode(const char *data, size_t len);
char *bc_b64_decode(const char *encoded, size_t *out_len);

/* Protocol */
int  bc_send_msg(bc_client_t *c, const char *json);
int  bc_recv_msg(bc_client_t *c, char **json_out);
int  bc_recv_msg_channel(bc_client_t *c, int channel, char **json_out);
void bc_set_error(bc_client_t *c, const char *fmt, ...);

#endif /* BASHCLIENT_INTERNAL_H */
