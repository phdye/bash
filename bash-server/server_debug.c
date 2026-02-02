/* server_debug.c -- Debugger for bash-server (Phase 6)
 *
 * Copyright (C) 2026 Free Software Foundation, Inc.
 *
 * This file is part of GNU Bash, the Bourne Again SHell.
 *
 * Bash is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Bash is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Bash.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Phase 6 (architecture §7 Level 5, §9):
 *
 * Provides interactive debugging of bash scripts with:
 *   - Breakpoints by command pattern, function name, or line number
 *   - Step execution (step/next/finish/skip/continue)
 *   - AST inspection at breakpoints (serialize pending COMMAND to JSON)
 *   - Conditional breakpoints
 *
 * Uses pre_command_hook to intercept command execution.  When a
 * breakpoint hits, sends a break_hit event on CHAN_DEBUG and blocks
 * until the client sends a resume command (continue/step/next/finish/skip).
 *
 * Since we run in a fork-per-session model, blocking on the client
 * socket in the hook callback is safe — each session is isolated.
 */

#include "server.h"
#include "command.h"
#include "command_hooks.h"

/* ================================================================
 * Breakpoint types and data structures
 * ================================================================ */

#define DBG_BREAK_COMMAND   1   /* Match by command string pattern */
#define DBG_BREAK_LINE      2   /* Match by line number */
#define DBG_BREAK_FUNC      3   /* Match by function name */

/* Step modes */
#define DBG_RUN         0   /* Normal execution */
#define DBG_STEP        1   /* Step into */
#define DBG_NEXT        2   /* Step over (same depth) */
#define DBG_FINISH      3   /* Step out (one level up) */
#define DBG_SKIP        4   /* Skip current command */

typedef struct breakpoint {
    int   id;
    int   type;         /* DBG_BREAK_COMMAND, _LINE, _FUNC */
    int   enabled;
    int   hit_count;
    char *pattern;      /* Command pattern or function name */
    int   line;         /* Line number (for _LINE breakpoints) */
    char *condition;    /* Conditional expression (optional, not evaluated yet) */
    struct breakpoint *next;
} breakpoint_t;

/* Global per-session debug state (safe: fork-per-session) */
static struct {
    int           active;       /* Debug mode enabled */
    int           client_rfd;   /* Read from client */
    int           client_wfd;   /* Write to client */
    int           step_mode;    /* DBG_RUN, DBG_STEP, etc. */
    int           step_depth;   /* Nesting depth for next/finish */
    int           call_depth;   /* Current call nesting depth */
    int           next_bp_id;   /* Next breakpoint ID to assign */
    int           hook_registered;
    breakpoint_t *breakpoints;  /* Linked list */
    /* Pending command for AST inspection */
    COMMAND      *pending_cmd;
} dbg;

/* Forward declarations */
static void debug_pre_hook(const pre_command_info_t *info);

/* ================================================================
 * Initialization / Cleanup
 * ================================================================ */

void
debug_init(int rfd, int wfd)
{
    memset(&dbg, 0, sizeof(dbg));
    dbg.client_rfd = rfd;
    dbg.client_wfd = wfd;
    dbg.active = 0;
    dbg.step_mode = DBG_RUN;
    dbg.next_bp_id = 1;
    dbg.breakpoints = NULL;
    dbg.hook_registered = 0;
    dbg.pending_cmd = NULL;
}

void
debug_cleanup(void)
{
    breakpoint_t *bp, *next;

    if (dbg.hook_registered) {
        unregister_pre_command_hook(debug_pre_hook);
        dbg.hook_registered = 0;
    }

    /* Free breakpoints */
    bp = dbg.breakpoints;
    while (bp) {
        next = bp->next;
        free(bp->pattern);
        free(bp->condition);
        free(bp);
        bp = next;
    }
    dbg.breakpoints = NULL;
    dbg.active = 0;
    dbg.pending_cmd = NULL;
}

/* Ensure the pre_command_hook is registered when debug mode is active */
static void
debug_ensure_hook(void)
{
    if (!dbg.hook_registered) {
        register_pre_command_hook(debug_pre_hook);
        dbg.hook_registered = 1;
    }
}

/* ================================================================
 * Breakpoint Management
 * ================================================================ */

