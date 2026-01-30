/* server_session.c -- Session handling and command execution for bash-server */

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

#include "server.h"
#include "../shell.h"
#include "../builtins/common.h"
#include <sys/stat.h>

/* External declarations from bash */
extern int last_command_exit_value;
extern int shell_initialized;
extern char *shell_name;
extern int interactive_shell;
extern int login_shell;
extern int posixly_correct;

/* Forward declarations */
static int handle_auth(client_session_t *session, const char *arg, server_config_t *config);
static int handle_eval(client_session_t *session, const char *arg);
static int handle_ping(client_session_t *session);
static int handle_quit(client_session_t *session);
static int capture_output(client_session_t *session, const char *command);
void init_bash_for_session(server_config_t *config);

/* External secure compare from server_protocol.c */
extern int protocol_secure_compare(const char *a, const char *b);

/* Initialize client session */
int
session_init(client_session_t *session, int client_fd)
{
    memset(session, 0, sizeof(*session));
    session->fd = client_fd;
    session->write_fd = -1;  /* -1 = use fd for both read and write */
    session->authenticated = 0;
    session->protocol_version = PROTOCOL_V1;  /* Default; auto-detected later */
    session->wire_format = WIRE_BINARY;       /* Default; NDJSON set if detected */
    session->pid = getpid();
    session->stdout_pipe[0] = -1;
    session->stdout_pipe[1] = -1;
    session->stderr_pipe[0] = -1;
    session->stderr_pipe[1] = -1;
    
    return 0;
}

/* Cleanup client session */
void
session_cleanup(client_session_t *session)
{
    if (session->write_fd >= 0 && session->write_fd != session->fd) {
        close(session->write_fd);
        session->write_fd = -1;
    }
    if (session->fd >= 0) {
        close(session->fd);
        session->fd = -1;
    }
    if (session->stdout_pipe[0] >= 0)
        close(session->stdout_pipe[0]);
    if (session->stdout_pipe[1] >= 0)
        close(session->stdout_pipe[1]);
    if (session->stderr_pipe[0] >= 0)
        close(session->stderr_pipe[0]);
    if (session->stderr_pipe[1] >= 0)
        close(session->stderr_pipe[1]);
}

/* Return the fd to use for writing protocol responses.
   In socket/socketpair mode, read and write use the same fd.
   In stdio mode, fd=stdin (read), write_fd=stdout (write). */
static inline int
session_wfd(client_session_t *session)
{
    return (session->write_fd >= 0) ? session->write_fd : session->fd;
}

/* External declarations for bash initialization */
extern void initialize_shell_builtins PARAMS((void));
extern void initialize_traps PARAMS((void));
extern void initialize_signals PARAMS((int));
extern void tilde_initialize PARAMS((void));
extern void initialize_shell_variables PARAMS((char **, int));
extern void initialize_job_control PARAMS((int));
extern void initialize_bash_input PARAMS((void));
extern void initialize_flags PARAMS((void));
extern void initialize_shell_options PARAMS((int));
extern void initialize_bashopts PARAMS((int));
extern char **shell_environment;

/* Try to source a file if it exists.  Uses bash's own source_file()
   (from builtins/evalfile.c) which properly sets up return_catch so
   that `return` statements in sourced scripts work correctly.
   Returns 0 on success, -1 if file doesn't exist or isn't regular. */
static int
session_source_file(const char *path)
{
    struct stat st;

    if (stat(path, &st) < 0 || !S_ISREG(st.st_mode))
        return -1;

    /* source_file(path, sflags) — sflags=0 means normal sourcing */
    source_file(path, 0);
    return 0;
}

