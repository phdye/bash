/*
 * protocol.c - NDJSON protocol, base64, minimal JSON helpers
 */

#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
#include <ctype.h>

/* --- Base64 --- */

static const char b64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *bc_b64_encode(const char *data, size_t len)
{
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = malloc(out_len + 1);
    if (!out) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < len; i += 3, j += 4) {
        unsigned int n = ((unsigned char)data[i]) << 16;
        if (i + 1 < len) n |= ((unsigned char)data[i + 1]) << 8;
        if (i + 2 < len) n |= ((unsigned char)data[i + 2]);

        out[j]     = b64_table[(n >> 18) & 0x3F];
        out[j + 1] = b64_table[(n >> 12) & 0x3F];
        out[j + 2] = (i + 1 < len) ? b64_table[(n >> 6) & 0x3F] : '=';
        out[j + 3] = (i + 2 < len) ? b64_table[n & 0x3F] : '=';
    }
    out[j] = '\0';
    return out;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

char *bc_b64_decode(const char *encoded, size_t *out_len)
{
    size_t elen = strlen(encoded);
    size_t dlen = (elen / 4) * 3;
    if (elen > 0 && encoded[elen - 1] == '=') dlen--;
    if (elen > 1 && encoded[elen - 2] == '=') dlen--;

    char *out = malloc(dlen + 1);
    if (!out) return NULL;

    size_t i, j;
    for (i = 0, j = 0; i < elen; i += 4) {
        int a = b64_val(encoded[i]);
        int b = (i + 1 < elen) ? b64_val(encoded[i + 1]) : 0;
        int c = (i + 2 < elen) ? b64_val(encoded[i + 2]) : 0;
        int d = (i + 3 < elen) ? b64_val(encoded[i + 3]) : 0;

        /* Padding '=' returns -1; treat as 0 for the bitwise math */
        if (a < 0) a = 0;
        if (b < 0) b = 0;
        if (c < 0) c = 0;
        if (d < 0) d = 0;

        unsigned int n = (a << 18) | (b << 12) | (c << 6) | d;
        if (j < dlen) out[j++] = (n >> 16) & 0xFF;
        if (j < dlen) out[j++] = (n >> 8) & 0xFF;
        if (j < dlen) out[j++] = n & 0xFF;
    }
    out[j] = '\0';
    if (out_len) *out_len = dlen;
    return out;
}

/* --- Minimal JSON helpers --- */

/* Find key in JSON object, return pointer to value start.
   Handles simple cases (no nested quotes). */
static const char *json_find_key(const char *json, const char *key)
{
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    const char *p = json;
    while ((p = strstr(p, search)) != NULL) {
        p += strlen(search);
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == ':') {
            p++;
            while (*p && isspace((unsigned char)*p)) p++;
            return p;
        }
    }
    return NULL;
}

char *bc_json_get_string(const char *json, const char *key)
{
    const char *p = json_find_key(json, key);
    if (!p || *p != '"') return NULL;
    p++;
    const char *end = p;
    while (*end && *end != '"') {
        if (*end == '\\') end++;
        if (*end) end++;
    }
    size_t len = end - p;
    char *s = malloc(len + 1);
    if (!s) return NULL;

    /* Simple unescape */
    size_t i, j;
    for (i = 0, j = 0; i < len; i++) {
        if (p[i] == '\\' && i + 1 < len) {
            i++;
            switch (p[i]) {
            case 'n': s[j++] = '\n'; break;
            case 't': s[j++] = '\t'; break;
            case 'r': s[j++] = '\r'; break;
            case '\\': s[j++] = '\\'; break;
            case '"': s[j++] = '"'; break;
            case '/': s[j++] = '/'; break;
            default: s[j++] = p[i]; break;
            }
        } else {
            s[j++] = p[i];
        }
    }
    s[j] = '\0';
    return s;
}

int bc_json_get_int(const char *json, const char *key, int def)
{
    const char *p = json_find_key(json, key);
    if (!p) return def;
    if (*p == '-' || isdigit((unsigned char)*p))
        return (int)strtol(p, NULL, 10);
    if (strncmp(p, "true", 4) == 0) return 1;
    if (strncmp(p, "false", 5) == 0) return 0;
    return def;
}

long long bc_json_get_llong(const char *json, const char *key, long long def)
{
    const char *p = json_find_key(json, key);
    if (!p) return def;
    if (*p == '-' || isdigit((unsigned char)*p))
        return strtoll(p, NULL, 10);
    return def;
}

