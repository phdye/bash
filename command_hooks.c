/* command_hooks.c - Hook infrastructure for command execution events
   
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

#include "config.h"

#if defined (HAVE_UNISTD_H)
#  include <unistd.h>
#endif

#include "bashansi.h"
#include "command_hooks.h"

/* Static arrays for registered hooks */
static pre_command_hook_t pre_hooks[MAX_COMMAND_HOOKS];
static post_command_hook_t post_hooks[MAX_COMMAND_HOOKS];
static int num_pre_hooks = 0;
static int num_post_hooks = 0;

/* Register a pre-command hook. Returns 0 on success, -1 if full. */
int
register_pre_command_hook (hook)
     pre_command_hook_t hook;
{
  int i;
  
  if (hook == NULL)
    return -1;
    
  /* Check if already registered */
  for (i = 0; i < num_pre_hooks; i++)
    if (pre_hooks[i] == hook)
      return 0;  /* Already registered, success */
      
  if (num_pre_hooks >= MAX_COMMAND_HOOKS)
    return -1;  /* No room */
    
  pre_hooks[num_pre_hooks++] = hook;
  return 0;
}

/* Unregister a pre-command hook. Returns 0 on success, -1 if not found. */
int
unregister_pre_command_hook (hook)
     pre_command_hook_t hook;
{
  int i, j;
  
  for (i = 0; i < num_pre_hooks; i++)
    {
      if (pre_hooks[i] == hook)
        {
          /* Shift remaining hooks down */
          for (j = i; j < num_pre_hooks - 1; j++)
            pre_hooks[j] = pre_hooks[j + 1];
          num_pre_hooks--;
          return 0;
        }
    }
  return -1;  /* Not found */
}

/* Register a post-command hook. Returns 0 on success, -1 if full. */
int
register_post_command_hook (hook)
     post_command_hook_t hook;
{
  int i;
  
  if (hook == NULL)
    return -1;
    
  /* Check if already registered */
  for (i = 0; i < num_post_hooks; i++)
    if (post_hooks[i] == hook)
      return 0;  /* Already registered, success */
      
  if (num_post_hooks >= MAX_COMMAND_HOOKS)
    return -1;  /* No room */
    
  post_hooks[num_post_hooks++] = hook;
  return 0;
}

/* Unregister a post-command hook. Returns 0 on success, -1 if not found. */
int
unregister_post_command_hook (hook)
     post_command_hook_t hook;
{
  int i, j;
  
  for (i = 0; i < num_post_hooks; i++)
    {
      if (post_hooks[i] == hook)
        {
          /* Shift remaining hooks down */
          for (j = i; j < num_post_hooks - 1; j++)
            post_hooks[j] = post_hooks[j + 1];
          num_post_hooks--;
          return 0;
        }
    }
  return -1;  /* Not found */
}

/* Run all registered pre-command hooks */
void
run_pre_command_hooks (info)
     const pre_command_info_t *info;
{
  int i;
  
  for (i = 0; i < num_pre_hooks; i++)
    {
      if (pre_hooks[i])
        (*pre_hooks[i])(info);
    }
}

/* Run all registered post-command hooks */
void
run_post_command_hooks (info)
     const post_command_info_t *info;
{
  int i;
  
  for (i = 0; i < num_post_hooks; i++)
    {
      if (post_hooks[i])
        (*post_hooks[i])(info);
    }
}

/* Query: how many pre-command hooks are registered? */
int
pre_command_hooks_count ()
{
  return num_pre_hooks;
}

/* Query: how many post-command hooks are registered? */
int
post_command_hooks_count ()
{
  return num_post_hooks;
}
