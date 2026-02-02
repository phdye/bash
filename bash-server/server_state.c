/* server_state.c -- State operations for bash-server (Phase 2) */

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
   along with Bash.  If not, see <http://www.gnu.org/licenses/>. */

#include "server.h"
#include "../shell.h"
#include "../alias.h"
#include "../trap.h"
#include "../builtins/common.h"

/* Build a comma-separated attribute string for a variable.
   Returns a static buffer. */
static const char *
var_attrs_string(SHELL_VAR *var)
{
    static char buf[256];
    char *p = buf;

    *p = '\0';

#define APPEND_ATTR(flag, name) do { \
    if (var->attributes & (flag)) { \
        if (p != buf) *p++ = ','; \
        strcpy(p, name); \
        p += strlen(name); \
    } \
} while (0)

    APPEND_ATTR(att_exported, "exported");
    APPEND_ATTR(att_readonly, "readonly");
    APPEND_ATTR(att_integer, "integer");
    APPEND_ATTR(att_local, "local");
    APPEND_ATTR(att_array, "array");
    APPEND_ATTR(att_assoc, "assoc");
    APPEND_ATTR(att_nameref, "nameref");
    APPEND_ATTR(att_uppercase, "uppercase");
    APPEND_ATTR(att_lowercase, "lowercase");
    APPEND_ATTR(att_capcase, "capcase");
    APPEND_ATTR(att_trace, "trace");

#undef APPEND_ATTR

    return buf;
}

/* GET-VAR <name>
   Response: VALUE <name> <base64-encoded-value> [attrs]
   or: ERR variable not found */
int
state_handle_get_var(int wfd, const char *arg)
{
    SHELL_VAR *var;
    const char *value;
    char *encoded;
    const char *attrs;

    if (!arg || !*arg) {
        protocol_write_line(wfd, "%s variable name required", RSP_ERR);
        return 0;
    }

    var = find_variable(arg);
    if (!var) {
        protocol_write_line(wfd, "%s variable not found: %s", RSP_ERR, arg);
        return 0;
    }

    value = var->value ? var->value : "";
    encoded = protocol_base64_encode(value, strlen(value));
    attrs = var_attrs_string(var);

    if (encoded) {
        if (attrs[0])
            protocol_write_line(wfd, "VALUE %s %s %s", arg, encoded, attrs);
        else
            protocol_write_line(wfd, "VALUE %s %s", arg, encoded);
        free(encoded);
    } else {
        protocol_write_line(wfd, "%s encoding failed", RSP_ERR);
    }

    return 0;
}

/* SET-VAR <name> <value> [--export] [--readonly] [--integer]
   Response: OK or ERR */
int
state_handle_set_var(int wfd, const char *arg)
{
    char name[256];
    char value[SERVER_MAX_CMD];
    const char *p;
    int do_export = 0, do_readonly = 0, do_integer = 0;
    SHELL_VAR *var;
    size_t i;

    if (!arg || !*arg) {
        protocol_write_line(wfd, "%s variable name required", RSP_ERR);
        return 0;
    }

    /* Parse name (first word) */
    p = arg;
    i = 0;
    while (*p && *p != ' ' && i < sizeof(name) - 1)
        name[i++] = *p++;
    name[i] = '\0';

    /* Skip space */
    while (*p == ' ') p++;

    /* Parse value - everything up to first --flag or end */
    i = 0;
    while (*p && i < sizeof(value) - 1) {
        /* Check for flags */
        if (*p == '-' && *(p+1) == '-') {
            if (strncmp(p, "--export", 8) == 0 && (p[8] == '\0' || p[8] == ' ')) {
                do_export = 1;
                p += 8;
                while (*p == ' ') p++;
                continue;
            }
            if (strncmp(p, "--readonly", 10) == 0 && (p[10] == '\0' || p[10] == ' ')) {
                do_readonly = 1;
                p += 10;
                while (*p == ' ') p++;
                continue;
            }
            if (strncmp(p, "--integer", 9) == 0 && (p[9] == '\0' || p[9] == ' ')) {
                do_integer = 1;
                p += 9;
                while (*p == ' ') p++;
                continue;
            }
        }
        value[i++] = *p++;
    }
    value[i] = '\0';

    /* Trim trailing whitespace from value */
    while (i > 0 && value[i-1] == ' ')
        value[--i] = '\0';

    var = bind_variable(name, value, 0);
    if (!var) {
        protocol_write_line(wfd, "%s bind_variable failed for %s", RSP_ERR, name);
        return 0;
    }

    if (do_export)
        var->attributes |= att_exported;
    if (do_readonly)
        var->attributes |= att_readonly;
    if (do_integer)
        var->attributes |= att_integer;

    protocol_write_line(wfd, "%s", RSP_OK);
    return 0;
}

/* UNSET-VAR <name>
   Response: OK or ERR */
