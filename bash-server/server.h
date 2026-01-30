/* server.h -- Common header for bash-server components */

/* Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of GNU Bash, the Bourne Again SHell.

   Bash is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   Bash is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Bash.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef _SERVER_H_
#define _SERVER_H_

#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>

/* Buffer sizes */
#define SERVER_MAX_LINE     8192
#define SERVER_MAX_TOKEN    256
#define SERVER_MAX_CMD      65536
#define SERVER_MAX_OUTPUT   (1024 * 1024)  /* 1MB */

/* Protocol commands */
#define CMD_AUTH    "AUTH"
#define CMD_EVAL    "EVAL"
#define CMD_QUIT    "QUIT"
#define CMD_PING    "PING"

/* Phase 2: State operation commands */
#define CMD_GET_VAR     "GET-VAR"
#define CMD_SET_VAR     "SET-VAR"
#define CMD_UNSET_VAR   "UNSET-VAR"
#define CMD_GET_FUNC    "GET-FUNC"
#define CMD_UNSET_FUNC  "UNSET-FUNC"
#define CMD_GET_ALIAS   "GET-ALIAS"
#define CMD_SET_ALIAS   "SET-ALIAS"
#define CMD_UNSET_ALIAS "UNSET-ALIAS"
#define CMD_SET_TRAP    "SET-TRAP"
#define CMD_UNSET_TRAP  "UNSET-TRAP"
#define CMD_INSPECT     "INSPECT"

/* Response codes */
#define RSP_OK      "OK"
#define RSP_ERR     "ERR"
#define RSP_BYE     "BYE"
#define RSP_PONG    "PONG"
#define RSP_STDOUT  "STDOUT"
#define RSP_STDERR  "STDERR"
#define RSP_EXIT    "EXIT"

/* Token size: 32 bytes of entropy, hex-encoded to 64 chars + NUL */
#define SERVER_TOKEN_BYTES  32
#define SERVER_TOKEN_HEXLEN (SERVER_TOKEN_BYTES * 2)

/* Protocol versions */
#define PROTOCOL_V1  1   /* Line-oriented text protocol */
#define PROTOCOL_V2  2   /* Length-prefixed JSON frames */

/* v2 frame header: channel(1) + flags(1) + length(4) = 6 bytes */
#define FRAME_HEADER_SIZE  6
#define FRAME_MAX_PAYLOAD  (1024 * 1024)  /* 1MB max payload */

/* v2 channel IDs */
#define CHAN_CONTROL  0   /* Auth, configure, disconnect */
#define CHAN_COMMAND  1   /* EVAL, pre-parsed trees, responses */
#define CHAN_STATE    2   /* Variable/function get/set */
#define CHAN_OBSERVE  3   /* Observability events (server push) */
#define CHAN_DEBUG    4   /* Breakpoints, stepping */
#define CHAN_PTY      5   /* Terminal I/O */
#define CHAN_MAX      5

/* v2 frame flags */
#define FRAME_FLAG_COMPRESSED  0x01
#define FRAME_FLAG_BINARY      0x02
#define FRAME_FLAG_CONTINUED   0x04
#define FRAME_FLAG_FINAL       0x08

/* Observability levels (architecture §7) */
#define OBSERVE_LEVEL_OFF      0   /* Output only (implicit) */
#define OBSERVE_LEVEL_COMMAND  1   /* Pre/post command events */
#define OBSERVE_LEVEL_MAX      1   /* Highest currently supported */

/* Server configuration */
typedef struct server_config {
    char *socket_path;
    char *auth_token;       /* Auto-generated token (heap-allocated) */
    char *auth_file;        /* Path to token file (heap-allocated) */
    int   max_clients;
    int   verbose;
    int   daemon_mode;
    int   no_peercred;    /* Disable Cygwin credential handshake (Python compat) */
    char *pid_file;
    /* Shell initialization flags */
    int   login_mode;     /* --login: full login shell init */
    int   norc;           /* --norc: skip ~/.bashrc */
    int   noprofile;      /* --noprofile: skip /etc/profile and ~/.bash_profile */
    char *init_file;      /* --init <script>: additional init script */
    /* Transport mode */
    int   stdio_mode;     /* --stdio: use stdin/stdout instead of socket */
    int   fd_mode;        /* --fd N: use inherited fd N */
    int   client_fd;      /* fd number for --fd mode */
    int   auth_fd;        /* --auth-fd N: write token to fd N (-1 = stderr) */
    /* Named pipe transport (Cygwin only) */
    char *named_pipe;     /* --named-pipe NAME: use Windows Named Pipe */
} server_config_t;

/* Client session state */
typedef struct client_session {
    int   fd;           /* Primary fd (for socketpair/socket: same for read+write) */
    int   write_fd;     /* Write fd (-1 means use fd for both read and write) */
    int   authenticated;
    int   protocol_version;  /* PROTOCOL_V1 or PROTOCOL_V2 */
    pid_t pid;
    int   stdout_pipe[2];
    int   stderr_pipe[2];
} client_session_t;

