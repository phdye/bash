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
static void init_bash_for_session(void);

/* External secure compare from server_protocol.c */
extern int protocol_secure_compare(const char *a, const char *b);

/* Initialize client session */
int
session_init(client_session_t *session, int client_fd)
{
    memset(session, 0, sizeof(*session));
    session->fd = client_fd;
    session->authenticated = 0;
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

/* Initialize bash shell for this session */
static void
init_bash_for_session(void)
{
    static int bash_initialized = 0;
    
    if (bash_initialized)
        return;
    
    /* Set up shell name */
    shell_name = "bash-server";
    
    /* Not interactive or login shell */
    interactive_shell = 0;
    login_shell = 0;
    
    /* Initialize shell if not already done */
    if (!shell_initialized) {
        /* Basic initialization - we can't call full shell_initialize()
           as it requires main's argument handling. Instead we do minimal
           setup needed for parse_and_execute(). */
        
        /* The shell is initialized by the bash library when we link against it.
           We just need to make sure variables are set up properly. */
    }
    
    bash_initialized = 1;
}

/* Handle AUTH command */
static int
handle_auth(client_session_t *session, const char *arg, server_config_t *config)
{
    if (session->authenticated) {
        protocol_write_line(session->fd, "%s already authenticated", RSP_OK);
        return 0;
    }
    
    if (!arg || !*arg) {
        protocol_write_line(session->fd, "%s token required", RSP_ERR);
        return 0;
    }
    
    /* Use constant-time comparison for security */
    if (protocol_secure_compare(arg, config->auth_token)) {
        session->authenticated = 1;
        init_bash_for_session();
        protocol_write_line(session->fd, "%s", RSP_OK);
    } else {
        protocol_write_line(session->fd, "%s invalid token", RSP_ERR);
    }
    
    return 0;
}

/* Handle PING command */
static int
handle_ping(client_session_t *session)
{
    protocol_write_line(session->fd, "%s", RSP_PONG);
    return 0;
}

/* Handle QUIT command */
static int
handle_quit(client_session_t *session)
{
    protocol_write_line(session->fd, "%s", RSP_BYE);
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

/* Capture command output via pipes */
static int
capture_output(client_session_t *session, const char *command)
{
    int stdout_pipe[2] = {-1, -1};
    int stderr_pipe[2] = {-1, -1};
    int saved_stdout = -1;
    int saved_stderr = -1;
    int exit_code;
    char *stdout_data = NULL;
    char *stderr_data = NULL;
    char *stdout_b64 = NULL;
    char *stderr_b64 = NULL;
    size_t stdout_len = 0;
    size_t stderr_len = 0;
    char *cmd_copy;
    pid_t pid;
    int status;
    
    /* Create pipes */
    if (pipe(stdout_pipe) < 0 || pipe(stderr_pipe) < 0) {
        protocol_write_line(session->fd, "%s pipe creation failed", RSP_ERR);
        goto cleanup;
    }
    
    /* Fork to execute command (isolates from server) */
    pid = fork();
    if (pid < 0) {
        protocol_write_line(session->fd, "%s fork failed", RSP_ERR);
        goto cleanup;
    }
    
    if (pid == 0) {
        /* Child process - execute command */
        close(stdout_pipe[0]);
        close(stderr_pipe[0]);
        
        /* Redirect stdout/stderr to pipes */
        dup2(stdout_pipe[1], STDOUT_FILENO);
        dup2(stderr_pipe[1], STDERR_FILENO);
        close(stdout_pipe[1]);
        close(stderr_pipe[1]);
        
        /* Copy command (parse_and_execute frees it) */
        cmd_copy = strdup(command);
        if (!cmd_copy)
            _exit(127);
        
        /* Execute the command */
        exit_code = parse_and_execute(cmd_copy, "bash-server", SEVAL_NONINT | SEVAL_NOHIST);
        
        /* Flush output */
        fflush(stdout);
        fflush(stderr);
        
        _exit(exit_code);
    }
    
    /* Parent process */
    close(stdout_pipe[1]);
    close(stderr_pipe[1]);
    stdout_pipe[1] = -1;
    stderr_pipe[1] = -1;
    
    /* Read output from pipes */
    stdout_data = read_all_fd(stdout_pipe[0], &stdout_len);
    stderr_data = read_all_fd(stderr_pipe[0], &stderr_len);
    
    /* Wait for child */
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) {
        exit_code = WEXITSTATUS(status);
    } else {
        exit_code = 128 + WTERMSIG(status);
    }
    
    /* Send stdout */
    if (stdout_data && stdout_len > 0) {
        stdout_b64 = protocol_base64_encode(stdout_data, stdout_len);
        if (stdout_b64) {
            protocol_write_line(session->fd, "%s %s", RSP_STDOUT, stdout_b64);
        }
    } else {
        protocol_write_line(session->fd, "%s", RSP_STDOUT);
    }
    
    /* Send stderr */
    if (stderr_data && stderr_len > 0) {
        stderr_b64 = protocol_base64_encode(stderr_data, stderr_len);
        if (stderr_b64) {
            protocol_write_line(session->fd, "%s %s", RSP_STDERR, stderr_b64);
        }
    } else {
        protocol_write_line(session->fd, "%s", RSP_STDERR);
    }
    
    /* Send exit code */
    protocol_write_line(session->fd, "%s %d", RSP_EXIT, exit_code);
    
cleanup:
    if (stdout_pipe[0] >= 0) close(stdout_pipe[0]);
    if (stdout_pipe[1] >= 0) close(stdout_pipe[1]);
    if (stderr_pipe[0] >= 0) close(stderr_pipe[0]);
    if (stderr_pipe[1] >= 0) close(stderr_pipe[1]);
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
        protocol_write_line(session->fd, "%s not authenticated", RSP_ERR);
        return 0;
    }
    
    if (!arg || !*arg) {
        protocol_write_line(session->fd, "%s command required", RSP_ERR);
        return 0;
    }
    
    return capture_output(session, arg);
}

/* Main session handler - process client commands */
int
session_handle(client_session_t *session, server_config_t *config)
{
    char line[SERVER_MAX_LINE];
    char cmd[32];
    char arg[SERVER_MAX_CMD];
    int done = 0;
    
    while (!done) {
        /* Read command */
        if (protocol_read_line(session->fd, line, sizeof(line)) < 0) {
            break;  /* Connection closed or error */
        }
        
        /* Parse command */
        if (protocol_parse_command(line, cmd, arg, sizeof(arg)) < 0) {
            protocol_write_line(session->fd, "%s invalid command", RSP_ERR);
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
        } else {
            protocol_write_line(session->fd, "%s unknown command: %s", RSP_ERR, cmd);
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
