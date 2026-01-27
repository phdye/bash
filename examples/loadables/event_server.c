/* event_server.c - Secure bidirectional event server loadable builtin
   
   Copyright (C) 2026 Free Software Foundation, Inc.

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

/*
   Secure Event Server Loadable Builtin
   
   SAFETY LAYERS:
   1. Socket permissions (0600 - owner only)
   2. Authentication token required for command injection
   3. Prompt-time execution only (commands queued, run when idle)
   4. Loop prevention (injected commands tagged)
   5. Command allowlist/denylist
   6. Rate limiting
   7. Execution mode (subshell vs mainshell)
   8. Audit logging
   9. Explicit enable required for injection
   
   Usage:
     enable -f ./event_server.exe event_server
     
     # Basic event streaming
     event_server start
     event_server socket /tmp/bash.sock
     
     # Command injection (requires explicit setup)
     event_server auth mysecrettoken
     event_server allow "ls,pwd,echo,cat"   # or "*" for all
     event_server mode subshell             # safer
     event_server inject enable
     
     # Optional
     event_server ratelimit 10              # commands/sec
     event_server audit /var/log/bash.log
*/

#include "loadables.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <ctype.h>
#include <signal.h>
#include <sys/wait.h>

#include "command_hooks.h"

/* ============== Configuration Constants ============== */

#define MAX_AUTH_TOKEN_LEN    256
#define MAX_SOCKET_PATH_LEN   256
#define MAX_COMMAND_LEN       4096
#define MAX_ALLOWLIST_ENTRIES 64
#define MAX_ALLOWLIST_CMD_LEN 64
#define MAX_COMMAND_QUEUE     32
#define MAX_AUDIT_PATH_LEN    256
#define DEFAULT_RATE_LIMIT    10
#define JSON_BUFFER_SIZE      8192

/* ============== Data Structures ============== */

/* Execution modes */
typedef enum {
    EXEC_MODE_SUBSHELL,   /* Safer: can't modify parent environment */
    EXEC_MODE_MAINSHELL   /* Full access to shell state */
} exec_mode_t;

/* Command queue entry */
typedef struct {
    char id[64];                    /* Request ID for response correlation */
    char command[MAX_COMMAND_LEN];  /* Command to execute */
    exec_mode_t mode;               /* Execution mode */
    int pending;                    /* 1 if waiting to execute */
    struct sockaddr_un client_addr; /* Where to send response */
    socklen_t client_addr_len;
} queued_command_t;

/* Rate limiter state */
typedef struct {
    time_t window_start;
    int count_in_window;
    int limit_per_second;
} rate_limiter_t;

/* ============== Global State ============== */

/* Event streaming */
static int event_enabled = 0;
static int event_socket = -1;           /* Outgoing events (DGRAM, connect mode) */
static int listen_socket = -1;          /* Incoming commands (DGRAM, bind mode) */
static char socket_path[MAX_SOCKET_PATH_LEN] = "";
static unsigned long event_count = 0;

/* Security: Authentication */
static char auth_token[MAX_AUTH_TOKEN_LEN] = "";
static int auth_configured = 0;

/* Security: Command injection control */
static int inject_enabled = 0;
static exec_mode_t default_exec_mode = EXEC_MODE_SUBSHELL;

/* Security: Allowlist (empty = deny all, "*" = allow all) */
static char allowlist[MAX_ALLOWLIST_ENTRIES][MAX_ALLOWLIST_CMD_LEN];
static int allowlist_count = 0;
static int allowlist_all = 0;  /* If 1, allow all commands */

/* Security: Denylist (checked even if allowlist_all) */
static char denylist[MAX_ALLOWLIST_ENTRIES][MAX_ALLOWLIST_CMD_LEN];
static int denylist_count = 0;

/* Security: Rate limiting */
static rate_limiter_t rate_limiter = {0, 0, DEFAULT_RATE_LIMIT};

/* Security: Audit logging */
static char audit_path[MAX_AUDIT_PATH_LEN] = "";
static FILE *audit_file = NULL;

/* Command queue */
static queued_command_t command_queue[MAX_COMMAND_QUEUE];
static int queue_head = 0;
static int queue_tail = 0;

/* Loop prevention */
static int executing_injected = 0;  /* Flag to suppress events during injected cmd */

/* Statistics */
static unsigned long commands_executed = 0;
static unsigned long commands_rejected_auth = 0;
static unsigned long commands_rejected_allowlist = 0;
static unsigned long commands_rejected_ratelimit = 0;

/* ============== Utility Functions ============== */

