/* cmd_serialize.c -- COMMAND tree <-> JSON serialization for bash-server
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
 * Phase 6 (architecture §9): Provides bidirectional serialization between
 * bash's internal COMMAND tree structures and JSON, enabling:
 *   - Pre-parsed command execution: client sends AST as JSON, server
 *     deserializes to COMMAND and calls execute_command() directly
 *   - AST inspection: debugger serializes pending COMMAND to JSON for
 *     client inspection/modification at breakpoints
 */

#include "server.h"
#include "command.h"
#include "general.h"  /* WORD_DESC, WORD_LIST */

/* ================================================================
 * Growable string buffer for JSON construction
 * ================================================================ */

typedef struct serbuf {
    char  *data;
    size_t len;
    size_t cap;
} serbuf_t;

static void
serbuf_init(serbuf_t *sb, size_t initial)
{
    sb->cap = initial > 64 ? initial : 64;
    sb->data = malloc(sb->cap);
    sb->len = 0;
    if (sb->data)
        sb->data[0] = '\0';
}

static void
serbuf_append(serbuf_t *sb, const char *s, size_t slen)
{
    size_t needed;

    if (!sb->data || !s)
        return;

    needed = sb->len + slen + 1;
    if (needed > sb->cap) {
        size_t newcap = sb->cap * 2;
        char *p;
        while (newcap < needed)
            newcap *= 2;
        p = realloc(sb->data, newcap);
        if (!p)
            return;
        sb->data = p;
        sb->cap = newcap;
    }
    memcpy(sb->data + sb->len, s, slen);
    sb->len += slen;
    sb->data[sb->len] = '\0';
}

static void
serbuf_append_str(serbuf_t *sb, const char *s)
{
    if (s)
        serbuf_append(sb, s, strlen(s));
}

/* Append a JSON-escaped string (with surrounding quotes) */
static void
serbuf_append_json_str(serbuf_t *sb, const char *s)
{
    const char *p;

    serbuf_append(sb, "\"", 1);
    if (s) {
        for (p = s; *p; p++) {
            switch (*p) {
            case '"':  serbuf_append(sb, "\\\"", 2); break;
            case '\\': serbuf_append(sb, "\\\\", 2); break;
            case '\n': serbuf_append(sb, "\\n", 2);  break;
            case '\r': serbuf_append(sb, "\\r", 2);  break;
            case '\t': serbuf_append(sb, "\\t", 2);  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof(esc), "\\u%04x", (unsigned char)*p);
                    serbuf_append_str(sb, esc);
                } else {
                    serbuf_append(sb, p, 1);
                }
            }
        }
    }
    serbuf_append(sb, "\"", 1);
}

static void
serbuf_append_int(serbuf_t *sb, int n)
{
    char buf[32];
    snprintf(buf, sizeof(buf), "%d", n);
    serbuf_append_str(sb, buf);
}

static char *
serbuf_finish(serbuf_t *sb)
{
    return sb->data;  /* Caller owns the memory */
}

/* ================================================================
 * Command type name mapping
 * ================================================================ */

static const char *
cmd_type_name(enum command_type type)
{
    switch (type) {
    case cm_simple:       return "cm_simple";
    case cm_connection:   return "cm_connection";
    case cm_for:          return "cm_for";
    case cm_if:           return "cm_if";
    case cm_while:        return "cm_while";
    case cm_until:        return "cm_until";
    case cm_case:         return "cm_case";
    case cm_group:        return "cm_group";
    case cm_subshell:     return "cm_subshell";
    case cm_function_def: return "cm_function_def";
#if defined (SELECT_COMMAND)
    case cm_select:       return "cm_select";
#endif
#if defined (DPAREN_ARITHMETIC)
    case cm_arith:        return "cm_arith";
#endif
#if defined (COND_COMMAND)
    case cm_cond:         return "cm_cond";
#endif
#if defined (ARITH_FOR_COMMAND)
    case cm_arith_for:    return "cm_arith_for";
#endif
    case cm_coproc:       return "cm_coproc";
    default:              return "cm_unknown";
    }
}

int
cmd_type_from_name(const char *name)
{
    if (strcmp(name, "cm_simple") == 0)       return cm_simple;
    if (strcmp(name, "cm_connection") == 0)   return cm_connection;
    if (strcmp(name, "cm_for") == 0)          return cm_for;
    if (strcmp(name, "cm_if") == 0)           return cm_if;
    if (strcmp(name, "cm_while") == 0)        return cm_while;
    if (strcmp(name, "cm_until") == 0)        return cm_until;
    if (strcmp(name, "cm_case") == 0)         return cm_case;
    if (strcmp(name, "cm_group") == 0)        return cm_group;
    if (strcmp(name, "cm_subshell") == 0)     return cm_subshell;
    if (strcmp(name, "cm_function_def") == 0) return cm_function_def;
#if defined (SELECT_COMMAND)
    if (strcmp(name, "cm_select") == 0)       return cm_select;
#endif
#if defined (DPAREN_ARITHMETIC)
    if (strcmp(name, "cm_arith") == 0)        return cm_arith;
#endif
#if defined (COND_COMMAND)
    if (strcmp(name, "cm_cond") == 0)         return cm_cond;
#endif
#if defined (ARITH_FOR_COMMAND)
    if (strcmp(name, "cm_arith_for") == 0)    return cm_arith_for;
#endif
    if (strcmp(name, "cm_coproc") == 0)       return cm_coproc;
    return -1;
}