/* Initialize bash shell for this session */
void
init_bash_for_session(server_config_t *config)
{
    static int bash_initialized = 0;

    if (bash_initialized)
        return;

    /* Set up shell name */
    shell_name = "bash-server";

    /* Set login/interactive mode based on config */
    interactive_shell = 0;
    login_shell = config->login_mode;

    /* Initialize shell components (pass 0 for non-privileged mode) */
    initialize_shell_builtins();
    initialize_traps();
    initialize_signals(0);
    tilde_initialize();
    initialize_shell_variables(shell_environment, 0);
    initialize_job_control(0);
    initialize_bash_input();
    initialize_flags();
    initialize_shell_options(0);
    initialize_bashopts(0);

    shell_initialized = 1;
    bash_initialized = 1;

    /* Source startup files based on configuration.
       Runs after full bash initialization so builtins/variables are available.

       Login mode: /etc/profile → ~/.bash_profile || ~/.bash_login || ~/.profile
       Default (non-login): ~/.bashrc
       --norc: skip ~/.bashrc
       --noprofile: skip /etc/profile and ~/.bash_profile
       --init <script>: source additional script after standard files */

    {
        const char *home = getenv("HOME");
        char path[4096];

        if (config->login_mode && !config->noprofile) {
            /* Login shell initialization */
            session_source_file("/etc/profile");
            /* Try ~/.bash_profile, then ~/.bash_login, then ~/.profile */
            if (home) {
                snprintf(path, sizeof(path), "%s/.bash_profile", home);
                if (session_source_file(path) < 0) {
                    snprintf(path, sizeof(path), "%s/.bash_login", home);
                    if (session_source_file(path) < 0) {
                        snprintf(path, sizeof(path), "%s/.profile", home);
                        session_source_file(path);
                    }
                }
            }
        } else if (!config->login_mode && !config->norc) {
            /* Non-login: source ~/.bashrc (default behavior) */
            session_source_file("/etc/bash.bashrc");
            if (home) {
                snprintf(path, sizeof(path), "%s/.bashrc", home);
                session_source_file(path);
            }
        }
    }

    /* Source additional init script if specified */
    if (config->init_file) {
        session_source_file(config->init_file);
    }
}

/* Handle AUTH command */
static int
handle_auth(client_session_t *session, const char *arg, server_config_t *config)
{
    if (session->authenticated) {
        protocol_write_line(session_wfd(session), "%s already authenticated", RSP_OK);
        return 0;
    }

    if (!arg || !*arg) {
        protocol_write_line(session_wfd(session), "%s token required", RSP_ERR);
        return 0;
    }

    /* Use constant-time comparison for security */
    if (protocol_secure_compare(arg, config->auth_token)) {
        session->authenticated = 1;
        init_bash_for_session(config);
        protocol_write_line(session_wfd(session), "%s", RSP_OK);
    } else {
        protocol_write_line(session_wfd(session), "%s invalid token", RSP_ERR);
    }

    return 0;
}

/* Handle PING command */
static int
handle_ping(client_session_t *session)
{
    protocol_write_line(session_wfd(session), "%s", RSP_PONG);
    return 0;
}

/* Handle QUIT command */
static int
handle_quit(client_session_t *session)
{
    protocol_write_line(session_wfd(session), "%s", RSP_BYE);
    return 1;  /* Signal to close connection */
}

/* Read all data from a file descriptor into a buffer */
static char *
read_all_fd(int fd, size_t *len)
{
    char *buf = NULL;
    size_t bufsize = 0;
    size_t buflen = 0;
    char tmp[4096];
    ssize_t n;
    
    while ((n = read(fd, tmp, sizeof(tmp))) > 0) {
        if (buflen + n + 1 > bufsize) {
            bufsize = bufsize ? bufsize * 2 : 8192;
            if (bufsize > SERVER_MAX_OUTPUT)
                bufsize = SERVER_MAX_OUTPUT;
            buf = realloc(buf, bufsize);
            if (!buf) {
                *len = 0;
                return NULL;
            }
        }
        
        if (buflen + n >= bufsize) {
            /* Truncate at max */
            n = bufsize - buflen - 1;
        }
        
        memcpy(buf + buflen, tmp, n);
        buflen += n;
        
        if (buflen >= SERVER_MAX_OUTPUT - 1)
            break;
    }
    
    if (buf) {
        buf[buflen] = '\0';
    }
    
    *len = buflen;
    return buf;
}