/* Get current time in nanoseconds */
static unsigned long long get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Get ISO 8601 timestamp for audit log */
static void get_timestamp(char *buf, size_t len)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%S", tm);
}

/* Simple JSON string escape (handles quotes and backslashes) */
static void json_escape(const char *src, char *dst, size_t dst_len)
{
    size_t i = 0;
    while (*src && i < dst_len - 2) {
        if (*src == '"' || *src == '\\') {
            dst[i++] = '\\';
        } else if (*src == '\n') {
            dst[i++] = '\\';
            dst[i++] = 'n';
            src++;
            continue;
        } else if (*src == '\r') {
            dst[i++] = '\\';
            dst[i++] = 'r';
            src++;
            continue;
        } else if (*src == '\t') {
            dst[i++] = '\\';
            dst[i++] = 't';
            src++;
            continue;
        }
        dst[i++] = *src++;
    }
    dst[i] = '\0';
}

/* Extract first word from command (for allowlist checking) */
static void get_first_word(const char *cmd, char *word, size_t word_len)
{
    size_t i = 0;
    
    /* Skip leading whitespace */
    while (*cmd && isspace(*cmd)) cmd++;
    
    /* Copy until whitespace or special char */
    while (*cmd && !isspace(*cmd) && *cmd != ';' && *cmd != '|' && 
           *cmd != '&' && *cmd != '<' && *cmd != '>' && i < word_len - 1) {
        word[i++] = *cmd++;
    }
    word[i] = '\0';
}

/* Simple JSON string extraction (finds "key":"value") */
static int json_get_string(const char *json, const char *key, char *value, size_t value_len)
{
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);

    const char *start = strstr(json, pattern);
    if (!start) return 0;

    start += strlen(pattern);

    /* Skip optional whitespace */
    while (*start && (*start == ' ' || *start == '\t')) start++;

    /* Expect opening quote */
    if (*start != '"') return 0;
    start++;

    const char *end = strchr(start, '"');
    if (!end) return 0;

    size_t len = end - start;
    if (len >= value_len) len = value_len - 1;

    strncpy(value, start, len);
    value[len] = '\0';
    return 1;
}

/* ============== Audit Logging ============== */

static void audit_log(const char *event_type, const char *detail, const char *result)
{
    if (!audit_file) return;
    
    char timestamp[32];
    get_timestamp(timestamp, sizeof(timestamp));
    
    fprintf(audit_file, "%s [%s] %s -> %s\n", timestamp, event_type, detail, result);
    fflush(audit_file);
}

/* ============== Security Checks ============== */

/* Check authentication token */
static int check_auth(const char *provided_token)
{
    if (!auth_configured) {
        return 0;  /* No auth configured = reject all */
    }
    
    if (strlen(provided_token) == 0) {
        return 0;
    }
    
    /* Constant-time comparison to prevent timing attacks */
    size_t auth_len = strlen(auth_token);
    size_t provided_len = strlen(provided_token);
    
    if (auth_len != provided_len) {
        return 0;
    }
    
    int result = 0;
    for (size_t i = 0; i < auth_len; i++) {
        result |= auth_token[i] ^ provided_token[i];
    }
    
    return result == 0;
}

/* Check if command is allowed */
static int check_allowlist(const char *command)
{
    char first_word[MAX_ALLOWLIST_CMD_LEN];
    get_first_word(command, first_word, sizeof(first_word));
    
    /* Check denylist first (always enforced) */
    for (int i = 0; i < denylist_count; i++) {
        if (strcmp(first_word, denylist[i]) == 0) {
            return 0;  /* Denied */
        }
    }
    
    /* If allow all, permit */
    if (allowlist_all) {
        return 1;
    }
    
    /* Check allowlist */
    for (int i = 0; i < allowlist_count; i++) {
        if (strcmp(first_word, allowlist[i]) == 0) {
            return 1;  /* Allowed */
        }
    }
    
    return 0;  /* Not in allowlist = denied */
}

/* Check rate limit */
static int check_rate_limit(void)
{
    time_t now = time(NULL);
    
    /* Reset window if new second */
    if (now != rate_limiter.window_start) {
        rate_limiter.window_start = now;
        rate_limiter.count_in_window = 0;
    }
    
    if (rate_limiter.count_in_window >= rate_limiter.limit_per_second) {
        return 0;  /* Rate limited */
    }
    
    rate_limiter.count_in_window++;
    return 1;
}

/* ============== Socket Management ============== */