/* Connector int → string name */
static const char *
connector_name(int conn)
{
    switch (conn) {
    case ';':  return ";";
    case '|':  return "|";
    case '&':  return "&";
    case 288:  return "&&";   /* AND_AND */
    case 289:  return "||";   /* OR_OR */
    default:   return "?";
    }
}

int
connector_from_name(const char *name)
{
    if (strcmp(name, ";") == 0)    return ';';
    if (strcmp(name, "|") == 0)    return '|';
    if (strcmp(name, "&") == 0)    return '&';
    if (strcmp(name, "&&") == 0)   return 288;  /* AND_AND */
    if (strcmp(name, "||") == 0)   return 289;  /* OR_OR */
    return ';';  /* default */
}

/* ================================================================
 * Serialization: COMMAND → JSON
 * ================================================================ */

static void cmd_serialize_recursive(serbuf_t *sb, COMMAND *cmd);

/* Serialize a WORD_LIST as JSON array */
static void
serialize_word_list(serbuf_t *sb, WORD_LIST *list)
{
    WORD_LIST *w;
    int first = 1;

    serbuf_append_str(sb, "[");
    for (w = list; w; w = w->next) {
        if (!first)
            serbuf_append_str(sb, ",");
        first = 0;
        serbuf_append_str(sb, "{\"word\":");
        serbuf_append_json_str(sb, w->word ? w->word->word : "");
        serbuf_append_str(sb, ",\"flags\":");
        serbuf_append_int(sb, w->word ? w->word->flags : 0);
        serbuf_append_str(sb, "}");
    }
    serbuf_append_str(sb, "]");
}

/* Serialize REDIRECT chain as JSON array */
static void
serialize_redirects(serbuf_t *sb, REDIRECT *redir)
{
    REDIRECT *r;
    int first = 1;

    if (!redir) {
        serbuf_append_str(sb, "null");
        return;
    }

    serbuf_append_str(sb, "[");
    for (r = redir; r; r = r->next) {
        if (!first)
            serbuf_append_str(sb, ",");
        first = 0;
        serbuf_append_str(sb, "{\"instruction\":");
        serbuf_append_int(sb, (int)r->instruction);
        serbuf_append_str(sb, ",\"redirector\":");
        serbuf_append_int(sb, r->redirector.dest);
        serbuf_append_str(sb, ",\"flags\":");
        serbuf_append_int(sb, r->flags);
        serbuf_append_str(sb, ",\"rflags\":");
        serbuf_append_int(sb, r->rflags);
        if (r->redirectee.filename) {
            serbuf_append_str(sb, ",\"filename\":");
            serbuf_append_json_str(sb, r->redirectee.filename->word);
        } else {
            serbuf_append_str(sb, ",\"dest\":");
            serbuf_append_int(sb, r->redirectee.dest);
        }
        if (r->here_doc_eof) {
            serbuf_append_str(sb, ",\"here_doc_eof\":");
            serbuf_append_json_str(sb, r->here_doc_eof);
        }
        serbuf_append_str(sb, "}");
    }
    serbuf_append_str(sb, "]");
}

static void
serialize_simple(serbuf_t *sb, SIMPLE_COM *s)
{
    serbuf_append_str(sb, "\"simple\":{");
    serbuf_append_str(sb, "\"flags\":");
    serbuf_append_int(sb, s->flags);
    serbuf_append_str(sb, ",\"line\":");
    serbuf_append_int(sb, s->line);
    serbuf_append_str(sb, ",\"words\":");
    serialize_word_list(sb, s->words);
    serbuf_append_str(sb, ",\"redirects\":");
    serialize_redirects(sb, s->redirects);
    serbuf_append_str(sb, "}");
}

static void
serialize_connection(serbuf_t *sb, CONNECTION *c)
{
    serbuf_append_str(sb, "\"connection\":{");
    serbuf_append_str(sb, "\"connector\":");
    serbuf_append_json_str(sb, connector_name(c->connector));
    serbuf_append_str(sb, ",\"first\":");
    cmd_serialize_recursive(sb, c->first);
    serbuf_append_str(sb, ",\"second\":");
    cmd_serialize_recursive(sb, c->second);
    serbuf_append_str(sb, "}");
}

static void
serialize_for(serbuf_t *sb, FOR_COM *f)
{
    serbuf_append_str(sb, "\"for\":{");
    serbuf_append_str(sb, "\"flags\":");
    serbuf_append_int(sb, f->flags);
    serbuf_append_str(sb, ",\"line\":");
    serbuf_append_int(sb, f->line);
    serbuf_append_str(sb, ",\"name\":");
    serbuf_append_json_str(sb, f->name ? f->name->word : "");
    serbuf_append_str(sb, ",\"map_list\":");
    serialize_word_list(sb, f->map_list);
    serbuf_append_str(sb, ",\"action\":");
    cmd_serialize_recursive(sb, f->action);
    serbuf_append_str(sb, "}");
}

