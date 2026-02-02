/* server_observe.c -- Observability hooks for bash-server (Phase 4)

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
   along with Bash.  If not, see <http://www.gnu.org/licenses/>. */

/* Observability Levels (from architecture §7):
 *
 *   Level 0: Output only (stdout/stderr capture, exit codes) — implicit
 *   Level 1: Command events (pre/post command with cwd, line, duration)
 *   Level 2+: Future (variable assignments, expansion events, etc.)
 *
 * This module implements Level 0-1 by registering pre/post command hooks
 * with the bash command_hooks infrastructure (command_hooks.h/c).
 *
 * Since bash-server uses fork-per-session, each session process can safely
 * use global state for the observe fd and level.
 *
 * Events are sent as JSON frames on CHAN_OBSERVE (channel 3).
 */

#include "server.h"
#include "../command_hooks.h"
#include <sys/time.h>

/* Per-session observability state (safe because fork-per-session) */
static int observe_fd = -1;      /* fd to write observe frames to */
static int observe_level = 0;    /* Current observability level */
static int hooks_registered = 0; /* Whether hooks are currently active */

/* Timestamp tracking for command duration */
static struct timeval cmd_start_time;
static int cmd_timing_active = 0;

/* Sequence number for event ordering */
static unsigned int event_seq = 0;

/* Forward declarations */
static void observe_pre_hook(const pre_command_info_t *info);
static void observe_post_hook(const post_command_info_t *info);

/* Get current time in milliseconds since epoch */
static long long
current_time_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* Initialize observability for a session.
 * Called after auth completes in the v2 session handler.
 * fd: the write fd for sending frames to the client
 * level: desired observability level (0 = off, 1 = command events) */
void
observe_init(int fd, int level)
{
    observe_fd = fd;
    observe_level = 0;
    hooks_registered = 0;
    event_seq = 0;
    cmd_timing_active = 0;

    if (level > 0)
        observe_set_level(level);
}

/* Clean up observability — unregister hooks */
void
observe_cleanup(void)
{
    if (hooks_registered) {
        unregister_pre_command_hook(observe_pre_hook);
        unregister_post_command_hook(observe_post_hook);
        hooks_registered = 0;
    }
    observe_fd = -1;
    observe_level = 0;
}

/* Change the observability level.
 * Registers/unregisters hooks as needed.
 * Returns the level actually set (may be clamped). */
int
observe_set_level(int level)
{
    /* Clamp to supported range */
    if (level < 0) level = 0;
    if (level > OBSERVE_LEVEL_MAX) level = OBSERVE_LEVEL_MAX;

    if (level >= 1 && !hooks_registered) {
        if (register_pre_command_hook(observe_pre_hook) == 0 &&
            register_post_command_hook(observe_post_hook) == 0) {
            hooks_registered = 1;
        }
    } else if (level < 1 && hooks_registered) {
        unregister_pre_command_hook(observe_pre_hook);
        unregister_post_command_hook(observe_post_hook);
        hooks_registered = 0;
    }

    observe_level = level;
    return observe_level;
}

/* Get current observability level */
int
observe_get_level(void)
{
    return observe_level;
}

/* Pre-command hook callback.
 * Sends a "pre_command" event on CHAN_OBSERVE with:
 *   - command string
 *   - current working directory
 *   - line number
 *   - subshell/async flags
 *   - timestamp */
static void
observe_pre_hook(const pre_command_info_t *info)
{
    char escaped_cmd[4096];
    char escaped_cwd[1024];

    if (observe_fd < 0 || observe_level < 1)
        return;

    /* Start timing */
    gettimeofday(&cmd_start_time, NULL);
    cmd_timing_active = 1;

    /* Escape strings for JSON */
    json_escape_for_observe(info->command_string ? info->command_string : "",
                            escaped_cmd, sizeof(escaped_cmd));
    json_escape_for_observe(info->cwd ? info->cwd : "",
                            escaped_cwd, sizeof(escaped_cwd));

    json_frame_write_fmt(observe_fd, CHAN_OBSERVE,
        "{\"level\":1,\"type\":\"pre_command\",\"seq\":%u,"
        "\"timestamp\":%lld,"
        "\"data\":{"
        "\"command\":\"%s\","
        "\"cwd\":\"%s\","
        "\"line_number\":%d,"
        "\"is_subshell\":%s,"
        "\"is_async\":%s"
        "}}",
        event_seq++,
        current_time_ms(),
        escaped_cmd,
        escaped_cwd,
        info->line_number,
        info->is_subshell ? "true" : "false",
        info->is_async ? "true" : "false");
}

/* Post-command hook callback.
 * Sends a "post_command" event on CHAN_OBSERVE with:
 *   - command string
 *   - exit status
 *   - signal number (if killed by signal)
 *   - duration in milliseconds */
static void
observe_post_hook(const post_command_info_t *info)
{
    char escaped_cmd[4096];
    long long duration_ms = 0;

    if (observe_fd < 0 || observe_level < 1)
        return;

    /* Calculate duration */
    if (cmd_timing_active) {
        struct timeval now;
        gettimeofday(&now, NULL);
        duration_ms = (long long)(now.tv_sec - cmd_start_time.tv_sec) * 1000
                    + (now.tv_usec - cmd_start_time.tv_usec) / 1000;
        cmd_timing_active = 0;
    }

    json_escape_for_observe(info->command_string ? info->command_string : "",
                            escaped_cmd, sizeof(escaped_cmd));

    if (info->signal_number > 0) {
        json_frame_write_fmt(observe_fd, CHAN_OBSERVE,
            "{\"level\":1,\"type\":\"post_command\",\"seq\":%u,"
            "\"timestamp\":%lld,"
            "\"data\":{"
            "\"command\":\"%s\","
            "\"exit_status\":%d,"
            "\"signal_number\":%d,"
            "\"duration_ms\":%lld"
            "}}",
            event_seq++,
            current_time_ms(),
            escaped_cmd,
            info->exit_status,
            info->signal_number,
            duration_ms);
    } else {
        json_frame_write_fmt(observe_fd, CHAN_OBSERVE,
            "{\"level\":1,\"type\":\"post_command\",\"seq\":%u,"
            "\"timestamp\":%lld,"
            "\"data\":{"
            "\"command\":\"%s\","
            "\"exit_status\":%d,"
            "\"duration_ms\":%lld"
            "}}",
            event_seq++,
            current_time_ms(),
            escaped_cmd,
            info->exit_status,
            duration_ms);
    }
}

/* JSON escape helper for observe events.
 * This is a simple wrapper that the observe module uses so it doesn't
 * depend on the static json_escape() in server_json.c. */
void
json_escape_for_observe(const char *src, char *buf, size_t bufsize)
{
    char *p = buf;
    char *end = buf + bufsize - 1;

    while (*src && p < end) {
        switch (*src) {
        case '"':  if (p + 2 > end) goto done; *p++ = '\\'; *p++ = '"';  break;
        case '\\': if (p + 2 > end) goto done; *p++ = '\\'; *p++ = '\\'; break;
        case '\n': if (p + 2 > end) goto done; *p++ = '\\'; *p++ = 'n';  break;
        case '\r': if (p + 2 > end) goto done; *p++ = '\\'; *p++ = 'r';  break;
        case '\t': if (p + 2 > end) goto done; *p++ = '\\'; *p++ = 't';  break;
        default:
            if ((unsigned char)*src < 0x20) {
                if (p + 6 > end) goto done;
                p += snprintf(p, 7, "\\u%04x", (unsigned char)*src);
            } else {
                *p++ = *src;
            }
            break;
        }
        src++;
    }
done:
    *p = '\0';
}