/* Capture command output via temporary files (in-process execution).
 *
 * Unlike the old fork-per-command model, this executes parse_and_execute()
 * directly in the session process.  Shell state (variables, functions,
 * working directory, aliases, traps) persists across EVAL commands.
 *
 * Output is captured by temporarily redirecting stdout/stderr to temp files
 * (created via mkstemp and immediately unlinked), then restoring the
 * original file descriptors.  This avoids pipe-buffer deadlocks that would
 * occur with in-process execution writing to a pipe nobody is reading. */
static int
capture_output(client_session_t *session, const char *command)
{
    char stdout_path[] = "/tmp/bash-srv-XXXXXX";
    char stderr_path[] = "/tmp/bash-srv-XXXXXX";
    int stdout_tmpfd = -1, stderr_tmpfd = -1;
    int saved_stdout = -1, saved_stderr = -1;
    int exit_code = 0;
    char *stdout_data = NULL, *stderr_data = NULL;
    char *stdout_b64 = NULL, *stderr_b64 = NULL;
    size_t stdout_len = 0, stderr_len = 0;
    char *cmd_copy;

    /* Create temp files for output capture */
    stdout_tmpfd = mkstemp(stdout_path);
    if (stdout_tmpfd < 0) {
        protocol_write_line(session_wfd(session), "%s temp file creation failed: %s",
                            RSP_ERR, strerror(errno));
        return 0;
    }
    stderr_tmpfd = mkstemp(stderr_path);
    if (stderr_tmpfd < 0) {
        protocol_write_line(session_wfd(session), "%s temp file creation failed: %s",
                            RSP_ERR, strerror(errno));
        close(stdout_tmpfd);
        unlink(stdout_path);
        return 0;
    }

    /* Unlink immediately — files remain open via fd but are cleaned up
       automatically if the process crashes. */
    unlink(stdout_path);
    unlink(stderr_path);

    /* Save original stdout/stderr */
    saved_stdout = dup(STDOUT_FILENO);
    saved_stderr = dup(STDERR_FILENO);
    if (saved_stdout < 0 || saved_stderr < 0) {
        protocol_write_line(session_wfd(session), "%s dup failed: %s",
                            RSP_ERR, strerror(errno));
        goto cleanup;
    }

    /* Redirect stdout/stderr to temp files */
    if (dup2(stdout_tmpfd, STDOUT_FILENO) < 0 ||
        dup2(stderr_tmpfd, STDERR_FILENO) < 0) {
        if (saved_stdout >= 0) dup2(saved_stdout, STDOUT_FILENO);
        if (saved_stderr >= 0) dup2(saved_stderr, STDERR_FILENO);
        protocol_write_line(session_wfd(session), "%s redirect failed: %s",
                            RSP_ERR, strerror(errno));
        goto cleanup;
    }

    /* Execute command in-process — state persists across EVALs.
       parse_and_execute() may free cmd_copy, so we must strdup. */
    cmd_copy = strdup(command);
    if (!cmd_copy) {
        fflush(stdout);
        fflush(stderr);
        dup2(saved_stdout, STDOUT_FILENO);
        dup2(saved_stderr, STDERR_FILENO);
        protocol_write_line(session_wfd(session), "%s out of memory", RSP_ERR);
        goto cleanup;
    }

    parse_and_execute(cmd_copy, "bash-server", SEVAL_NONINT | SEVAL_NOHIST);
    exit_code = last_command_exit_value;

    /* Flush C stdio buffers before restoring fds */
    fflush(stdout);
    fflush(stderr);

    /* Restore stdout/stderr */
    dup2(saved_stdout, STDOUT_FILENO);
    dup2(saved_stderr, STDERR_FILENO);

    /* Read captured output from temp files */
    lseek(stdout_tmpfd, 0, SEEK_SET);
    lseek(stderr_tmpfd, 0, SEEK_SET);
    stdout_data = read_all_fd(stdout_tmpfd, &stdout_len);
    stderr_data = read_all_fd(stderr_tmpfd, &stderr_len);

    /* Send stdout */
    if (stdout_data && stdout_len > 0) {
        stdout_b64 = protocol_base64_encode(stdout_data, stdout_len);
        if (stdout_b64)
            protocol_write_line(session_wfd(session), "%s %s", RSP_STDOUT, stdout_b64);
    } else {
        protocol_write_line(session_wfd(session), "%s", RSP_STDOUT);
    }

    /* Send stderr */
    if (stderr_data && stderr_len > 0) {
        stderr_b64 = protocol_base64_encode(stderr_data, stderr_len);
        if (stderr_b64)
            protocol_write_line(session_wfd(session), "%s %s", RSP_STDERR, stderr_b64);
    } else {
        protocol_write_line(session_wfd(session), "%s", RSP_STDERR);
    }

    /* Send exit code */
    protocol_write_line(session_wfd(session), "%s %d", RSP_EXIT, exit_code);

cleanup:
    if (stdout_tmpfd >= 0) close(stdout_tmpfd);
    if (stderr_tmpfd >= 0) close(stderr_tmpfd);
    if (saved_stdout >= 0) close(saved_stdout);
    if (saved_stderr >= 0) close(saved_stderr);
    free(stdout_data);
    free(stderr_data);
    free(stdout_b64);
    free(stderr_b64);

    return 0;
}