static void
serialize_if(serbuf_t *sb, IF_COM *i)
{
    serbuf_append_str(sb, "\"if\":{");
    serbuf_append_str(sb, "\"flags\":");
    serbuf_append_int(sb, i->flags);
    serbuf_append_str(sb, ",\"test\":");
    cmd_serialize_recursive(sb, i->test);
    serbuf_append_str(sb, ",\"true_case\":");
    cmd_serialize_recursive(sb, i->true_case);
    serbuf_append_str(sb, ",\"false_case\":");
    cmd_serialize_recursive(sb, i->false_case);
    serbuf_append_str(sb, "}");
}

static void
serialize_while(serbuf_t *sb, WHILE_COM *w)
{
    serbuf_append_str(sb, "\"while\":{");
    serbuf_append_str(sb, "\"flags\":");
    serbuf_append_int(sb, w->flags);
    serbuf_append_str(sb, ",\"test\":");
    cmd_serialize_recursive(sb, w->test);
    serbuf_append_str(sb, ",\"action\":");
    cmd_serialize_recursive(sb, w->action);
    serbuf_append_str(sb, "}");
}

static void
serialize_case(serbuf_t *sb, CASE_COM *c)
{
    PATTERN_LIST *p;
    int first;

    serbuf_append_str(sb, "\"case\":{");
    serbuf_append_str(sb, "\"flags\":");
    serbuf_append_int(sb, c->flags);
    serbuf_append_str(sb, ",\"line\":");
    serbuf_append_int(sb, c->line);
    serbuf_append_str(sb, ",\"word\":");
    serbuf_append_json_str(sb, c->word ? c->word->word : "");
    serbuf_append_str(sb, ",\"clauses\":[");
    first = 1;
    for (p = c->clauses; p; p = p->next) {
        if (!first)
            serbuf_append_str(sb, ",");
        first = 0;
        serbuf_append_str(sb, "{\"patterns\":");
        serialize_word_list(sb, p->patterns);
        serbuf_append_str(sb, ",\"action\":");
        cmd_serialize_recursive(sb, p->action);
        serbuf_append_str(sb, ",\"flags\":");
        serbuf_append_int(sb, p->flags);
        serbuf_append_str(sb, "}");
    }
    serbuf_append_str(sb, "]}");
}

static void
serialize_group(serbuf_t *sb, GROUP_COM *g)
{
    serbuf_append_str(sb, "\"group\":{\"command\":");
    cmd_serialize_recursive(sb, g->command);
    serbuf_append_str(sb, "}");
}

static void
serialize_subshell(serbuf_t *sb, SUBSHELL_COM *s)
{
    serbuf_append_str(sb, "\"subshell\":{");
    serbuf_append_str(sb, "\"flags\":");
    serbuf_append_int(sb, s->flags);
    serbuf_append_str(sb, ",\"line\":");
    serbuf_append_int(sb, s->line);
    serbuf_append_str(sb, ",\"command\":");
    cmd_serialize_recursive(sb, s->command);
    serbuf_append_str(sb, "}");
}

static void
serialize_function_def(serbuf_t *sb, FUNCTION_DEF *f)
{
    serbuf_append_str(sb, "\"function_def\":{");
    serbuf_append_str(sb, "\"flags\":");
    serbuf_append_int(sb, f->flags);
    serbuf_append_str(sb, ",\"line\":");
    serbuf_append_int(sb, f->line);
    serbuf_append_str(sb, ",\"name\":");
    serbuf_append_json_str(sb, f->name ? f->name->word : "");
    if (f->source_file) {
        serbuf_append_str(sb, ",\"source_file\":");
        serbuf_append_json_str(sb, f->source_file);
    }
    serbuf_append_str(sb, ",\"command\":");
    cmd_serialize_recursive(sb, f->command);
    serbuf_append_str(sb, "}");
}

static void
cmd_serialize_recursive(serbuf_t *sb, COMMAND *cmd)
{
    if (!cmd) {
        serbuf_append_str(sb, "null");
        return;
    }

    serbuf_append_str(sb, "{\"type\":");
    serbuf_append_json_str(sb, cmd_type_name(cmd->type));
    serbuf_append_str(sb, ",\"flags\":");
    serbuf_append_int(sb, cmd->flags);
    serbuf_append_str(sb, ",\"line\":");
    serbuf_append_int(sb, cmd->line);

    if (cmd->redirects) {
        serbuf_append_str(sb, ",\"redirects\":");
        serialize_redirects(sb, cmd->redirects);
    }

    serbuf_append_str(sb, ",");

    switch (cmd->type) {
    case cm_simple:
        if (cmd->value.Simple)
            serialize_simple(sb, cmd->value.Simple);
        break;
    case cm_connection:
        if (cmd->value.Connection)
            serialize_connection(sb, cmd->value.Connection);
        break;
    case cm_for:
        if (cmd->value.For)
            serialize_for(sb, cmd->value.For);
        break;
    case cm_if:
        if (cmd->value.If)
            serialize_if(sb, cmd->value.If);
        break;
    case cm_while:
    case cm_until:
        if (cmd->value.While)
            serialize_while(sb, cmd->value.While);
        break;
    case cm_case:
        if (cmd->value.Case)
            serialize_case(sb, cmd->value.Case);
        break;
    case cm_group:
        if (cmd->value.Group)
            serialize_group(sb, cmd->value.Group);
        break;
    case cm_subshell:
        if (cmd->value.Subshell)
            serialize_subshell(sb, cmd->value.Subshell);
        break;
    case cm_function_def:
        if (cmd->value.Function_def)
            serialize_function_def(sb, cmd->value.Function_def);
        break;
    default:
        serbuf_append_str(sb, "\"unsupported\":true");
        break;
    }

    serbuf_append_str(sb, "}");
}