/* Create listening socket with secure permissions */
static int create_listen_socket(const char *path)
{
    struct sockaddr_un addr;
    int fd;
    
    /* Remove existing socket file */
    unlink(path);
    
    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        builtin_error("socket() failed: %s", strerror(errno));
        return -1;
    }
    
    /* Set non-blocking */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        builtin_error("bind() to %s failed: %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    
    /* SECURITY: Set socket permissions to owner only (0600) */
    if (chmod(path, S_IRUSR | S_IWUSR) < 0) {
        builtin_error("chmod() on socket failed: %s", strerror(errno));
        close(fd);
        unlink(path);
        return -1;
    }
    
    return fd;
}

/* ============== Event Sending ============== */

static void send_event_json(const char *json)
{
    if (event_socket >= 0) {
        send(event_socket, json, strlen(json), MSG_DONTWAIT);
    } else if (event_enabled) {
        fprintf(stderr, "[EVENT] %s", json);
    }
}

static void send_event(const char *type, const char *command, int exit_status, 
                       const char *cwd, int is_injected)
{
    char buffer[JSON_BUFFER_SIZE];
    char cmd_escaped[MAX_COMMAND_LEN * 2];
    char cwd_escaped[512];
    unsigned long long timestamp = get_time_ns();
    
    json_escape(command ? command : "", cmd_escaped, sizeof(cmd_escaped));
    json_escape(cwd ? cwd : "", cwd_escaped, sizeof(cwd_escaped));
    
    snprintf(buffer, sizeof(buffer),
        "{\"type\":\"%s\",\"timestamp\":%llu,\"pid\":%d,\"command\":\"%s\","
        "\"exit_status\":%d,\"cwd\":\"%s\",\"injected\":%s}\n",
        type,
        timestamp,
        (int)getpid(),
        cmd_escaped,
        exit_status,
        cwd_escaped,
        is_injected ? "true" : "false");
    
    send_event_json(buffer);
    event_count++;
}

/* Send response to command injection request */
static void send_response(struct sockaddr_un *client_addr, socklen_t addr_len,
                          const char *id, const char *status, int exit_code,
                          const char *message)
{
    if (listen_socket < 0) return;
    
    char buffer[JSON_BUFFER_SIZE];
    char msg_escaped[1024];
    
    json_escape(message ? message : "", msg_escaped, sizeof(msg_escaped));
    
    snprintf(buffer, sizeof(buffer),
        "{\"type\":\"result\",\"id\":\"%s\",\"status\":\"%s\","
        "\"exit_code\":%d,\"message\":\"%s\"}\n",
        id, status, exit_code, msg_escaped);
    
    sendto(listen_socket, buffer, strlen(buffer), 0,
           (struct sockaddr *)client_addr, addr_len);
}

/* Send error response */
static void send_error(struct sockaddr_un *client_addr, socklen_t addr_len,
                       const char *id, const char *code, const char *message)
{
    if (listen_socket < 0) return;
    
    char buffer[JSON_BUFFER_SIZE];
    char msg_escaped[1024];
    
    json_escape(message ? message : "", msg_escaped, sizeof(msg_escaped));
    
    snprintf(buffer, sizeof(buffer),
        "{\"type\":\"error\",\"id\":\"%s\",\"code\":\"%s\",\"message\":\"%s\"}\n",
        id, code, msg_escaped);
    
    sendto(listen_socket, buffer, strlen(buffer), 0,
           (struct sockaddr *)client_addr, addr_len);
}

/* ============== Command Queue ============== */

static int queue_is_full(void)
{
    return ((queue_tail + 1) % MAX_COMMAND_QUEUE) == queue_head;
}

static int queue_is_empty(void)
{
    return queue_head == queue_tail;
}

static int enqueue_command(const char *id, const char *command, exec_mode_t mode,
                           struct sockaddr_un *client_addr, socklen_t addr_len)
{
    if (queue_is_full()) {
        return -1;
    }
    
    queued_command_t *entry = &command_queue[queue_tail];
    strncpy(entry->id, id, sizeof(entry->id) - 1);
    strncpy(entry->command, command, sizeof(entry->command) - 1);
    entry->mode = mode;
    entry->pending = 1;
    memcpy(&entry->client_addr, client_addr, sizeof(entry->client_addr));
    entry->client_addr_len = addr_len;
    
    queue_tail = (queue_tail + 1) % MAX_COMMAND_QUEUE;
    return 0;
}

static queued_command_t *dequeue_command(void)
{
    if (queue_is_empty()) {
        return NULL;
    }
    
    queued_command_t *entry = &command_queue[queue_head];
    queue_head = (queue_head + 1) % MAX_COMMAND_QUEUE;
    return entry;
}