/* Handle EVAL command */
static int
handle_eval(client_session_t *session, const char *arg)
{
    if (!session->authenticated) {
        protocol_write_line(session_wfd(session), "%s not authenticated", RSP_ERR);
        return 0;
    }
    
    if (!arg || !*arg) {
        protocol_write_line(session_wfd(session), "%s command required", RSP_ERR);
        return 0;
    }
    
    return capture_output(session, arg);
}

/* Main session handler - process client commands.
   Auto-detects protocol version on first byte:
   v1 (text): first byte is printable ASCII (>= 0x20)
   v2 (JSON frames): first byte is channel ID (0-5) */
int
session_handle(client_session_t *session, server_config_t *config)
{
    char line[SERVER_MAX_LINE];
    char cmd[32];
    char arg[SERVER_MAX_CMD];
    int done = 0;
    int n;
    char first_byte;

    fprintf(stderr, "bash-server[%d]: session_handle fd=%d\n",
            (int)getpid(), session->fd);
    fflush(stderr);

    /* Auto-detect protocol version by peeking at first byte */
    {
        int version = protocol_detect_version(session->fd, &first_byte);
        if (version == PROTOCOL_V2) {
            fprintf(stderr, "bash-server[%d]: detected protocol v2 (binary)\n",
                    (int)getpid());
            fflush(stderr);
            return json_session_handle(session, config);
        }
        if (version == PROTOCOL_V2_NDJSON) {
            fprintf(stderr, "bash-server[%d]: detected protocol v2 (ndjson)\n",
                    (int)getpid());
            fflush(stderr);
            session->wire_format = WIRE_NDJSON;
            json_set_wire_format(WIRE_NDJSON);
            return json_session_handle(session, config);
        }
        /* v1 or detection failed — fall through to text protocol */
        session->protocol_version = PROTOCOL_V1;
    }

    while (!done) {
        /* Read command */
        n = protocol_read_line(session->fd, line, sizeof(line));
        if (n < 0) {
            fprintf(stderr, "bash-server[%d]: read_line returned %d (errno=%d: %s)\n",
                    (int)getpid(), n, errno, strerror(errno));
            fflush(stderr);
            break;  /* Connection closed or error */
        }
        
        /* Parse command */
        if (protocol_parse_command(line, cmd, arg, sizeof(arg)) < 0) {
            protocol_write_line(session_wfd(session), "%s invalid command", RSP_ERR);
            continue;
        }
        
        /* Dispatch command */
        if (strcmp(cmd, CMD_AUTH) == 0) {
            handle_auth(session, arg, config);
        } else if (strcmp(cmd, CMD_EVAL) == 0) {
            handle_eval(session, arg);
        } else if (strcmp(cmd, CMD_PING) == 0) {
            handle_ping(session);
        } else if (strcmp(cmd, CMD_QUIT) == 0) {
            done = handle_quit(session);
        /* Phase 2: State operations (require authentication) */
        } else if (!session->authenticated &&
                   (strcmp(cmd, CMD_GET_VAR) == 0 || strcmp(cmd, CMD_SET_VAR) == 0 ||
                    strcmp(cmd, CMD_UNSET_VAR) == 0 || strcmp(cmd, CMD_GET_FUNC) == 0 ||
                    strcmp(cmd, CMD_UNSET_FUNC) == 0 || strcmp(cmd, CMD_GET_ALIAS) == 0 ||
                    strcmp(cmd, CMD_SET_ALIAS) == 0 || strcmp(cmd, CMD_UNSET_ALIAS) == 0 ||
                    strcmp(cmd, CMD_SET_TRAP) == 0 || strcmp(cmd, CMD_UNSET_TRAP) == 0 ||
                    strcmp(cmd, CMD_INSPECT) == 0)) {
            protocol_write_line(session_wfd(session), "%s not authenticated", RSP_ERR);
        } else if (strcmp(cmd, CMD_GET_VAR) == 0) {
            state_handle_get_var(session_wfd(session), arg);
        } else if (strcmp(cmd, CMD_SET_VAR) == 0) {
            state_handle_set_var(session_wfd(session), arg);
        } else if (strcmp(cmd, CMD_UNSET_VAR) == 0) {
            state_handle_unset_var(session_wfd(session), arg);
        } else if (strcmp(cmd, CMD_GET_FUNC) == 0) {
            state_handle_get_func(session_wfd(session), arg);
        } else if (strcmp(cmd, CMD_UNSET_FUNC) == 0) {
            state_handle_unset_func(session_wfd(session), arg);
        } else if (strcmp(cmd, CMD_GET_ALIAS) == 0) {
            state_handle_get_alias(session_wfd(session), arg);
        } else if (strcmp(cmd, CMD_SET_ALIAS) == 0) {
            state_handle_set_alias(session_wfd(session), arg);
        } else if (strcmp(cmd, CMD_UNSET_ALIAS) == 0) {
            state_handle_unset_alias(session_wfd(session), arg);
        } else if (strcmp(cmd, CMD_SET_TRAP) == 0) {
            state_handle_set_trap(session_wfd(session), arg);
        } else if (strcmp(cmd, CMD_UNSET_TRAP) == 0) {
            state_handle_unset_trap(session_wfd(session), arg);
        } else if (strcmp(cmd, CMD_INSPECT) == 0) {
            state_handle_inspect(session_wfd(session), arg);
        } else {
            protocol_write_line(session_wfd(session), "%s unknown command: %s", RSP_ERR, cmd);
        }
    }
    
    return 0;
}

/* Execute a command and return exit code (for internal use) */
int
session_execute_command(client_session_t *session, const char *command)
{
    char *cmd_copy;
    int result;
    
    if (!session->authenticated)
        return -1;
    
    cmd_copy = strdup(command);
    if (!cmd_copy)
        return -1;
    
    result = parse_and_execute(cmd_copy, "bash-server", SEVAL_NONINT | SEVAL_NOHIST);
    
    return result;
}