/* Function declarations - server_socket.c */
int  server_socket_create(const char *path, int no_peercred);
void server_socket_close(int fd, const char *path);
int  server_accept_client(int server_fd);

/* Function declarations - server_session.c */
int  session_init(client_session_t *session, int client_fd);
void session_cleanup(client_session_t *session);
int  session_handle(client_session_t *session, server_config_t *config);
int  session_execute_command(client_session_t *session, const char *command);
void init_bash_for_session(server_config_t *config);

/* Function declarations - server_protocol.c */
int  protocol_read_line(int fd, char *buf, size_t bufsize);
int  protocol_write_line(int fd, const char *fmt, ...);
int  protocol_parse_command(const char *line, char *cmd, char *arg, size_t argsize);
char *protocol_base64_encode(const char *data, size_t len);
char *protocol_base64_decode(const char *data, size_t *outlen);
int  protocol_secure_compare(const char *a, const char *b);

/* Function declarations - server_main.c */
void server_signal_handler(int sig);
void server_shutdown(void);
int  server_daemonize(void);

/* Bash integration - these link to the main bash code */
extern int shell_initialized;
extern int last_command_exit_value;

/* From evalstring.c - parse and execute a command string */
extern int parse_and_execute(char *string, const char *from_file, int flags);

/* From shell.c - initialization functions */
extern void shell_initialize(void);

/* From variables.c */
extern char *get_string_value(const char *);

/* Function declarations - server_state.c */
int  state_handle_get_var(int wfd, const char *arg);
int  state_handle_set_var(int wfd, const char *arg);
int  state_handle_unset_var(int wfd, const char *arg);
int  state_handle_get_func(int wfd, const char *arg);
int  state_handle_unset_func(int wfd, const char *arg);
int  state_handle_get_alias(int wfd, const char *arg);
int  state_handle_set_alias(int wfd, const char *arg);
int  state_handle_unset_alias(int wfd, const char *arg);
int  state_handle_set_trap(int wfd, const char *arg);
int  state_handle_unset_trap(int wfd, const char *arg);
int  state_handle_inspect(int wfd, const char *arg);

/* Function declarations - server_json.c (protocol v2) */
int  json_frame_read(int fd, int *channel, int *flags, char **payload, size_t *payload_len);
int  json_frame_write(int fd, int channel, int flags, const char *payload, size_t payload_len);
int  json_frame_write_fmt(int fd, int channel, const char *fmt, ...);
int  json_session_handle(client_session_t *session, server_config_t *config);

/* JSON helpers (server_json.c) */
const char *json_get_string(const char *json, const char *key, char *buf, size_t bufsize);
int  json_get_int(const char *json, const char *key, int *value);

/* Protocol version detection */
int  protocol_detect_version(int fd, char *first_byte);

/* Function declarations - server_pty.c (PTY/interactive mode) */
int  pty_handle_spawn(int client_rfd, int client_wfd, const char *payload);
int  pty_parse_signal(const char *name);

/* Function declarations - server_observe.c (observability hooks) */
void observe_init(int fd, int level);
void observe_cleanup(void);
int  observe_set_level(int level);
int  observe_get_level(void);
void json_escape_for_observe(const char *src, char *buf, size_t bufsize);

/* Function declarations - cmd_serialize.c (COMMAND tree serialization) */
struct command;  /* forward declaration from command.h */
char *cmd_serialize(struct command *cmd);
struct command *cmd_deserialize(const char *json);
void  cmd_free(struct command *cmd);
int   cmd_type_from_name(const char *name);
int   connector_from_name(const char *name);
/* cmd_free_word() and cmd_free_word_list() are internal to cmd_serialize.c */

/* Function declarations - server_debug.c (breakpoint debugger) */
#define DBG_BREAK_COMMAND   1
#define DBG_BREAK_LINE      2
#define DBG_BREAK_FUNC      3

void  debug_init(int rfd, int wfd);
void  debug_cleanup(void);
int   debug_handle_message(int rfd, int wfd, const char *payload);
int   debug_add_breakpoint(int type, const char *pattern, int line, const char *condition);
int   debug_remove_breakpoint(int id);
int   debug_enable_breakpoint(int id, int enable);
int   debug_breakpoint_count(void);
char *debug_list_breakpoints(void);
int   debug_get_step_mode(void);
void  debug_set_step_mode(int mode);
int   debug_is_active(void);
void  debug_set_active(int active);
void  debug_set_pending_cmd(struct command *cmd);

/* Function declarations - server_winpipe.c (Cygwin only) */
#ifdef __CYGWIN__
#include <windows.h>
HANDLE server_winpipe_create(const char *name);
int    server_winpipe_accept(HANDLE pipe_handle, volatile sig_atomic_t *running);
int    server_winpipe_token_path(const char *name, char *buf, size_t bufsize);
#endif

#endif /* _SERVER_H_ */