/* Serialize a COMMAND tree to a JSON string.
 * Returns a malloc'd string that the caller must free.
 * Returns NULL on error. */
char *
cmd_serialize(COMMAND *cmd)
{
    serbuf_t sb;

    if (!cmd)
        return strdup("null");

    serbuf_init(&sb, 1024);
    if (!sb.data)
        return NULL;

    cmd_serialize_recursive(&sb, cmd);

    return serbuf_finish(&sb);
}

/* ================================================================
 * Deserialization: JSON → COMMAND
 *
 * Uses direct struct allocation for portability and testability.
 * The resulting COMMAND trees are compatible with execute_command().
 * ================================================================ */

/* Forward declaration */
static COMMAND *cmd_deserialize_recursive(const char *json);

/* Helper: find a JSON key's value start position.
 * Returns pointer to the value, or NULL if not found.
 * This is a simplified JSON parser that handles nested structures. */
static const char *
json_find_key(const char *json, const char *key)
{
    char search[256];
    const char *p;

    snprintf(search, sizeof(search), "\"%s\":", key);
    p = strstr(json, search);
    if (!p)
        return NULL;

    p += strlen(search);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;
    return p;
}

/* Find the end of a JSON value (handles nested {}, [], strings, numbers, null) */
static const char *
json_skip_value(const char *p)
{
    int depth;

    if (!p)
        return NULL;

    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
        p++;

    if (*p == '"') {
        /* String */
        p++;
        while (*p && !(*p == '"' && *(p-1) != '\\'))
            p++;
        if (*p == '"')
            p++;
        return p;
    }
    if (*p == '{' || *p == '[') {
        /* Object or array */
        char open = *p;
        char close = (open == '{') ? '}' : ']';
        depth = 1;
        p++;
        while (*p && depth > 0) {
            if (*p == '"') {
                /* Skip string */
                p++;
                while (*p && !(*p == '"' && *(p-1) != '\\'))
                    p++;
                if (*p == '"')
                    p++;
                continue;
            }
            if (*p == open)
                depth++;
            else if (*p == close)
                depth--;
            p++;
        }
        return p;
    }
    if (strncmp(p, "null", 4) == 0)
        return p + 4;
    if (strncmp(p, "true", 4) == 0)
        return p + 4;
    if (strncmp(p, "false", 5) == 0)
        return p + 5;
    /* Number */
    if (*p == '-')
        p++;
    while (*p >= '0' && *p <= '9')
        p++;
    return p;
}

/* Extract a JSON string value (unescaped).
 * Returns malloc'd string or NULL. */
static char *
json_extract_string(const char *p)
{
    const char *start;
    char *result, *w;

    if (!p || *p != '"')
        return NULL;

    p++;  /* skip opening quote */
    start = p;

    /* Find end */
    while (*p && !(*p == '"' && *(p-1) != '\\'))
        p++;

    result = malloc(p - start + 1);
    if (!result)
        return NULL;

    /* Unescape */
    w = result;
    for (p = start; *p && !(*p == '"' && *(p-1) != '\\'); p++) {
        if (*p == '\\' && *(p+1)) {
            p++;
            switch (*p) {
            case '"':  *w++ = '"'; break;
            case '\\': *w++ = '\\'; break;
            case 'n':  *w++ = '\n'; break;
            case 'r':  *w++ = '\r'; break;
            case 't':  *w++ = '\t'; break;
            default:   *w++ = *p; break;
            }
        } else {
            *w++ = *p;
        }
    }
    *w = '\0';
    return result;
}

/* Extract JSON int value */
static int
json_extract_int(const char *p)
{
    if (!p)
        return 0;
    while (*p == ' ')
        p++;
    return atoi(p);
}

/* Check if value at p is "null" */
static int
json_is_null(const char *p)
{
    if (!p)
        return 1;
    while (*p == ' ')
        p++;
    return strncmp(p, "null", 4) == 0;
}

/* Allocate a WORD_DESC */
static WORD_DESC *
alloc_word(const char *text, int flags)
{
    WORD_DESC *w = calloc(1, sizeof(WORD_DESC));
    if (w) {
        w->word = text ? strdup(text) : strdup("");
        w->flags = flags;
    }
    return w;
}