int
state_handle_unset_var(int wfd, const char *arg)
{
    if (!arg || !*arg) {
        protocol_write_line(wfd, "%s variable name required", RSP_ERR);
        return 0;
    }

    /* Check if readonly */
    {
        SHELL_VAR *var = find_variable(arg);
        if (var && readonly_p(var)) {
            protocol_write_line(wfd, "%s %s: readonly variable", RSP_ERR, arg);
            return 0;
        }
    }

    unbind_variable(arg);
    protocol_write_line(wfd, "%s", RSP_OK);
    return 0;
}

/* GET-FUNC <name>
   Response: FUNC <name> <base64-encoded-definition>
   or: ERR function not found */
int
state_handle_get_func(int wfd, const char *arg)
{
    SHELL_VAR *func;
    char *def;
    char *encoded;

    if (!arg || !*arg) {
        protocol_write_line(wfd, "%s function name required", RSP_ERR);
        return 0;
    }

    func = find_function(arg);
    if (!func) {
        protocol_write_line(wfd, "%s function not found: %s", RSP_ERR, arg);
        return 0;
    }

    def = named_function_string((char *)arg, function_cell(func), FUNC_MULTILINE);
    if (def) {
        encoded = protocol_base64_encode(def, strlen(def));
        if (encoded) {
            protocol_write_line(wfd, "FUNC %s %s", arg, encoded);
            free(encoded);
        } else {
            protocol_write_line(wfd, "%s encoding failed", RSP_ERR);
        }
    } else {
        protocol_write_line(wfd, "%s cannot get function definition: %s", RSP_ERR, arg);
    }

    return 0;
}

/* UNSET-FUNC <name>
   Uses EVAL to call `unset -f name` since there's no direct C API for it. */
int
state_handle_unset_func(int wfd, const char *arg)
{
    SHELL_VAR *func;
    char cmd[512];
    char *cmd_copy;

    if (!arg || !*arg) {
        protocol_write_line(wfd, "%s function name required", RSP_ERR);
        return 0;
    }

    func = find_function(arg);
    if (!func) {
        protocol_write_line(wfd, "%s function not found: %s", RSP_ERR, arg);
        return 0;
    }

    /* Use bash builtin to unset function */
    snprintf(cmd, sizeof(cmd), "unset -f %s", arg);
    cmd_copy = strdup(cmd);
    if (cmd_copy) {
        parse_and_execute(cmd_copy, "bash-server", SEVAL_NONINT | SEVAL_NOHIST);
    }

    protocol_write_line(wfd, "%s", RSP_OK);
    return 0;
}

/* GET-ALIAS <name>
   Response: ALIAS <name> <base64-encoded-value>
   or: ERR alias not found */
int
state_handle_get_alias(int wfd, const char *arg)
{
    char *value;
    char *encoded;

    if (!arg || !*arg) {
        protocol_write_line(wfd, "%s alias name required", RSP_ERR);
        return 0;
    }

    value = get_alias_value((char *)arg);
    if (!value) {
        protocol_write_line(wfd, "%s alias not found: %s", RSP_ERR, arg);
        return 0;
    }

    encoded = protocol_base64_encode(value, strlen(value));
    if (encoded) {
        protocol_write_line(wfd, "ALIAS %s %s", arg, encoded);
        free(encoded);
    } else {
        protocol_write_line(wfd, "%s encoding failed", RSP_ERR);
    }

    return 0;
}

/* SET-ALIAS <name> <value>
   Response: OK */
int
state_handle_set_alias(int wfd, const char *arg)
{
    char name[256];
    const char *p;
    size_t i;

    if (!arg || !*arg) {
        protocol_write_line(wfd, "%s alias name required", RSP_ERR);
        return 0;
    }

    /* Parse name */
    p = arg;
    i = 0;
    while (*p && *p != ' ' && i < sizeof(name) - 1)
        name[i++] = *p++;
    name[i] = '\0';

    while (*p == ' ') p++;

    if (!*p) {
        protocol_write_line(wfd, "%s alias value required", RSP_ERR);
        return 0;
    }

    add_alias(name, (char *)p);
    protocol_write_line(wfd, "%s", RSP_OK);
    return 0;
}

/* UNSET-ALIAS <name>
   Response: OK or ERR */
int
state_handle_unset_alias(int wfd, const char *arg)
{
    if (!arg || !*arg) {
        protocol_write_line(wfd, "%s alias name required", RSP_ERR);
        return 0;
    }

    if (remove_alias((char *)arg) < 0) {
        protocol_write_line(wfd, "%s alias not found: %s", RSP_ERR, arg);
        return 0;
    }

    protocol_write_line(wfd, "%s", RSP_OK);
    return 0;
}

/* SET-TRAP <signal> <command>
   Response: OK or ERR */