/* Add a breakpoint.  Returns the breakpoint ID, or -1 on error. */
int
debug_add_breakpoint(int type, const char *pattern, int line, const char *condition)
{
    breakpoint_t *bp;

    bp = calloc(1, sizeof(breakpoint_t));
    if (!bp)
        return -1;

    bp->id = dbg.next_bp_id++;
    bp->type = type;
    bp->enabled = 1;
    bp->hit_count = 0;
    bp->pattern = pattern ? strdup(pattern) : NULL;
    bp->line = line;
    bp->condition = condition ? strdup(condition) : NULL;

    /* Prepend to list */
    bp->next = dbg.breakpoints;
    dbg.breakpoints = bp;

    /* Ensure debug mode is active */
    if (!dbg.active) {
        dbg.active = 1;
        debug_ensure_hook();
    }

    return bp->id;
}

/* Remove a breakpoint by ID.  Returns 0 on success, -1 if not found. */
int
debug_remove_breakpoint(int id)
{
    breakpoint_t **pp, *bp;

    for (pp = &dbg.breakpoints; *pp; pp = &(*pp)->next) {
        if ((*pp)->id == id) {
            bp = *pp;
            *pp = bp->next;
            free(bp->pattern);
            free(bp->condition);
            free(bp);
            return 0;
        }
    }
    return -1;
}

/* Enable/disable a breakpoint.  Returns 0 on success, -1 if not found. */
int
debug_enable_breakpoint(int id, int enable)
{
    breakpoint_t *bp;

    for (bp = dbg.breakpoints; bp; bp = bp->next) {
        if (bp->id == id) {
            bp->enabled = enable;
            return 0;
        }
    }
    return -1;
}

/* Find breakpoint by ID */
static breakpoint_t *
debug_find_breakpoint(int id)
{
    breakpoint_t *bp;
    for (bp = dbg.breakpoints; bp; bp = bp->next)
        if (bp->id == id)
            return bp;
    return NULL;
}

/* Count active breakpoints */
int
debug_breakpoint_count(void)
{
    breakpoint_t *bp;
    int count = 0;
    for (bp = dbg.breakpoints; bp; bp = bp->next)
        count++;
    return count;
}

/* Check if a command matches any enabled breakpoint.
 * Returns the breakpoint that matched, or NULL. */
static breakpoint_t *
debug_check_breakpoints(const char *command, int line_number)
{
    breakpoint_t *bp;

    for (bp = dbg.breakpoints; bp; bp = bp->next) {
        if (!bp->enabled)
            continue;

        switch (bp->type) {
        case DBG_BREAK_COMMAND:
            if (bp->pattern && command && strstr(command, bp->pattern))
                return bp;
            break;

        case DBG_BREAK_LINE:
            if (bp->line > 0 && bp->line == line_number)
                return bp;
            break;

        case DBG_BREAK_FUNC:
            /* Function breakpoints would check the function name context.
             * For now, match against the command string. */
            if (bp->pattern && command && strstr(command, bp->pattern))
                return bp;
            break;
        }
    }

    return NULL;
}

/* ================================================================
 * Breakpoint list serialization (for "list" command)
 * ================================================================ */

/* Serialize all breakpoints to a JSON array string.
 * Returns malloc'd string. */
char *
debug_list_breakpoints(void)
{
    breakpoint_t *bp;
    char buf[4096];
    char *p = buf;
    int remaining = sizeof(buf) - 1;
    int first = 1;
    int n;

    n = snprintf(p, remaining, "[");
    p += n; remaining -= n;

    for (bp = dbg.breakpoints; bp; bp = bp->next) {
        const char *type_name = "command";
        if (bp->type == DBG_BREAK_LINE)
            type_name = "line";
        else if (bp->type == DBG_BREAK_FUNC)
            type_name = "function";

        n = snprintf(p, remaining,
            "%s{\"id\":%d,\"type\":\"%s\",\"enabled\":%s,\"hit_count\":%d",
            first ? "" : ",",
            bp->id, type_name,
            bp->enabled ? "true" : "false",
            bp->hit_count);
        p += n; remaining -= n;
        first = 0;

        if (bp->pattern) {
            /* Simple JSON escape for pattern (no nested quotes expected) */
            n = snprintf(p, remaining, ",\"pattern\":\"%s\"", bp->pattern);
            p += n; remaining -= n;
        }
        if (bp->type == DBG_BREAK_LINE) {
            n = snprintf(p, remaining, ",\"line\":%d", bp->line);
            p += n; remaining -= n;
        }
        if (bp->condition) {
            n = snprintf(p, remaining, ",\"condition\":\"%s\"", bp->condition);
            p += n; remaining -= n;
        }

        n = snprintf(p, remaining, "}");
        p += n; remaining -= n;

        if (remaining <= 0)
            break;
    }

    snprintf(p, remaining + 1, "]");

    return strdup(buf);
}