char *bc_json_get_object(const char *json, const char *key)
{
    const char *p = json_find_key(json, key);
    if (!p || *p != '{') return NULL;
    int depth = 1;
    const char *start = p;
    p++;
    while (*p && depth > 0) {
        if (*p == '{') depth++;
        else if (*p == '}') depth--;
        else if (*p == '"') {
            p++;
            while (*p && *p != '"') { if (*p == '\\') p++; p++; }
        }
        if (*p) p++;
    }
    size_t len = p - start;
    char *s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

char *bc_json_get_array(const char *json, const char *key)
{
    const char *p = json_find_key(json, key);
    if (!p || *p != '[') return NULL;
    int depth = 1;
    const char *start = p;
    p++;
    while (*p && depth > 0) {
        if (*p == '[') depth++;
        else if (*p == ']') depth--;
        else if (*p == '"') {
            p++;
            while (*p && *p != '"') { if (*p == '\\') p++; p++; }
        }
        if (*p) p++;
    }
    size_t len = p - start;
    char *s = malloc(len + 1);
    if (!s) return NULL;
    memcpy(s, start, len);
    s[len] = '\0';
    return s;
}

/* --- Protocol send/recv --- */

void bc_set_error(bc_client_t *c, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(c->errbuf, BC_ERRBUF_SIZE, fmt, ap);
    va_end(ap);
}

int bc_send_msg(bc_client_t *c, const char *json)
{
    size_t len = strlen(json);
    if (bc_transport_write(&c->transport, json, len) < 0) {
        bc_set_error(c, "write error");
        return BC_ERR_TRANSPORT;
    }
    if (json[len - 1] != '\n') {
        if (bc_transport_write(&c->transport, "\n", 1) < 0) {
            bc_set_error(c, "write error");
            return BC_ERR_TRANSPORT;
        }
    }
    return BC_OK;
}

int bc_recv_msg(bc_client_t *c, char **json_out)
{
    if (!c->linebuf) {
        c->linebuf = malloc(BC_LINEBUF_SIZE);
        if (!c->linebuf) {
            bc_set_error(c, "out of memory");
            return BC_ERR_NOMEM;
        }
        c->linebuf_size = BC_LINEBUF_SIZE;
    }

    int n = bc_transport_readline(&c->transport, c->linebuf, c->linebuf_size);
    if (n <= 0) {
        bc_set_error(c, "read error or connection closed");
        return BC_ERR_TRANSPORT;
    }

    /* Strip trailing newline */
    if (n > 0 && c->linebuf[n - 1] == '\n') c->linebuf[--n] = '\0';
    if (n > 0 && c->linebuf[n - 1] == '\r') c->linebuf[--n] = '\0';

    if (json_out) {
        *json_out = strdup(c->linebuf);
        if (!*json_out) {
            bc_set_error(c, "out of memory");
            return BC_ERR_NOMEM;
        }
    }
    return BC_OK;
}

int bc_recv_msg_channel(bc_client_t *c, int channel, char **json_out)
{
    /* Read messages, dispatching server-push until we get one for our channel. */
    for (int attempts = 0; attempts < 100; attempts++) {
        char *json = NULL;
        int rc = bc_recv_msg(c, &json);
        if (rc != BC_OK) return rc;

        int ch = bc_json_get_int(json, "ch", 0);
        if (ch == channel) {
            if (json_out) *json_out = json;
            else free(json);
            return BC_OK;
        }

        /* Dispatch server-push */
        char *type = bc_json_get_string(json, "type");
        if (!type) { free(json); continue; }

        if (ch == BC_CHAN_OBSERVE && strcmp(type, "pre_command") == 0) {
            if (c->observe_pre_cb) {
                char *data = bc_json_get_object(json, "data");
                if (data) {
                    bc_pre_command_event_t ev = {0};
                    ev.seq = bc_json_get_int(json, "seq", 0);
                    ev.timestamp = bc_json_get_llong(json, "timestamp", 0);
                    ev.command = bc_json_get_string(data, "command");
                    ev.cwd = bc_json_get_string(data, "cwd");
                    ev.line_number = bc_json_get_int(data, "line_number", 0);
                    ev.is_subshell = bc_json_get_int(data, "is_subshell", 0);
                    ev.is_async = bc_json_get_int(data, "is_async", 0);
                    c->observe_pre_cb(&ev, c->observe_pre_ud);
                    free(ev.command); free(ev.cwd); free(data);
                }
            }
        } else if (ch == BC_CHAN_OBSERVE && strcmp(type, "post_command") == 0) {
            if (c->observe_post_cb) {
                char *data = bc_json_get_object(json, "data");
                if (data) {
                    bc_post_command_event_t ev = {0};
                    ev.seq = bc_json_get_int(json, "seq", 0);
                    ev.timestamp = bc_json_get_llong(json, "timestamp", 0);
                    ev.command = bc_json_get_string(data, "command");
                    ev.exit_status = bc_json_get_int(data, "exit_status", 0);
                    ev.signal_number = bc_json_get_int(data, "signal_number", 0);
                    ev.duration_ms = bc_json_get_int(data, "duration_ms", 0);
                    c->observe_post_cb(&ev, c->observe_post_ud);
                    free(ev.command); free(data);
                }
            }
        } else if (ch == BC_CHAN_DEBUG && strcmp(type, "break_hit") == 0) {
            if (c->debug_break_cb) {
                bc_break_hit_event_t ev = {0};
                ev.line = bc_json_get_int(json, "line", 0);
                ev.command = bc_json_get_string(json, "command");
                ev.depth = bc_json_get_int(json, "depth", 0);
                c->debug_break_cb(&ev, c->debug_break_ud);
                free(ev.command);
            }
        } else if (ch == BC_CHAN_PTY && strcmp(type, "output") == 0) {
            if (c->pty_output_cb) {
                char *enc = bc_json_get_string(json, "encoding");
                char *raw = bc_json_get_string(json, "data");
                if (raw) {
                    if (enc && strcmp(enc, "base64") == 0) {
                        size_t dlen;
                        char *decoded = bc_b64_decode(raw, &dlen);
                        if (decoded) {
                            c->pty_output_cb(decoded, dlen, c->pty_output_ud);
                            free(decoded);
                        }
                    } else {
                        c->pty_output_cb(raw, strlen(raw), c->pty_output_ud);
                    }
                }
                free(enc); free(raw);
            }
        } else if (ch == BC_CHAN_PTY && strcmp(type, "exit") == 0) {
            if (c->pty_exit_cb) {
                int code = bc_json_get_int(json, "exit_code", -1);
                c->pty_exit_cb(code, c->pty_exit_ud);
            }
        }

        free(type);
        free(json);
    }

    bc_set_error(c, "too many messages without matching channel %d", channel);
    return BC_ERR_PROTOCOL;
}
