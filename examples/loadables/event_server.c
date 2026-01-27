/* event_server.c - Loadable builtin for bash event streaming
   
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
   Event Server Loadable Builtin
   
   This module provides event streaming capabilities for bash.
   It hooks into bash's command execution lifecycle and sends
   events over a Unix domain socket or to stderr for debugging.
   
   Usage:
     enable -f ./event_server.exe event_server
     event_server start              # Start capturing events (to stderr)
     event_server socket /tmp/bash.sock  # Set socket path
     event_server stop               # Stop capturing events
     event_server status             # Show current status
*/

#include "loadables.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>

#include "command_hooks.h"

/* Global state */
static int event_enabled = 0;
static int event_socket = -1;
static char socket_path[256] = "";
static unsigned long event_count = 0;

/* Get current time in nanoseconds */
static unsigned long long get_time_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (unsigned long long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

/* Send event as JSON to socket or stderr */
static void send_event(const char *type, const char *command, int exit_status, const char *cwd)
{
    char buffer[4096];
    unsigned long long timestamp = get_time_ns();
    
    snprintf(buffer, sizeof(buffer),
        "{\"type\":\"%s\",\"timestamp\":%llu,\"pid\":%d,\"command\":\"%s\",\"exit_status\":%d,\"cwd\":\"%s\"}\n",
        type,
        timestamp,
        (int)getpid(),
        command ? command : "",
        exit_status,
        cwd ? cwd : "");
    
    if (event_socket >= 0) {
        /* Send to socket (non-blocking) */
        send(event_socket, buffer, strlen(buffer), MSG_DONTWAIT);
    } else {
        /* Debug: print to stderr */
        fprintf(stderr, "[EVENT] %s", buffer);
    }
    
    event_count++;
}

/* Pre-command hook callback */
static void pre_command_callback(const pre_command_info_t *info)
{
    if (!event_enabled)
        return;
    
    send_event("pre_command", info->command_string, 0, info->cwd);
}

/* Post-command hook callback */
static void post_command_callback(const post_command_info_t *info)
{
    if (!event_enabled)
        return;
    
    send_event("post_command", info->command_string, info->exit_status, NULL);
}

/* Initialize socket connection */
static int init_socket(const char *path)
{
    struct sockaddr_un addr;
    int fd;
    
    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        builtin_error("socket() failed: %s", strerror(errno));
        return -1;
    }
    
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    
    /* Connect for sending (UDP-style) */
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        builtin_error("connect() to %s failed: %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    
    return fd;
}

/* The event_server builtin command */
int
event_server_builtin(WORD_LIST *list)
{
    char *cmd;
    
    if (!list) {
        printf("Usage: event_server start|stop|status|socket <path>\n");
        printf("\n");
        printf("Commands:\n");
        printf("  start          Start capturing command events\n");
        printf("  stop           Stop capturing command events\n");
        printf("  status         Show current event server status\n");
        printf("  socket <path>  Connect to Unix domain socket for event output\n");
        printf("\n");
        printf("When no socket is configured, events are printed to stderr.\n");
        return EXECUTION_SUCCESS;
    }
    
    cmd = list->word->word;
    
    if (strcmp(cmd, "start") == 0) {
        if (event_enabled) {
            printf("Event server already running.\n");
            return EXECUTION_SUCCESS;
        }
        event_enabled = 1;
        printf("Event server started. Events will be sent to %s\n",
               socket_path[0] ? socket_path : "stderr");
        return EXECUTION_SUCCESS;
    }
    else if (strcmp(cmd, "stop") == 0) {
        if (!event_enabled) {
            printf("Event server not running.\n");
            return EXECUTION_SUCCESS;
        }
        event_enabled = 0;
        printf("Event server stopped. %lu events captured.\n", event_count);
        return EXECUTION_SUCCESS;
    }
    else if (strcmp(cmd, "status") == 0) {
        printf("Event server status:\n");
        printf("  Running: %s\n", event_enabled ? "yes" : "no");
        printf("  Socket:  %s\n", socket_path[0] ? socket_path : "(stderr)");
        printf("  Connected: %s\n", event_socket >= 0 ? "yes" : "no");
        printf("  Events captured: %lu\n", event_count);
        printf("  Pre-command hooks: %d\n", pre_command_hooks_count());
        printf("  Post-command hooks: %d\n", post_command_hooks_count());
        return EXECUTION_SUCCESS;
    }
    else if (strcmp(cmd, "socket") == 0) {
        if (!list->next) {
            builtin_error("socket path required");
            return EXECUTION_FAILURE;
        }
        
        char *path = list->next->word->word;
        
        /* Close existing socket if any */
        if (event_socket >= 0) {
            close(event_socket);
            event_socket = -1;
        }
        
        /* Try to connect */
        event_socket = init_socket(path);
        if (event_socket < 0) {
            socket_path[0] = '\0';
            return EXECUTION_FAILURE;
        }
        
        strncpy(socket_path, path, sizeof(socket_path) - 1);
        printf("Connected to socket: %s\n", socket_path);
        return EXECUTION_SUCCESS;
    }
    else {
        builtin_error("unknown command: %s", cmd);
        return EXECUTION_FAILURE;
    }
}

/* Called when the builtin is loaded via 'enable -f' */
int
event_server_builtin_load(char *name)
{
    /* Register our hooks */
    if (register_pre_command_hook(pre_command_callback) < 0) {
        fprintf(stderr, "event_server: failed to register pre-command hook\n");
        return 0;
    }
    
    if (register_post_command_hook(post_command_callback) < 0) {
        fprintf(stderr, "event_server: failed to register post-command hook\n");
        unregister_pre_command_hook(pre_command_callback);
        return 0;
    }
    
    fprintf(stderr, "event_server: loaded (use 'event_server start' to begin)\n");
    return 1;
}

/* Called when the builtin is unloaded */
int
event_server_builtin_unload(char *name)
{
    /* Stop event capture */
    event_enabled = 0;
    
    /* Unregister hooks */
    unregister_pre_command_hook(pre_command_callback);
    unregister_post_command_hook(post_command_callback);
    
    /* Close socket if open */
    if (event_socket >= 0) {
        close(event_socket);
        event_socket = -1;
    }
    
    fprintf(stderr, "event_server: unloaded (%lu events captured)\n", event_count);
    return 1;
}

/* Builtin documentation */
char *event_server_doc[] = {
    "Control the bash event server for command execution monitoring.",
    "",
    "The event server hooks into bash's command execution lifecycle and",
    "streams events (pre-command, post-command) as JSON to a Unix domain",
    "socket or to stderr for debugging.",
    "",
    "Commands:",
    "  event_server start          Start event capture",
    "  event_server stop           Stop event capture",
    "  event_server status         Show server status",
    "  event_server socket <path>  Set output socket path",
    "",
    "Events are JSON objects with fields: type, timestamp, pid, command,",
    "exit_status, cwd.",
    (char *)NULL
};

/* Builtin registration structure */
struct builtin event_server_struct = {
    "event_server",           /* builtin name */
    event_server_builtin,     /* function implementing the builtin */
    BUILTIN_ENABLED,          /* initial flags for builtin */
    event_server_doc,         /* documentation */
    "event_server [start|stop|status|socket <path>]", /* usage string */
    0                         /* reserved */
};