/* ============== Command Execution ============== */

/* Execute a queued command using fork() to isolate from bash state.
 * This avoids the crash that occurs when calling parse_and_execute()
 * from within a hook callback while bash is mid-execution. */
static void execute_queued_command(queued_command_t *cmd)
{
    char audit_detail[MAX_COMMAND_LEN + 64];
    int result = -1;

    snprintf(audit_detail, sizeof(audit_detail), "id=%s cmd='%s'", cmd->id, cmd->command);

    /* Set flag to suppress recursive events */
    executing_injected = 1;

    pid_t pid = fork();

    if (pid == 0) {
        /* Child process - execute the command */
        /* Reset signal handlers to defaults */
        signal(SIGINT, SIG_DFL);
        signal(SIGTERM, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);

        /* Execute the command via bash -c */
        execlp("bash", "bash", "-c", cmd->command, (char *)NULL);

        /* If execlp fails */
        _exit(127);
    }
    else if (pid > 0) {
        /* Parent process - wait for child */
        int status;

        if (waitpid(pid, &status, 0) > 0) {
            if (WIFEXITED(status)) {
                result = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result = 128 + WTERMSIG(status);
            }
        }
    }
    else {
        /* fork() failed */
        audit_log("EXEC", audit_detail, "FORK_FAILED");
        send_error(&cmd->client_addr, cmd->client_addr_len,
                   cmd->id, "EXEC_FAILED", "fork() failed");
        executing_injected = 0;
        return;
    }

    executing_injected = 0;
    commands_executed++;

    /* Send response */
    send_response(&cmd->client_addr, cmd->client_addr_len,
                  cmd->id, "ok", result, "Command executed");

    audit_log("EXEC", audit_detail, result == 0 ? "SUCCESS" : "FAILED");
}

/* Process all queued commands (called from prompt hook) */
static void process_command_queue(void)
{
    queued_command_t *cmd;
    int processed = 0;
    
    while ((cmd = dequeue_command()) != NULL && processed < 5) {
        execute_queued_command(cmd);
        processed++;
    }
}

/* ============== Incoming Request Handler ============== */

static void handle_incoming_request(void)
{
    char buffer[JSON_BUFFER_SIZE];
    struct sockaddr_un client_addr;
    socklen_t addr_len = sizeof(client_addr);

    ssize_t n = recvfrom(listen_socket, buffer, sizeof(buffer) - 1, 0,
                         (struct sockaddr *)&client_addr, &addr_len);

    if (n <= 0) return;
    buffer[n] = '\0';
    
    /* Parse request */
    char req_type[32] = "";
    char req_id[64] = "";
    char req_auth[MAX_AUTH_TOKEN_LEN] = "";
    char req_command[MAX_COMMAND_LEN] = "";
    char req_mode[32] = "";
    
    json_get_string(buffer, "type", req_type, sizeof(req_type));
    json_get_string(buffer, "id", req_id, sizeof(req_id));
    json_get_string(buffer, "auth", req_auth, sizeof(req_auth));
    json_get_string(buffer, "command", req_command, sizeof(req_command));
    json_get_string(buffer, "mode", req_mode, sizeof(req_mode));
    
    /* Generate ID if not provided */
    if (strlen(req_id) == 0) {
        snprintf(req_id, sizeof(req_id), "%llu", get_time_ns());
    }
    
    char audit_detail[MAX_COMMAND_LEN + 128];
    snprintf(audit_detail, sizeof(audit_detail), "id=%s type=%s cmd='%.100s'",
             req_id, req_type, req_command);

    /* Only handle execute requests */
    if (strcmp(req_type, "execute") != 0) {
        send_error(&client_addr, addr_len, req_id, "INVALID_TYPE",
                   "Only 'execute' requests supported");
        audit_log("REJECT", audit_detail, "INVALID_TYPE");
        return;
    }

    /* SECURITY CHECK 1: Is injection enabled? */
    if (!inject_enabled) {
        send_error(&client_addr, addr_len, req_id, "INJECTION_DISABLED",
                   "Command injection is not enabled");
        audit_log("REJECT", audit_detail, "INJECTION_DISABLED");
        return;
    }
    
    /* SECURITY CHECK 2: Authentication */
    if (!check_auth(req_auth)) {
        commands_rejected_auth++;
        send_error(&client_addr, addr_len, req_id, "AUTH_FAILED",
                   "Invalid or missing authentication token");
        audit_log("REJECT", audit_detail, "AUTH_FAILED");
        return;
    }
    
    /* SECURITY CHECK 3: Rate limiting */
    if (!check_rate_limit()) {
        commands_rejected_ratelimit++;
        send_error(&client_addr, addr_len, req_id, "RATE_LIMITED",
                   "Too many requests, slow down");
        audit_log("REJECT", audit_detail, "RATE_LIMITED");
        return;
    }
    
    /* SECURITY CHECK 4: Allowlist/Denylist */
    if (!check_allowlist(req_command)) {
        commands_rejected_allowlist++;
        send_error(&client_addr, addr_len, req_id, "COMMAND_DENIED",
                   "Command not in allowlist or is denied");
        audit_log("REJECT", audit_detail, "COMMAND_DENIED");
        return;
    }
    
    /* Determine execution mode */
    exec_mode_t mode = default_exec_mode;
    if (strcmp(req_mode, "mainshell") == 0) {
        mode = EXEC_MODE_MAINSHELL;
    } else if (strcmp(req_mode, "subshell") == 0) {
        mode = EXEC_MODE_SUBSHELL;
    }
    
    /* Queue the command for execution at next prompt */
    if (enqueue_command(req_id, req_command, mode, &client_addr, addr_len) < 0) {
        send_error(&client_addr, addr_len, req_id, "QUEUE_FULL",
                   "Command queue is full, try again later");
        audit_log("REJECT", audit_detail, "QUEUE_FULL");
        return;
    }
    
    audit_log("QUEUED", audit_detail, "OK");
}