/* Free a WORD_DESC */
void
cmd_free_word(WORD_DESC *w)
{
    if (w) {
        free(w->word);
        free(w);
    }
}

/* Free a WORD_LIST */
void
cmd_free_word_list(WORD_LIST *list)
{
    WORD_LIST *next;
    while (list) {
        next = list->next;
        cmd_free_word(list->word);
        free(list);
        list = next;
    }
}

/* Deserialize a JSON array of word objects into a WORD_LIST */
static WORD_LIST *
deserialize_word_list(const char *json)
{
    WORD_LIST *head = NULL, *tail = NULL;
    const char *p;

    if (!json || *json != '[')
        return NULL;

    p = json + 1;  /* skip '[' */

    while (*p) {
        WORD_LIST *node;
        WORD_DESC *wd;
        const char *word_val, *flags_val;
        char *word_str;
        int flags;

        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t')
            p++;
        if (*p == ']')
            break;
        if (*p != '{')
            break;

        /* Parse word object */
        word_val = json_find_key(p, "word");
        flags_val = json_find_key(p, "flags");

        word_str = json_extract_string(word_val);
        flags = flags_val ? json_extract_int(flags_val) : 0;

        wd = alloc_word(word_str, flags);
        free(word_str);

        node = calloc(1, sizeof(WORD_LIST));
        if (node) {
            node->word = wd;
            node->next = NULL;
            if (tail)
                tail->next = node;
            else
                head = node;
            tail = node;
        }

        /* Skip to end of this object */
        p = json_skip_value(p);
    }

    return head;
}

/* Deserialize redirects array → REDIRECT chain */
static REDIRECT *
deserialize_redirects(const char *json)
{
    REDIRECT *head = NULL, *tail = NULL;
    const char *p;

    if (!json || json_is_null(json) || *json != '[')
        return NULL;

    p = json + 1;

    while (*p) {
        REDIRECT *r;
        const char *v;

        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t')
            p++;
        if (*p == ']')
            break;
        if (*p != '{')
            break;

        r = calloc(1, sizeof(REDIRECT));
        if (!r)
            break;

        v = json_find_key(p, "instruction");
        r->instruction = v ? json_extract_int(v) : r_output_direction;

        v = json_find_key(p, "redirector");
        r->redirector.dest = v ? json_extract_int(v) : 1;

        v = json_find_key(p, "flags");
        r->flags = v ? json_extract_int(v) : 0;

        v = json_find_key(p, "rflags");
        r->rflags = v ? json_extract_int(v) : 0;

        v = json_find_key(p, "filename");
        if (v && *v == '"') {
            char *fn = json_extract_string(v);
            r->redirectee.filename = alloc_word(fn, 0);
            free(fn);
        } else {
            v = json_find_key(p, "dest");
            r->redirectee.dest = v ? json_extract_int(v) : 0;
        }

        v = json_find_key(p, "here_doc_eof");
        if (v && *v == '"')
            r->here_doc_eof = json_extract_string(v);

        r->next = NULL;
        if (tail)
            tail->next = r;
        else
            head = r;
        tail = r;

        p = json_skip_value(p);
    }

    return head;
}

static void cmd_free_recursive(COMMAND *cmd);

/* Deserialize a simple command from its sub-object */
static COMMAND *
deserialize_simple(const char *json, int cmd_flags, int cmd_line, REDIRECT *cmd_redirects)
{
    COMMAND *cmd;
    SIMPLE_COM *s;
    const char *v;

    s = calloc(1, sizeof(SIMPLE_COM));
    if (!s)
        return NULL;

    v = json_find_key(json, "flags");
    s->flags = v ? json_extract_int(v) : 0;

    v = json_find_key(json, "line");
    s->line = v ? json_extract_int(v) : cmd_line;

    v = json_find_key(json, "words");
    s->words = v ? deserialize_word_list(v) : NULL;

    v = json_find_key(json, "redirects");
    s->redirects = (v && !json_is_null(v)) ? deserialize_redirects(v) : NULL;

    cmd = calloc(1, sizeof(COMMAND));
    if (!cmd) {
        free(s);
        return NULL;
    }
    cmd->type = cm_simple;
    cmd->flags = cmd_flags;
    cmd->line = cmd_line;
    cmd->redirects = cmd_redirects;
    cmd->value.Simple = s;

    return cmd;
}

/* Deserialize a connection */
static COMMAND *
deserialize_connection(const char *json, int cmd_flags, int cmd_line, REDIRECT *cmd_redirects)
{
    COMMAND *cmd;
    CONNECTION *c;
    const char *v;
    char *conn_str;

    c = calloc(1, sizeof(CONNECTION));
    if (!c)
        return NULL;

    v = json_find_key(json, "connector");
    conn_str = v ? json_extract_string(v) : NULL;
    c->connector = conn_str ? connector_from_name(conn_str) : ';';
    free(conn_str);

    v = json_find_key(json, "first");
    c->first = v ? cmd_deserialize_recursive(v) : NULL;

    v = json_find_key(json, "second");
    c->second = v ? cmd_deserialize_recursive(v) : NULL;

    cmd = calloc(1, sizeof(COMMAND));
    if (!cmd) {
        free(c);
        return NULL;
    }
    cmd->type = cm_connection;
    cmd->flags = cmd_flags;
    cmd->line = cmd_line;
    cmd->redirects = cmd_redirects;
    cmd->value.Connection = c;

    return cmd;
}

