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

/* Response codes */
#define RSP_OK      "OK"
#define RSP_ERR     "ERR"
#define RSP_BYE     "BYE"
#define RSP_PONG    "PONG"
#define RSP_STDOUT  "STDOUT"
#define RSP_STDERR  "STDERR"
#define RSP_EXIT    "EXIT"

/* Server configuration */
typedef struct server_config {
    char *socket_path;
    char *auth_token;
    int   max_clients;
    int   verbose;
    int   daemon_mode;
    char *pid_file;
} server_config_t;

/* Client session state */
typedef struct client_session {
    int   fd;
    int   authenticated;
    pid_t pid;
    int   stdout_pipe[2];
    int   stderr_pipe[2];
} client_session_t;

/* Function declarations - server_socket.c */
int  server_socket_create(const char *path);
void server_socket_close(int fd, const char *path);
int  server_accept_client(int server_fd);

/* Function declarations - server_session.c */
int  session_init(client_session_t *session, int client_fd);
void session_cleanup(client_session_t *session);
int  session_handle(client_session_t *session, server_config_t *config);
int  session_execute_command(client_session_t *session, const char *command);

/* Function declarations - server_protocol.c */
int  protocol_read_line(int fd, char *buf, size_t bufsize);
int  protocol_write_line(int fd, const char *fmt, ...);
int  protocol_parse_command(const char *line, char *cmd, char *arg, size_t argsize);
char *protocol_base64_encode(const char *data, size_t len);
char *protocol_base64_decode(const char *data, size_t *outlen);

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

#endif /* _SERVER_H_ */