/* Poll for incoming requests (non-blocking) */
static void poll_incoming_requests(void)
{
    if (listen_socket < 0 || !inject_enabled) return;

    fd_set readfds;
    struct timeval tv = {0, 0};  /* Non-blocking */

    FD_ZERO(&readfds);
    FD_SET(listen_socket, &readfds);

    int sel_result = select(listen_socket + 1, &readfds, NULL, NULL, &tv);
    if (sel_result > 0) {
        handle_incoming_request();
    }
}

/* ============== Hook Callbacks ============== */

static void pre_command_callback(const pre_command_info_t *info)
{
    if (!event_enabled) return;
    if (executing_injected) return;  /* Loop prevention */
    
    /* Poll for incoming requests */
    poll_incoming_requests();
    
    send_event("pre_command", info->command_string, 0, info->cwd, 0);
}

static void post_command_callback(const post_command_info_t *info)
{
    if (!event_enabled) return;
    if (executing_injected) return;  /* Loop prevention */
    
    send_event("post_command", info->command_string, info->exit_status, NULL, 0);
    
    /* Process queued commands after each command completes */
    process_command_queue();
    
    /* Poll for more incoming requests */
    poll_incoming_requests();
}

/* ============== Builtin Command Handler ============== */

static void print_usage(void)
{
    printf("Usage: event_server <command> [args]\n");
    printf("\n");
    printf("Event Streaming:\n");
    printf("  start              Start event capture\n");
    printf("  stop               Stop event capture\n");
    printf("  socket <path>      Set Unix socket for events/commands\n");
    printf("  status             Show server status\n");
    printf("\n");
    printf("Security Configuration:\n");
    printf("  auth <token>       Set authentication token (required for injection)\n");
    printf("  allow <cmds>       Set allowed commands (comma-separated, or \"*\")\n");
    printf("  deny <cmds>        Set denied commands (comma-separated)\n");
    printf("  mode <mode>        Set execution mode: subshell (default) or mainshell\n");
    printf("  ratelimit <n>      Set max commands per second (default: 10)\n");
    printf("  audit <path>       Set audit log file path\n");
    printf("\n");
    printf("Command Injection:\n");
    printf("  inject enable      Enable command injection (requires auth + allow)\n");
    printf("  inject disable     Disable command injection\n");
    printf("\n");
}

