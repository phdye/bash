/* command_hooks.h - Hook infrastructure for command execution events
   
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

#ifndef _COMMAND_HOOKS_H_
#define _COMMAND_HOOKS_H_

#include "stdc.h"

/* Maximum number of hooks that can be registered */
#define MAX_COMMAND_HOOKS 16

/* Information passed to pre-command hooks */
typedef struct {
    const char *command_string;   /* The command being executed */
    const char *cwd;              /* Current working directory */
    int line_number;              /* Line number in script/input */
    int is_subshell;              /* Running in subshell? */
    int is_async;                 /* Running asynchronously? */
    struct command *command;      /* The COMMAND being executed (may be NULL) */
} pre_command_info_t;

/* Information passed to post-command hooks */
typedef struct {
    const char *command_string;   /* The command that was executed */
    int exit_status;              /* Exit status of command */
    int signal_number;            /* Signal that killed it, or 0 */
} post_command_info_t;

/* Hook function types */
typedef void (*pre_command_hook_t) PARAMS((const pre_command_info_t *info));
typedef void (*post_command_hook_t) PARAMS((const post_command_info_t *info));

/* Registration functions - return 0 on success, -1 on failure */
extern int register_pre_command_hook PARAMS((pre_command_hook_t hook));
extern int unregister_pre_command_hook PARAMS((pre_command_hook_t hook));
extern int register_post_command_hook PARAMS((post_command_hook_t hook));
extern int unregister_post_command_hook PARAMS((post_command_hook_t hook));

/* Hook invocation - called from execute_cmd.c */
extern void run_pre_command_hooks PARAMS((const pre_command_info_t *info));
extern void run_post_command_hooks PARAMS((const post_command_info_t *info));

/* Query functions */
extern int pre_command_hooks_count PARAMS((void));
extern int post_command_hooks_count PARAMS((void));

#endif /* _COMMAND_HOOKS_H_ */