/* Deserialize a for command */
static COMMAND *
deserialize_for(const char *json, int cmd_flags, int cmd_line, REDIRECT *cmd_redirects)
{
    COMMAND *cmd;
    FOR_COM *f;
    const char *v;
    char *name_str;

    f = calloc(1, sizeof(FOR_COM));
    if (!f)
        return NULL;

    v = json_find_key(json, "flags");
    f->flags = v ? json_extract_int(v) : 0;

    v = json_find_key(json, "line");
    f->line = v ? json_extract_int(v) : cmd_line;

    v = json_find_key(json, "name");
    name_str = v ? json_extract_string(v) : NULL;
    f->name = alloc_word(name_str ? name_str : "i", 0);
    free(name_str);

    v = json_find_key(json, "map_list");
    f->map_list = v ? deserialize_word_list(v) : NULL;

    v = json_find_key(json, "action");
    f->action = v ? cmd_deserialize_recursive(v) : NULL;

    cmd = calloc(1, sizeof(COMMAND));
    if (!cmd) {
        free(f);
        return NULL;
    }
    cmd->type = cm_for;
    cmd->flags = cmd_flags;
    cmd->line = cmd_line;
    cmd->redirects = cmd_redirects;
    cmd->value.For = f;

    return cmd;
}

/* Deserialize an if command */
static COMMAND *
deserialize_if(const char *json, int cmd_flags, int cmd_line, REDIRECT *cmd_redirects)
{
    COMMAND *cmd;
    IF_COM *i;
    const char *v;

    i = calloc(1, sizeof(IF_COM));
    if (!i)
        return NULL;

    v = json_find_key(json, "flags");
    i->flags = v ? json_extract_int(v) : 0;

    v = json_find_key(json, "test");
    i->test = v ? cmd_deserialize_recursive(v) : NULL;

    v = json_find_key(json, "true_case");
    i->true_case = v ? cmd_deserialize_recursive(v) : NULL;

    v = json_find_key(json, "false_case");
    i->false_case = v ? cmd_deserialize_recursive(v) : NULL;

    cmd = calloc(1, sizeof(COMMAND));
    if (!cmd) {
        free(i);
        return NULL;
    }
    cmd->type = cm_if;
    cmd->flags = cmd_flags;
    cmd->line = cmd_line;
    cmd->redirects = cmd_redirects;
    cmd->value.If = i;

    return cmd;
}

/* Deserialize a while/until command */
static COMMAND *
deserialize_while(const char *json, int cmd_flags, int cmd_line,
                  REDIRECT *cmd_redirects, enum command_type type)
{
    COMMAND *cmd;
    WHILE_COM *w;
    const char *v;

    w = calloc(1, sizeof(WHILE_COM));
    if (!w)
        return NULL;

    v = json_find_key(json, "flags");
    w->flags = v ? json_extract_int(v) : 0;

    v = json_find_key(json, "test");
    w->test = v ? cmd_deserialize_recursive(v) : NULL;

    v = json_find_key(json, "action");
    w->action = v ? cmd_deserialize_recursive(v) : NULL;

    cmd = calloc(1, sizeof(COMMAND));
    if (!cmd) {
        free(w);
        return NULL;
    }
    cmd->type = type;
    cmd->flags = cmd_flags;
    cmd->line = cmd_line;
    cmd->redirects = cmd_redirects;
    cmd->value.While = w;

    return cmd;
}

/* Deserialize a case command */
static COMMAND *
deserialize_case(const char *json, int cmd_flags, int cmd_line, REDIRECT *cmd_redirects)
{
    COMMAND *cmd;
    CASE_COM *c;
    const char *v, *p;
    char *word_str;

    c = calloc(1, sizeof(CASE_COM));
    if (!c)
        return NULL;

    v = json_find_key(json, "flags");
    c->flags = v ? json_extract_int(v) : 0;

    v = json_find_key(json, "line");
    c->line = v ? json_extract_int(v) : cmd_line;

    v = json_find_key(json, "word");
    word_str = v ? json_extract_string(v) : NULL;
    c->word = alloc_word(word_str ? word_str : "", 0);
    free(word_str);

    /* Parse clauses array */
    v = json_find_key(json, "clauses");
    c->clauses = NULL;
    if (v && *v == '[') {
        PATTERN_LIST *ptail = NULL;
        p = v + 1;
        while (*p) {
            PATTERN_LIST *pl;
            const char *pv;

            while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t')
                p++;
            if (*p == ']')
                break;
            if (*p != '{')
                break;

            pl = calloc(1, sizeof(PATTERN_LIST));
            if (!pl)
                break;

            pv = json_find_key(p, "patterns");
            pl->patterns = pv ? deserialize_word_list(pv) : NULL;

            pv = json_find_key(p, "action");
            pl->action = pv ? cmd_deserialize_recursive(pv) : NULL;

            pv = json_find_key(p, "flags");
            pl->flags = pv ? json_extract_int(pv) : 0;

            pl->next = NULL;
            if (ptail)
                ptail->next = pl;
            else
                c->clauses = pl;
            ptail = pl;

            p = json_skip_value(p);
        }
    }

    cmd = calloc(1, sizeof(COMMAND));
    if (!cmd) {
        free(c);
        return NULL;
    }
    cmd->type = cm_case;
    cmd->flags = cmd_flags;
    cmd->line = cmd_line;
    cmd->redirects = cmd_redirects;
    cmd->value.Case = c;

    return cmd;
}