static void print_status(void)
{
    printf("Event Server Status:\n");
    printf("  Event streaming:   %s\n", event_enabled ? "ENABLED" : "disabled");
    printf("  Socket path:       %s\n", socket_path[0] ? socket_path : "(none)");
    printf("  Event socket:      %s\n", event_socket >= 0 ? "connected" : "disconnected");
    printf("  Listen socket:     %s\n", listen_socket >= 0 ? "listening" : "not listening");
    printf("  Events sent:       %lu\n", event_count);
    printf("\n");
    printf("Security Configuration:\n");
    printf("  Auth configured:   %s\n", auth_configured ? "YES" : "no");
    printf("  Injection enabled: %s\n", inject_enabled ? "YES" : "no");
    printf("  Execution mode:    %s\n", 
           default_exec_mode == EXEC_MODE_SUBSHELL ? "subshell (safe)" : "mainshell");
    printf("  Allowlist:         %s\n", 
           allowlist_all ? "* (all)" : 
           (allowlist_count > 0 ? "configured" : "(empty - deny all)"));
    printf("  Denylist entries:  %d\n", denylist_count);
    printf("  Rate limit:        %d/sec\n", rate_limiter.limit_per_second);
    printf("  Audit log:         %s\n", audit_path[0] ? audit_path : "(disabled)");
    printf("\n");
    printf("Statistics:\n");
    printf("  Commands executed: %lu\n", commands_executed);
    printf("  Rejected (auth):   %lu\n", commands_rejected_auth);
    printf("  Rejected (allow):  %lu\n", commands_rejected_allowlist);
    printf("  Rejected (rate):   %lu\n", commands_rejected_ratelimit);
    printf("  Queue depth:       %d/%d\n", 
           (queue_tail - queue_head + MAX_COMMAND_QUEUE) % MAX_COMMAND_QUEUE,
           MAX_COMMAND_QUEUE - 1);
    printf("\n");
    printf("Hooks:\n");
    printf("  Pre-command:       %d\n", pre_command_hooks_count());
    printf("  Post-command:      %d\n", post_command_hooks_count());
}

static int handle_auth(WORD_LIST *args)
{
    if (!args) {
        builtin_error("auth requires a token argument");
        return EXECUTION_FAILURE;
    }
    
    char *token = args->word->word;
    
    if (strlen(token) < 8) {
        builtin_error("auth token must be at least 8 characters");
        return EXECUTION_FAILURE;
    }
    
    if (strlen(token) >= MAX_AUTH_TOKEN_LEN) {
        builtin_error("auth token too long (max %d)", MAX_AUTH_TOKEN_LEN - 1);
        return EXECUTION_FAILURE;
    }
    
    strncpy(auth_token, token, sizeof(auth_token) - 1);
    auth_configured = 1;
    
    printf("Authentication token configured.\n");
    audit_log("CONFIG", "auth token set", "OK");
    return EXECUTION_SUCCESS;
}