/* ================================================================
 * Debug step mode control
 * ================================================================ */

int
debug_get_step_mode(void)
{
    return dbg.step_mode;
}

void
debug_set_step_mode(int mode)
{
    dbg.step_mode = mode;
    if (mode == DBG_NEXT || mode == DBG_FINISH)
        dbg.step_depth = dbg.call_depth;

    /* If entering step mode, ensure hook is registered */
    if (mode != DBG_RUN && !dbg.active) {
        dbg.active = 1;
        debug_ensure_hook();
    }
}

int
debug_is_active(void)
{
    return dbg.active;
}

void
debug_set_active(int active)
{
    dbg.active = active;
    if (active)
        debug_ensure_hook();
}

/* ================================================================
 * Pre-command hook: intercept execution at breakpoints/steps
 * ================================================================ */

/* Wait for a resume command from the client on CHAN_DEBUG.
 * Blocks until the client sends continue/step/next/finish/skip.
 * Returns the step mode to use. */
static int
debug_wait_for_resume(void)
{
    while (1) {
        int channel, flags;
        char *payload;
        size_t payload_len;
        char type_buf[64];

        if (json_frame_read(dbg.client_rfd, &channel, &flags,
                            &payload, &payload_len) < 0) {
            /* Client disconnected — resume to avoid hanging */
            return DBG_RUN;
        }

        if (channel != CHAN_DEBUG) {
            free(payload);
            continue;
        }

        if (!json_get_string(payload, "type", type_buf, sizeof(type_buf))) {
            free(payload);
            continue;
        }

        if (strcmp(type_buf, "continue") == 0) {
            free(payload);
            return DBG_RUN;
        }
        if (strcmp(type_buf, "step") == 0) {
            free(payload);
            return DBG_STEP;
        }
        if (strcmp(type_buf, "next") == 0) {
            free(payload);
            return DBG_NEXT;
        }
        if (strcmp(type_buf, "finish") == 0) {
            free(payload);
            return DBG_FINISH;
        }
        if (strcmp(type_buf, "skip") == 0) {
            free(payload);
            return DBG_SKIP;
        }
        if (strcmp(type_buf, "inspect_ast") == 0) {
            /* Serialize pending command and send back */
            if (dbg.pending_cmd) {
                char *ast_json = cmd_serialize(dbg.pending_cmd);
                if (ast_json) {
                    json_frame_write_fmt(dbg.client_wfd, CHAN_DEBUG,
                        "{\"type\":\"ast\",\"data\":%s}", ast_json);
                    free(ast_json);
                }
            } else {
                json_frame_write_fmt(dbg.client_wfd, CHAN_DEBUG,
                    "{\"type\":\"ast\",\"data\":null}");
            }
            free(payload);
            continue;  /* Still waiting for resume */
        }
        if (strcmp(type_buf, "list") == 0) {
            /* List breakpoints */
            char *bp_list = debug_list_breakpoints();
            if (bp_list) {
                json_frame_write_fmt(dbg.client_wfd, CHAN_DEBUG,
                    "{\"type\":\"breakpoints\",\"data\":%s}", bp_list);
                free(bp_list);
            }
            free(payload);
            continue;
        }
        if (strcmp(type_buf, "break") == 0) {
            /* Set a breakpoint while paused */
            char kind_buf[32], pat_buf[256], cond_buf[256];
            int bp_type = DBG_BREAK_COMMAND;
            int bp_line = 0;
            int bp_id;

            if (json_get_string(payload, "kind", kind_buf, sizeof(kind_buf))) {
                if (strcmp(kind_buf, "line") == 0)
                    bp_type = DBG_BREAK_LINE;
                else if (strcmp(kind_buf, "function") == 0)
                    bp_type = DBG_BREAK_FUNC;
            }

            json_get_string(payload, "pattern", pat_buf, sizeof(pat_buf));
            json_get_int(payload, "line", &bp_line);

            const char *cond = NULL;
            if (json_get_string(payload, "condition", cond_buf, sizeof(cond_buf)))
                cond = cond_buf;

            bp_id = debug_add_breakpoint(bp_type, pat_buf, bp_line, cond);
            json_frame_write_fmt(dbg.client_wfd, CHAN_DEBUG,
                "{\"type\":\"break_ok\",\"id\":%d}", bp_id);

            free(payload);
            continue;
        }
        if (strcmp(type_buf, "delete") == 0) {
            int bp_id = 0;
            json_get_int(payload, "id", &bp_id);
            int rc = debug_remove_breakpoint(bp_id);
            json_frame_write_fmt(dbg.client_wfd, CHAN_DEBUG,
                "{\"type\":\"delete_ok\",\"id\":%d,\"found\":%s}",
                bp_id, rc == 0 ? "true" : "false");
            free(payload);
            continue;
        }

        free(payload);
        /* Unknown type — ignore and keep waiting */
    }
}