/* Deserialize a group command */
static COMMAND *
deserialize_group(const char *json, int cmd_flags, int cmd_line, REDIRECT *cmd_redirects)
{
    COMMAND *cmd;
    GROUP_COM *g;
    const char *v;

    g = calloc(1, sizeof(GROUP_COM));
    if (!g)
        return NULL;

    v = json_find_key(json, "command");
    g->command = v ? cmd_deserialize_recursive(v) : NULL;

    cmd = calloc(1, sizeof(COMMAND));
    if (!cmd) {
        free(g);
        return NULL;
    }
    cmd->type = cm_group;
    cmd->flags = cmd_flags;
    cmd->line = cmd_line;
    cmd->redirects = cmd_redirects;
    cmd->value.Group = g;

    return cmd;
}

/* Deserialize a subshell command */
static COMMAND *
deserialize_subshell(const char *json, int cmd_flags, int cmd_line, REDIRECT *cmd_redirects)
{
    COMMAND *cmd;
    SUBSHELL_COM *s;
    const char *v;

    s = calloc(1, sizeof(SUBSHELL_COM));
    if (!s)
        return NULL;

    v = json_find_key(json, "flags");
    s->flags = v ? json_extract_int(v) : 0;

    v = json_find_key(json, "line");
    s->line = v ? json_extract_int(v) : cmd_line;

    v = json_find_key(json, "command");
    s->command = v ? cmd_deserialize_recursive(v) : NULL;

    cmd = calloc(1, sizeof(COMMAND));
    if (!cmd) {
        free(s);
        return NULL;
    }
    cmd->type = cm_subshell;
    cmd->flags = cmd_flags;
    cmd->line = cmd_line;
    cmd->redirects = cmd_redirects;
    cmd->value.Subshell = s;

    return cmd;
}

/* Deserialize a function_def */
static COMMAND *
deserialize_function_def(const char *json, int cmd_flags, int cmd_line, REDIRECT *cmd_redirects)
{
    COMMAND *cmd;
    FUNCTION_DEF *f;
    const char *v;
    char *name_str, *src_str;

    f = calloc(1, sizeof(FUNCTION_DEF));
    if (!f)
        return NULL;

    v = json_find_key(json, "flags");
    f->flags = v ? json_extract_int(v) : 0;

    v = json_find_key(json, "line");
    f->line = v ? json_extract_int(v) : cmd_line;

    v = json_find_key(json, "name");
    name_str = v ? json_extract_string(v) : NULL;
    f->name = alloc_word(name_str ? name_str : "", 0);
    free(name_str);

    v = json_find_key(json, "source_file");
    src_str = v ? json_extract_string(v) : NULL;
    f->source_file = src_str;  /* may be NULL */

    v = json_find_key(json, "command");
    f->command = v ? cmd_deserialize_recursive(v) : NULL;

    cmd = calloc(1, sizeof(COMMAND));
    if (!cmd) {
        free(f);
        return NULL;
    }
    cmd->type = cm_function_def;
    cmd->flags = cmd_flags;
    cmd->line = cmd_line;
    cmd->redirects = cmd_redirects;
    cmd->value.Function_def = f;

    return cmd;
}