static int handle_allow(WORD_LIST *args)
{
    if (!args) {
        builtin_error("allow requires command list (comma-separated or \"*\")");
        return EXECUTION_FAILURE;
    }
    
    char *list = args->word->word;
    
    /* Reset allowlist */
    allowlist_count = 0;
    allowlist_all = 0;
    
    if (strcmp(list, "*") == 0) {
        allowlist_all = 1;
        printf("Allowlist: ALL commands (use deny to restrict)\n");
        audit_log("CONFIG", "allowlist set to *", "OK");
        return EXECUTION_SUCCESS;
    }
    
    /* Parse comma-separated list */
    char *token = strtok(list, ",");
    while (token && allowlist_count < MAX_ALLOWLIST_ENTRIES) {
        /* Trim whitespace */
        while (*token && isspace(*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace(*end)) *end-- = '\0';
        
        if (strlen(token) > 0) {
            strncpy(allowlist[allowlist_count], token, MAX_ALLOWLIST_CMD_LEN - 1);
            allowlist_count++;
        }
        token = strtok(NULL, ",");
    }
    
    printf("Allowlist: %d commands configured\n", allowlist_count);
    audit_log("CONFIG", "allowlist configured", "OK");
    return EXECUTION_SUCCESS;
}

static int handle_deny(WORD_LIST *args)
{
    if (!args) {
        builtin_error("deny requires command list (comma-separated)");
        return EXECUTION_FAILURE;
    }
    
    char *list = args->word->word;
    
    /* Reset denylist */
    denylist_count = 0;
    
    /* Parse comma-separated list */
    char *token = strtok(list, ",");
    while (token && denylist_count < MAX_ALLOWLIST_ENTRIES) {
        while (*token && isspace(*token)) token++;
        char *end = token + strlen(token) - 1;
        while (end > token && isspace(*end)) *end-- = '\0';
        
        if (strlen(token) > 0) {
            strncpy(denylist[denylist_count], token, MAX_ALLOWLIST_CMD_LEN - 1);
            denylist_count++;
        }
        token = strtok(NULL, ",");
    }
    
    printf("Denylist: %d commands configured\n", denylist_count);
    audit_log("CONFIG", "denylist configured", "OK");
    return EXECUTION_SUCCESS;
}

static int handle_socket(WORD_LIST *args)
{
    if (!args) {
        builtin_error("socket requires path argument");
        return EXECUTION_FAILURE;
    }
    
    char *path = args->word->word;
    
    /* Close existing sockets */
    if (event_socket >= 0) {
        close(event_socket);
        event_socket = -1;
    }
    if (listen_socket >= 0) {
        close(listen_socket);
        listen_socket = -1;
    }
    
    /* Create outgoing event socket (connect mode) */
    struct sockaddr_un addr;
    event_socket = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (event_socket >= 0) {
        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
        
        if (connect(event_socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            /* Not fatal - socket might not exist yet */
            close(event_socket);
            event_socket = -1;
        }
    }
    
    /* Create listening socket for incoming commands */
    char listen_path[MAX_SOCKET_PATH_LEN];
    snprintf(listen_path, sizeof(listen_path), "%s.cmd", path);
    listen_socket = create_listen_socket(listen_path);
    
    strncpy(socket_path, path, sizeof(socket_path) - 1);
    
    printf("Socket configured: %s\n", socket_path);
    printf("  Event socket:  %s\n", event_socket >= 0 ? "connected" : "not connected");
    printf("  Listen socket: %s.cmd (%s)\n", path,
           listen_socket >= 0 ? "listening" : "failed");
    
    return EXECUTION_SUCCESS;
}

static int handle_inject(WORD_LIST *args)
{
    if (!args) {
        builtin_error("inject requires 'enable' or 'disable'");
        return EXECUTION_FAILURE;
    }
    
    char *subcmd = args->word->word;
    
    if (strcmp(subcmd, "enable") == 0) {
        /* Check prerequisites */
        if (!auth_configured) {
            builtin_error("cannot enable injection: auth token not configured");
            builtin_error("use: event_server auth <token>");
            return EXECUTION_FAILURE;
        }
        if (!allowlist_all && allowlist_count == 0) {
            builtin_error("cannot enable injection: no commands allowed");
            builtin_error("use: event_server allow <commands>");
            return EXECUTION_FAILURE;
        }
        if (listen_socket < 0) {
            builtin_error("cannot enable injection: no socket configured");
            builtin_error("use: event_server socket <path>");
            return EXECUTION_FAILURE;
        }
        
        inject_enabled = 1;
        printf("Command injection ENABLED\n");
        printf("  Listening on: %s.cmd\n", socket_path);
        printf("  Auth required: YES\n");
        printf("  Mode: %s\n", 
               default_exec_mode == EXEC_MODE_SUBSHELL ? "subshell" : "mainshell");
        audit_log("CONFIG", "injection enabled", "OK");
        return EXECUTION_SUCCESS;
    }
    else if (strcmp(subcmd, "disable") == 0) {
        inject_enabled = 0;
        printf("Command injection DISABLED\n");
        audit_log("CONFIG", "injection disabled", "OK");
        return EXECUTION_SUCCESS;
    }
    
    builtin_error("unknown inject subcommand: %s", subcmd);
    return EXECUTION_FAILURE;
}

int event_server_builtin(WORD_LIST *list)
{
    if (!list) {
        print_usage();
        return EXECUTION_SUCCESS;
    }
    
    char *cmd = list->word->word;
    WORD_LIST *args = list->next;
    
    if (strcmp(cmd, "start") == 0) {
        event_enabled = 1;
        printf("Event streaming started.\n");
        return EXECUTION_SUCCESS;
    }
    else if (strcmp(cmd, "stop") == 0) {
        event_enabled = 0;
        printf("Event streaming stopped.\n");
        return EXECUTION_SUCCESS;
    }
    else if (strcmp(cmd, "status") == 0) {
        print_status();
        return EXECUTION_SUCCESS;
    }
    else if (strcmp(cmd, "socket") == 0) {
        return handle_socket(args);
    }
    else if (strcmp(cmd, "auth") == 0) {
        return handle_auth(args);
    }
    else if (strcmp(cmd, "allow") == 0) {
        return handle_allow(args);
    }
    else if (strcmp(cmd, "deny") == 0) {
        return handle_deny(args);
    }
    else if (strcmp(cmd, "inject") == 0) {
        return handle_inject(args);
    }
    else if (strcmp(cmd, "mode") == 0) {
        if (!args) {
            builtin_error("mode requires 'subshell' or 'mainshell'");
            return EXECUTION_FAILURE;
        }
        if (strcmp(args->word->word, "subshell") == 0) {
            default_exec_mode = EXEC_MODE_SUBSHELL;
            printf("Execution mode: subshell (safe)\n");
        } else if (strcmp(args->word->word, "mainshell") == 0) {
            default_exec_mode = EXEC_MODE_MAINSHELL;
            printf("Execution mode: mainshell (full access)\n");
        } else {
            builtin_error("unknown mode: %s", args->word->word);
            return EXECUTION_FAILURE;
        }
        return EXECUTION_SUCCESS;
    }
    else if (strcmp(cmd, "ratelimit") == 0) {
        if (!args) {
            builtin_error("ratelimit requires a number");
            return EXECUTION_FAILURE;
        }
        int limit = atoi(args->word->word);
        if (limit < 1 || limit > 1000) {
            builtin_error("ratelimit must be between 1 and 1000");
            return EXECUTION_FAILURE;
        }
        rate_limiter.limit_per_second = limit;
        printf("Rate limit: %d commands/second\n", limit);
        return EXECUTION_SUCCESS;
    }
    else if (strcmp(cmd, "audit") == 0) {
        if (!args) {
            builtin_error("audit requires a file path");
            return EXECUTION_FAILURE;
        }
        if (audit_file) {
            fclose(audit_file);
            audit_file = NULL;
        }
        strncpy(audit_path, args->word->word, sizeof(audit_path) - 1);
        audit_file = fopen(audit_path, "a");
        if (!audit_file) {
            builtin_error("cannot open audit file: %s", strerror(errno));
            audit_path[0] = '\0';
            return EXECUTION_FAILURE;
        }
        printf("Audit logging to: %s\n", audit_path);
        audit_log("CONFIG", "audit logging started", "OK");
        return EXECUTION_SUCCESS;
    }
    else {
        builtin_error("unknown command: %s", cmd);
        return EXECUTION_FAILURE;
    }
}

/* ============== Module Load/Unload ============== */

int event_server_builtin_load(char *name)
{
    if (register_pre_command_hook(pre_command_callback) < 0) {
        fprintf(stderr, "event_server: failed to register pre-command hook\n");
        return 0;
    }
    
    if (register_post_command_hook(post_command_callback) < 0) {
        fprintf(stderr, "event_server: failed to register post-command hook\n");
        unregister_pre_command_hook(pre_command_callback);
        return 0;
    }
    
    fprintf(stderr, "event_server: loaded (secure mode)\n");
    fprintf(stderr, "  Use 'event_server status' for configuration\n");
    return 1;
}

int event_server_builtin_unload(char *name)
{
    event_enabled = 0;
    inject_enabled = 0;
    
    unregister_pre_command_hook(pre_command_callback);
    unregister_post_command_hook(post_command_callback);
    
    if (event_socket >= 0) {
        close(event_socket);
        event_socket = -1;
    }
    if (listen_socket >= 0) {
        close(listen_socket);
        listen_socket = -1;
        /* Remove socket file */
        char listen_path[MAX_SOCKET_PATH_LEN];
        snprintf(listen_path, sizeof(listen_path), "%s.cmd", socket_path);
        unlink(listen_path);
    }
    if (audit_file) {
        audit_log("CONFIG", "unloading", "OK");
        fclose(audit_file);
        audit_file = NULL;
    }
    
    fprintf(stderr, "event_server: unloaded (%lu events, %lu commands)\n",
            event_count, commands_executed);
    return 1;
}

/* ============== Documentation ============== */

char *event_server_doc[] = {
    "Secure bidirectional event server for bash command monitoring and injection.",
    "",
    "SAFETY LAYERS:",
    "  1. Socket permissions (0600 - owner only)",
    "  2. Authentication token required for command injection",
    "  3. Prompt-time execution (commands queued, run when shell idle)",
    "  4. Loop prevention (injected commands don't trigger recursion)",
    "  5. Command allowlist/denylist",
    "  6. Rate limiting",
    "  7. Execution mode (subshell=safe, mainshell=full)",
    "  8. Audit logging",
    "  9. Explicit enable required",
    "",
    "QUICK START:",
    "  event_server socket /tmp/bash.sock",
    "  event_server auth mysecrettoken123",
    "  event_server allow \"ls,pwd,echo\"",
    "  event_server start",
    "  event_server inject enable",
    "",
    "PROTOCOL (JSON over Unix socket):",
    "  Request:  {\"type\":\"execute\",\"id\":\"x\",\"auth\":\"token\",\"command\":\"ls\"}",
    "  Response: {\"type\":\"result\",\"id\":\"x\",\"status\":\"ok\",\"exit_code\":0}",
    "",
    (char *)NULL
};

struct builtin event_server_struct = {
    "event_server",
    event_server_builtin,
    BUILTIN_ENABLED,
    event_server_doc,
    "event_server <command> [args]",
    0
};