/* Pre-command hook: called before each command execution */
static void
debug_pre_hook(const pre_command_info_t *info)
{
    breakpoint_t *bp;
    int should_break = 0;
    int resume_mode;

    if (!dbg.active)
        return;

    /* Check step mode */
    switch (dbg.step_mode) {
    case DBG_STEP:
        should_break = 1;
        break;
    case DBG_NEXT:
        if (dbg.call_depth <= dbg.step_depth)
            should_break = 1;
        break;
    case DBG_FINISH:
        if (dbg.call_depth < dbg.step_depth)
            should_break = 1;
        break;
    case DBG_RUN:
    default:
        break;
    }

    /* Check breakpoints */
    if (!should_break) {
        bp = debug_check_breakpoints(info->command_string, info->line_number);
        if (bp) {
            bp->hit_count++;
            should_break = 1;
        }
    }

    if (!should_break)
        return;

    /* Use the COMMAND pointer from the hook info for AST inspection */
    dbg.pending_cmd = (COMMAND *)info->command;

    /* Send break_hit event */
    {
        char esc_cmd[2048];
        json_escape_for_observe(info->command_string ? info->command_string : "",
                                esc_cmd, sizeof(esc_cmd));

        json_frame_write_fmt(dbg.client_wfd, CHAN_DEBUG,
            "{\"type\":\"break_hit\","
            "\"line\":%d,"
            "\"command\":\"%s\","
            "\"depth\":%d}",
            info->line_number, esc_cmd, dbg.call_depth);
    }

    /* Block until client sends resume command */
    resume_mode = debug_wait_for_resume();
    dbg.step_mode = resume_mode;
    dbg.pending_cmd = NULL;

    /* Note: DBG_SKIP would need to modify execution flow.
     * In the current architecture, the pre_command_hook cannot
     * prevent execution.  SKIP is acknowledged but the command
     * still runs.  A future enhancement could add a return value
     * to the hook to allow skipping. */
}

/* ================================================================
 * Handle CHAN_DEBUG messages from client
 *
 * Called from server_json.c when a message arrives on channel 4.
 * Returns 0 on success, -1 on error.
 * ================================================================ */