/* Recursive COMMAND deserialization */
static COMMAND *
cmd_deserialize_recursive(const char *json)
{
    const char *v;
    char *type_str;
    int cmd_type;
    int cmd_flags, cmd_line;
    REDIRECT *cmd_redirects;

    if (!json || json_is_null(json))
        return NULL;

    if (*json != '{')
        return NULL;

    /* Get command type */
    v = json_find_key(json, "type");
    type_str = v ? json_extract_string(v) : NULL;
    if (!type_str)
        return NULL;

    cmd_type = cmd_type_from_name(type_str);
    free(type_str);
    if (cmd_type < 0)
        return NULL;

    /* Get common fields */
    v = json_find_key(json, "flags");
    cmd_flags = v ? json_extract_int(v) : 0;

    v = json_find_key(json, "line");
    cmd_line = v ? json_extract_int(v) : 0;

    v = json_find_key(json, "redirects");
    cmd_redirects = (v && !json_is_null(v)) ? deserialize_redirects(v) : NULL;

    /* Dispatch to type-specific deserializer using sub-object */
    switch (cmd_type) {
    case cm_simple: {
        v = json_find_key(json, "simple");
        return v ? deserialize_simple(v, cmd_flags, cmd_line, cmd_redirects) : NULL;
    }
    case cm_connection: {
        v = json_find_key(json, "connection");
        return v ? deserialize_connection(v, cmd_flags, cmd_line, cmd_redirects) : NULL;
    }
    case cm_for: {
        v = json_find_key(json, "for");
        return v ? deserialize_for(v, cmd_flags, cmd_line, cmd_redirects) : NULL;
    }
    case cm_if: {
        v = json_find_key(json, "if");
        return v ? deserialize_if(v, cmd_flags, cmd_line, cmd_redirects) : NULL;
    }
    case cm_while: {
        v = json_find_key(json, "while");
        return v ? deserialize_while(v, cmd_flags, cmd_line, cmd_redirects, cm_while) : NULL;
    }
    case cm_until: {
        v = json_find_key(json, "while");
        return v ? deserialize_while(v, cmd_flags, cmd_line, cmd_redirects, cm_until) : NULL;
    }
    case cm_case: {
        v = json_find_key(json, "case");
        return v ? deserialize_case(v, cmd_flags, cmd_line, cmd_redirects) : NULL;
    }
    case cm_group: {
        v = json_find_key(json, "group");
        return v ? deserialize_group(v, cmd_flags, cmd_line, cmd_redirects) : NULL;
    }
    case cm_subshell: {
        v = json_find_key(json, "subshell");
        return v ? deserialize_subshell(v, cmd_flags, cmd_line, cmd_redirects) : NULL;
    }
    case cm_function_def: {
        v = json_find_key(json, "function_def");
        return v ? deserialize_function_def(v, cmd_flags, cmd_line, cmd_redirects) : NULL;
    }
    default:
        return NULL;
    }
}

/* Deserialize a JSON string into a COMMAND tree.
 * Returns a malloc'd COMMAND struct that the caller must free with cmd_free().
 * Returns NULL on error. */
COMMAND *
cmd_deserialize(const char *json)
{
    if (!json)
        return NULL;
    return cmd_deserialize_recursive(json);
}

/* ================================================================
 * Free deserialized COMMAND trees
 * ================================================================ */

static void
cmd_free_redirects(REDIRECT *r)
{
    REDIRECT *next;
    while (r) {
        next = r->next;
        if (r->redirectee.filename)
            cmd_free_word(r->redirectee.filename);
        free(r->here_doc_eof);
        free(r);
        r = next;
    }
}

static void
cmd_free_pattern_list(PATTERN_LIST *p)
{
    PATTERN_LIST *next;
    while (p) {
        next = p->next;
        cmd_free_word_list(p->patterns);
        cmd_free_recursive(p->action);
        free(p);
        p = next;
    }
}

static void
cmd_free_recursive(COMMAND *cmd)
{
    if (!cmd)
        return;

    cmd_free_redirects(cmd->redirects);

    switch (cmd->type) {
    case cm_simple:
        if (cmd->value.Simple) {
            cmd_free_word_list(cmd->value.Simple->words);
            cmd_free_redirects(cmd->value.Simple->redirects);
            free(cmd->value.Simple);
        }
        break;
    case cm_connection:
        if (cmd->value.Connection) {
            cmd_free_recursive(cmd->value.Connection->first);
            cmd_free_recursive(cmd->value.Connection->second);
            free(cmd->value.Connection);
        }
        break;
    case cm_for:
        if (cmd->value.For) {
            cmd_free_word(cmd->value.For->name);
            cmd_free_word_list(cmd->value.For->map_list);
            cmd_free_recursive(cmd->value.For->action);
            free(cmd->value.For);
        }
        break;
    case cm_if:
        if (cmd->value.If) {
            cmd_free_recursive(cmd->value.If->test);
            cmd_free_recursive(cmd->value.If->true_case);
            cmd_free_recursive(cmd->value.If->false_case);
            free(cmd->value.If);
        }
        break;
    case cm_while:
    case cm_until:
        if (cmd->value.While) {
            cmd_free_recursive(cmd->value.While->test);
            cmd_free_recursive(cmd->value.While->action);
            free(cmd->value.While);
        }
        break;
    case cm_case:
        if (cmd->value.Case) {
            cmd_free_word(cmd->value.Case->word);
            cmd_free_pattern_list(cmd->value.Case->clauses);
            free(cmd->value.Case);
        }
        break;
    case cm_group:
        if (cmd->value.Group) {
            cmd_free_recursive(cmd->value.Group->command);
            free(cmd->value.Group);
        }
        break;
    case cm_subshell:
        if (cmd->value.Subshell) {
            cmd_free_recursive(cmd->value.Subshell->command);
            free(cmd->value.Subshell);
        }
        break;
    case cm_function_def:
        if (cmd->value.Function_def) {
            cmd_free_word(cmd->value.Function_def->name);
            free(cmd->value.Function_def->source_file);
            cmd_free_recursive(cmd->value.Function_def->command);
            free(cmd->value.Function_def);
        }
        break;
    default:
        break;
    }

    free(cmd);
}

/* Free a COMMAND tree produced by cmd_deserialize().
 * Safe to call with NULL. */
void
cmd_free(COMMAND *cmd)
{
    cmd_free_recursive(cmd);
}