int
state_handle_set_trap(int wfd, const char *arg)
{
    char signame[64];
    const char *p;
    size_t i;
    int sig;

    if (!arg || !*arg) {
        protocol_write_line(wfd, "%s signal name required", RSP_ERR);
        return 0;
    }

    /* Parse signal name */
    p = arg;
    i = 0;
    while (*p && *p != ' ' && i < sizeof(signame) - 1)
        signame[i++] = *p++;
    signame[i] = '\0';
    while (*p == ' ') p++;

    sig = decode_signal(signame, DSIG_NOCASE);
    if (sig == NO_SIG) {
        protocol_write_line(wfd, "%s unknown signal: %s", RSP_ERR, signame);
        return 0;
    }

    if (!*p) {
        protocol_write_line(wfd, "%s trap command required", RSP_ERR);
        return 0;
    }

    set_signal(sig, (char *)p);
    protocol_write_line(wfd, "%s", RSP_OK);
    return 0;
}

/* UNSET-TRAP <signal>
   Response: OK or ERR */
int
state_handle_unset_trap(int wfd, const char *arg)
{
    int sig;

    if (!arg || !*arg) {
        protocol_write_line(wfd, "%s signal name required", RSP_ERR);
        return 0;
    }

    sig = decode_signal((char *)arg, DSIG_NOCASE);
    if (sig == NO_SIG) {
        protocol_write_line(wfd, "%s unknown signal: %s", RSP_ERR, arg);
        return 0;
    }

    restore_default_signal(sig);
    protocol_write_line(wfd, "%s", RSP_OK);
    return 0;
}

/* INSPECT <target> [pattern]
   Targets: vars, functions, aliases, traps, options
   Sends multiple VALUE/FUNC/ALIAS lines followed by INSPECT-END */
int
state_handle_inspect(int wfd, const char *arg)
{
    char target[64];
    const char *pattern = NULL;
    const char *p;
    size_t i;

    if (!arg || !*arg) {
        protocol_write_line(wfd, "%s inspect target required "
                            "(vars, functions, aliases, traps)", RSP_ERR);
        return 0;
    }

    /* Parse target */
    p = arg;
    i = 0;
    while (*p && *p != ' ' && i < sizeof(target) - 1)
        target[i++] = *p++;
    target[i] = '\0';
    while (*p == ' ') p++;
    if (*p)
        pattern = p;

    if (strcmp(target, "vars") == 0) {
        SHELL_VAR **vars = all_visible_variables();
        if (vars) {
            for (i = 0; vars[i]; i++) {
                SHELL_VAR *v = vars[i];
                /* Apply pattern filter if given */
                if (pattern && strncmp(v->name, pattern, strlen(pattern)) != 0)
                    continue;
                const char *val = v->value ? v->value : "";
                char *enc = protocol_base64_encode(val, strlen(val));
                const char *attrs = var_attrs_string(v);
                if (enc) {
                    if (attrs[0])
                        protocol_write_line(wfd, "VALUE %s %s %s", v->name, enc, attrs);
                    else
                        protocol_write_line(wfd, "VALUE %s %s", v->name, enc);
                    free(enc);
                }
            }
            free(vars);
        }
        protocol_write_line(wfd, "INSPECT-END");

    } else if (strcmp(target, "functions") == 0) {
        SHELL_VAR **funcs = all_shell_functions();
        if (funcs) {
            for (i = 0; funcs[i]; i++) {
                SHELL_VAR *f = funcs[i];
                if (pattern && strncmp(f->name, pattern, strlen(pattern)) != 0)
                    continue;
                char *def = named_function_string(f->name, function_cell(f), FUNC_MULTILINE);
                if (def) {
                    char *enc = protocol_base64_encode(def, strlen(def));
                    if (enc) {
                        protocol_write_line(wfd, "FUNC %s %s", f->name, enc);
                        free(enc);
                    }
                }
            }
            free(funcs);
        }
        protocol_write_line(wfd, "INSPECT-END");

    } else if (strcmp(target, "aliases") == 0) {
        alias_t **alist = all_aliases();
        if (alist) {
            for (i = 0; alist[i]; i++) {
                alias_t *a = alist[i];
                if (pattern && strncmp(a->name, pattern, strlen(pattern)) != 0)
                    continue;
                char *enc = protocol_base64_encode(a->value, strlen(a->value));
                if (enc) {
                    protocol_write_line(wfd, "ALIAS %s %s", a->name, enc);
                    free(enc);
                }
            }
            free(alist);
        }
        protocol_write_line(wfd, "INSPECT-END");

    } else if (strcmp(target, "traps") == 0) {
        for (i = 0; i < BASH_NSIG; i++) {
            if (trap_list[i] != NULL) {
                char *enc = protocol_base64_encode(trap_list[i], strlen(trap_list[i]));
                if (enc) {
                    protocol_write_line(wfd, "TRAP %s %s", signal_name(i), enc);
                    free(enc);
                }
            }
        }
        protocol_write_line(wfd, "INSPECT-END");

    } else {
        protocol_write_line(wfd, "%s unknown inspect target: %s", RSP_ERR, target);
    }

    return 0;
}