int
debug_handle_message(int rfd, int wfd, const char *payload)
{
    char type_buf[64];

    /* Initialize debug state if needed */
    if (!dbg.active && !dbg.hook_registered) {
        debug_init(rfd, wfd);
    }
    /* Update fds (they might change across calls) */
    dbg.client_rfd = rfd;
    dbg.client_wfd = wfd;

    if (!json_get_string(payload, "type", type_buf, sizeof(type_buf))) {
        json_frame_write_fmt(wfd, CHAN_DEBUG,
            "{\"type\":\"error\",\"message\":\"missing type field\"}");
        return -1;
    }

    /* Enable/disable debug mode */
    if (strcmp(type_buf, "enable") == 0) {
        dbg.active = 1;
        debug_ensure_hook();
        json_frame_write_fmt(wfd, CHAN_DEBUG,
            "{\"type\":\"enable_ok\"}");
        return 0;
    }
    if (strcmp(type_buf, "disable") == 0) {
        debug_cleanup();
        json_frame_write_fmt(wfd, CHAN_DEBUG,
            "{\"type\":\"disable_ok\"}");
        return 0;
    }

    /* Set breakpoint */
    if (strcmp(type_buf, "break") == 0) {
        char kind_buf[32], pat_buf[256], cond_buf[256];
        int bp_type = DBG_BREAK_COMMAND;
        int bp_line = 0;
        int bp_id;

        if (json_get_string(payload, "kind", kind_buf, sizeof(kind_buf))) {
            if (strcmp(kind_buf, "line") == 0)
                bp_type = DBG_BREAK_LINE;
            else if (strcmp(kind_buf, "function") == 0)
                bp_type = DBG_BREAK_FUNC;
        }

        json_get_string(payload, "pattern", pat_buf, sizeof(pat_buf));
        json_get_int(payload, "line", &bp_line);

        const char *cond = NULL;
        if (json_get_string(payload, "condition", cond_buf, sizeof(cond_buf)))
            cond = cond_buf;

        bp_id = debug_add_breakpoint(bp_type, pat_buf, bp_line, cond);
        if (bp_id > 0) {
            json_frame_write_fmt(wfd, CHAN_DEBUG,
                "{\"type\":\"break_ok\",\"id\":%d}", bp_id);
        } else {
            json_frame_write_fmt(wfd, CHAN_DEBUG,
                "{\"type\":\"error\",\"message\":\"failed to add breakpoint\"}");
        }
        return 0;
    }

    /* Delete breakpoint */
    if (strcmp(type_buf, "delete") == 0) {
        int bp_id = 0;
        json_get_int(payload, "id", &bp_id);
        int rc = debug_remove_breakpoint(bp_id);
        json_frame_write_fmt(wfd, CHAN_DEBUG,
            "{\"type\":\"delete_ok\",\"id\":%d,\"found\":%s}",
            bp_id, rc == 0 ? "true" : "false");
        return 0;
    }

    /* Enable/disable specific breakpoint */
    if (strcmp(type_buf, "enable_bp") == 0) {
        int bp_id = 0;
        json_get_int(payload, "id", &bp_id);
        int rc = debug_enable_breakpoint(bp_id, 1);
        json_frame_write_fmt(wfd, CHAN_DEBUG,
            "{\"type\":\"enable_bp_ok\",\"id\":%d,\"found\":%s}",
            bp_id, rc == 0 ? "true" : "false");
        return 0;
    }
    if (strcmp(type_buf, "disable_bp") == 0) {
        int bp_id = 0;
        json_get_int(payload, "id", &bp_id);
        int rc = debug_enable_breakpoint(bp_id, 0);
        json_frame_write_fmt(wfd, CHAN_DEBUG,
            "{\"type\":\"disable_bp_ok\",\"id\":%d,\"found\":%s}",
            bp_id, rc == 0 ? "true" : "false");
        return 0;
    }

    /* List breakpoints */
    if (strcmp(type_buf, "list") == 0) {
        char *bp_list = debug_list_breakpoints();
        if (bp_list) {
            json_frame_write_fmt(wfd, CHAN_DEBUG,
                "{\"type\":\"breakpoints\",\"data\":%s}", bp_list);
            free(bp_list);
        }
        return 0;
    }

    /* Set step mode (only effective when paused — but allow pre-setting) */
    if (strcmp(type_buf, "step") == 0) {
        debug_set_step_mode(DBG_STEP);
        json_frame_write_fmt(wfd, CHAN_DEBUG,
            "{\"type\":\"step_ok\",\"mode\":\"step\"}");
        return 0;
    }
    if (strcmp(type_buf, "continue") == 0) {
        debug_set_step_mode(DBG_RUN);
        json_frame_write_fmt(wfd, CHAN_DEBUG,
            "{\"type\":\"continue_ok\"}");
        return 0;
    }

    /* Status query */
    if (strcmp(type_buf, "status") == 0) {
        const char *mode_name = "run";
        switch (dbg.step_mode) {
        case DBG_STEP:   mode_name = "step"; break;
        case DBG_NEXT:   mode_name = "next"; break;
        case DBG_FINISH: mode_name = "finish"; break;
        }
        json_frame_write_fmt(wfd, CHAN_DEBUG,
            "{\"type\":\"status\",\"active\":%s,\"mode\":\"%s\","
            "\"breakpoints\":%d,\"depth\":%d}",
            dbg.active ? "true" : "false",
            mode_name,
            debug_breakpoint_count(),
            dbg.call_depth);
        return 0;
    }

    /* Inspect AST (also available outside of breakpoint pause) */
    if (strcmp(type_buf, "inspect_ast") == 0) {
        if (dbg.pending_cmd) {
            char *ast_json = cmd_serialize(dbg.pending_cmd);
            if (ast_json) {
                json_frame_write_fmt(wfd, CHAN_DEBUG,
                    "{\"type\":\"ast\",\"data\":%s}", ast_json);
                free(ast_json);
            } else {
                json_frame_write_fmt(wfd, CHAN_DEBUG,
                    "{\"type\":\"ast\",\"data\":null}");
            }
        } else {
            json_frame_write_fmt(wfd, CHAN_DEBUG,
                "{\"type\":\"ast\",\"data\":null}");
        }
        return 0;
    }

    json_frame_write_fmt(wfd, CHAN_DEBUG,
        "{\"type\":\"error\",\"message\":\"unknown debug command\"}");
    return -1;
}

/* Set pending command for AST inspection (used by tests and external callers) */
void
debug_set_pending_cmd(COMMAND *cmd)
{
    dbg.pending_cmd = cmd;
}
