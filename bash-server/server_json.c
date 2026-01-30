/* server_json.c -- JSON protocol v2 for bash-server */

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

/* Protocol v2: Length-prefixed JSON frames with channel multiplexing.
 *
 * Frame format (6 bytes header + N bytes payload):
 *   channel: uint8   (1 byte)  - Logical channel (0-5)
 *   flags:   uint8   (1 byte)  - COMPRESSED, BINARY, CONTINUED, FINAL
 *   length:  uint32  (4 bytes) - Payload length (network byte order)
 *   payload: bytes   (N bytes) - JSON string
 *
 * Version detection: v1 messages start with printable ASCII (>= 0x20),
 * v2 frames start with channel byte (0-5, all < 0x20).
 *
 * Current implementation (v2): All channels supported in framing,
 * channels 0 (CONTROL), 1 (COMMAND), 2 (STATE) handled.
 * Channels 3-5 return errors (reserved for future phases).
 */

#include "server.h"
#include "../shell.h"
#include "../builtins/common.h"
#include <stdarg.h>
#include <ctype.h>
#include <arpa/inet.h>  /* ntohl, htonl */
#include <sys/uio.h>    /* writev, struct iovec */

/* External declarations from bash */
extern int last_command_exit_value;

/* ================================================================
 * Wire format control — process-global, safe because fork-per-session
 * ================================================================ */

static int g_wire_format = WIRE_BINARY;

void
json_set_wire_format(int format)
{
    g_wire_format = format;
}

int
json_get_wire_format(void)
{
    return g_wire_format;
}

/* ================================================================
 * JSON Helpers — minimal hand-rolled parser/serializer
 *
 * These handle the limited vocabulary of bash-server messages.
 * Not a general-purpose JSON library.
 * ================================================================ */

/* Escape a string for JSON output.  Writes to buf, returns bytes written
   (not counting NUL).  Returns -1 if buf too small. */
static int
json_escape(const char *src, char *buf, size_t bufsize)
{
    char *p = buf;
    char *end = buf + bufsize - 1;

    while (*src && p < end) {
        switch (*src) {
        case '"':  if (p + 2 > end) return -1; *p++ = '\\'; *p++ = '"';  break;
        case '\\': if (p + 2 > end) return -1; *p++ = '\\'; *p++ = '\\'; break;
        case '\n': if (p + 2 > end) return -1; *p++ = '\\'; *p++ = 'n';  break;
        case '\r': if (p + 2 > end) return -1; *p++ = '\\'; *p++ = 'r';  break;
        case '\t': if (p + 2 > end) return -1; *p++ = '\\'; *p++ = 't';  break;
        default:
            if ((unsigned char)*src < 0x20) {
                /* Control character — use \u00XX */
                if (p + 6 > end) return -1;
                p += snprintf(p, 7, "\\u%04x", (unsigned char)*src);
            } else {
                *p++ = *src;
            }
            break;
        }
        src++;
    }
    *p = '\0';
    return (int)(p - buf);
}

/* Unescape a JSON string in-place.  src points past the opening quote.
   Writes unescaped bytes into buf, stops at closing quote or end.
   Returns length of unescaped string, or -1 on error. */
static int
json_unescape(const char *src, char *buf, size_t bufsize)
{
    char *p = buf;
    char *end = buf + bufsize - 1;

    while (*src && *src != '"' && p < end) {
        if (*src == '\\') {
            src++;
            switch (*src) {
            case '"':  *p++ = '"';  break;
            case '\\': *p++ = '\\'; break;
            case '/':  *p++ = '/';  break;
            case 'n':  *p++ = '\n'; break;
            case 'r':  *p++ = '\r'; break;
            case 't':  *p++ = '\t'; break;
            case 'b':  *p++ = '\b'; break;
            case 'f':  *p++ = '\f'; break;
            case 'u': {
                /* \uXXXX — only handle ASCII range */
                unsigned int code = 0;
                int i;
                src++;
                for (i = 0; i < 4 && *src; i++, src++) {
                    code <<= 4;
                    if (*src >= '0' && *src <= '9') code |= *src - '0';
                    else if (*src >= 'a' && *src <= 'f') code |= *src - 'a' + 10;
                    else if (*src >= 'A' && *src <= 'F') code |= *src - 'A' + 10;
                    else return -1;
                }
                if (code < 0x80) {
                    *p++ = (char)code;
                }
                /* Skip non-ASCII unicode for now */
                continue;  /* src already advanced */
            }
            default: *p++ = *src; break;
            }
            src++;
        } else {
            *p++ = *src++;
        }
    }
    *p = '\0';
    return (int)(p - buf);
}

/* Extract a string value for a given key from a JSON object.
   Returns pointer to buf on success, NULL if key not found.
   The JSON must be a flat object (no nested objects searched). */
const char *
json_get_string(const char *json, const char *key, char *buf, size_t bufsize)
{
    char search[256];
    const char *p;
    int keylen;

    keylen = snprintf(search, sizeof(search), "\"%s\"", key);
    if (keylen < 0 || keylen >= (int)sizeof(search))
        return NULL;

    p = strstr(json, search);
    if (!p)
        return NULL;

    /* Skip past key and colon */
    p += keylen;
    while (*p && (*p == ' ' || *p == '\t' || *p == ':'))
        p++;

    if (*p != '"')
        return NULL;

    p++;  /* Skip opening quote */
    if (json_unescape(p, buf, bufsize) < 0)
        return NULL;

    return buf;
}

/* Extract an integer value for a given key from a JSON object.
   Returns 0 on success, -1 if key not found or not a number. */
int
json_get_int(const char *json, const char *key, int *value)
{
    char search[256];
    const char *p;
    int keylen;

    keylen = snprintf(search, sizeof(search), "\"%s\"", key);
    if (keylen < 0 || keylen >= (int)sizeof(search))
        return -1;

    p = strstr(json, search);
    if (!p)
        return -1;

    p += keylen;
    while (*p && (*p == ' ' || *p == '\t' || *p == ':'))
        p++;

    if (!*p || (*p != '-' && !isdigit((unsigned char)*p)))
        return -1;

    *value = atoi(p);
    return 0;
}

/* Check if a JSON object has a boolean true value for a key.
   Returns 1 for true, 0 for false or not found. */
static int
json_get_bool(const char *json, const char *key)
{
    char search[256];
    const char *p;
    int keylen;

    keylen = snprintf(search, sizeof(search), "\"%s\"", key);
    if (keylen < 0 || keylen >= (int)sizeof(search))
        return 0;

    p = strstr(json, search);
    if (!p)
        return 0;

    p += keylen;
    while (*p && (*p == ' ' || *p == '\t' || *p == ':'))
        p++;

    return (strncmp(p, "true", 4) == 0);
}

/* Extract a JSON string array value for a key.
   Returns items found, fills arr[] with pointers into buf (caller provides storage).
   Very simple: handles ["str1","str2",...] only. */
static int
json_get_string_array(const char *json, const char *key,
                      char *buf, size_t bufsize, char **arr, int max_arr)
{
    char search[256];
    const char *p;
    int keylen, count = 0;
    char *bp = buf;
    char *bend = buf + bufsize;

    keylen = snprintf(search, sizeof(search), "\"%s\"", key);
    if (keylen < 0 || keylen >= (int)sizeof(search))
        return 0;

    p = strstr(json, search);
    if (!p)
        return 0;

    p += keylen;
    while (*p && (*p == ' ' || *p == '\t' || *p == ':'))
        p++;

    if (*p != '[')
        return 0;
    p++;

    while (*p && *p != ']' && count < max_arr) {
        while (*p && (*p == ' ' || *p == ',' || *p == '\t'))
            p++;
        if (*p == '"') {
            p++;
            arr[count] = bp;
            int len = json_unescape(p, bp, bend - bp);
            if (len < 0) break;
            bp += len + 1;
            count++;
            /* Skip past closing quote */
            while (*p && *p != '"') {
                if (*p == '\\') p++;
                p++;
            }
            if (*p == '"') p++;
        } else {
            break;
        }
    }

    return count;
}


/* ================================================================
 * Frame I/O
 * ================================================================ */

/* Read exactly n bytes from fd.  Returns 0 on success, -1 on error/EOF. */
static int
read_exact(int fd, void *buf, size_t n)
{
    size_t total = 0;
    ssize_t r;

    while (total < n) {
        r = read(fd, (char *)buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0)
            return -1;  /* EOF */
        total += r;
    }
    return 0;
}

/* Write exactly n bytes to fd.  Returns 0 on success, -1 on error. */
static int
write_exact(int fd, const void *buf, size_t n)
{
    size_t total = 0;
    ssize_t w;

    while (total < n) {
        w = write(fd, (const char *)buf + total, n - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        total += w;
    }
    return 0;
}

/* ================================================================
 * NDJSON Frame I/O
 *
 * Each message is one JSON line terminated by \n.
 * The "ch" field carries the channel ID that binary framing puts in
 * the header byte.  Format: {"ch":N, ...rest of JSON...}\n
 * ================================================================ */

/* Write an NDJSON frame: inject "ch":N into payload and append \n.
   Payload must start with '{'.  Uses writev() for zero-copy.
   Returns 0 on success, -1 on error. */
static int
ndjson_frame_write(int fd, int channel, const char *payload, size_t payload_len)
{
    char prefix[32];
    int prefix_len;
    struct iovec iov[3];
    ssize_t total, written;

    if (payload_len == 0 || payload[0] != '{') {
        /* Non-JSON payload — wrap it: {"ch":N,"data":"..."}\n */
        char wrapped[FRAME_MAX_PAYLOAD + 64];
        int wlen = snprintf(wrapped, sizeof(wrapped),
            "{\"ch\":%d,\"data\":\"%.*s\"}\n",
            channel, (int)payload_len, payload ? payload : "");
        if (wlen < 0 || wlen >= (int)sizeof(wrapped))
            return -1;
        return write_exact(fd, wrapped, wlen);
    }

    /* Normal case: payload is JSON object starting with '{'.
       Produce: {"ch":N, + payload[1:] + \n */
    prefix_len = snprintf(prefix, sizeof(prefix), "{\"ch\":%d,", channel);
    if (prefix_len < 0)
        return -1;

    iov[0].iov_base = prefix;
    iov[0].iov_len = prefix_len;
    iov[1].iov_base = (char *)payload + 1;  /* skip '{' */
    iov[1].iov_len = payload_len - 1;
    iov[2].iov_base = "\n";
    iov[2].iov_len = 1;

    total = prefix_len + (payload_len - 1) + 1;
    written = 0;

    while (written < total) {
        ssize_t w = writev(fd, iov, 3);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        written += w;
        /* Advance iov entries past what was written */
        {
            ssize_t skip = w;
            int i;
            for (i = 0; i < 3 && skip > 0; i++) {
                if ((size_t)skip >= iov[i].iov_len) {
                    skip -= iov[i].iov_len;
                    iov[i].iov_len = 0;
                } else {
                    iov[i].iov_base = (char *)iov[i].iov_base + skip;
                    iov[i].iov_len -= skip;
                    skip = 0;
                }
            }
        }
    }

    return 0;
}

/* Read an NDJSON frame: read bytes until \n, extract "ch" field.
   On success: *payload is malloc'd (full line including "ch"), returns 0.
   On error/EOF: returns -1. */
static int
ndjson_frame_read(int fd, int *channel, int *flags, char **payload, size_t *payload_len)
{
    char *buf = NULL;
    size_t cap = 0, len = 0;
    char byte;
    ssize_t n;
    int ch_val;

    *flags = 0;  /* NDJSON has no flags */

    /* Read one byte at a time until \n (simple, correct for line protocol) */
    cap = 1024;
    buf = malloc(cap);
    if (!buf)
        return -1;

    while (1) {
        n = read(fd, &byte, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            free(buf);
            return -1;
        }
        if (n == 0) {
            /* EOF */
            free(buf);
            return -1;
        }
        if (byte == '\n')
            break;

        if (len + 1 >= cap) {
            cap *= 2;
            if (cap > FRAME_MAX_PAYLOAD + 64) {
                free(buf);
                return -1;
            }
            char *newbuf = realloc(buf, cap);
            if (!newbuf) {
                free(buf);
                return -1;
            }
            buf = newbuf;
        }
        buf[len++] = byte;
    }

    buf[len] = '\0';

    /* Extract "ch" field */
    if (json_get_int(buf, "ch", &ch_val) < 0) {
        /* No channel field — default to CHAN_CONTROL */
        ch_val = CHAN_CONTROL;
    }

    if (ch_val < 0 || ch_val > CHAN_MAX) {
        free(buf);
        *payload = NULL;
        *payload_len = 0;
        return -1;
    }

    *channel = ch_val;
    *payload = buf;
    *payload_len = len;
    return 0;
}


/* Read a v2 frame from fd (binary or NDJSON depending on wire format).
   On success: *payload is malloc'd (caller frees), returns 0.
   On error/EOF: returns -1. */
int
json_frame_read(int fd, int *channel, int *flags, char **payload, size_t *payload_len)
{
    unsigned char header[FRAME_HEADER_SIZE];
    uint32_t net_len;
    size_t len;
    char *buf;

    if (g_wire_format == WIRE_NDJSON)
        return ndjson_frame_read(fd, channel, flags, payload, payload_len);

    if (read_exact(fd, header, FRAME_HEADER_SIZE) < 0)
        return -1;

    *channel = header[0];
    *flags = header[1];
    memcpy(&net_len, header + 2, 4);
    len = ntohl(net_len);

    if (*channel > CHAN_MAX) {
        /* Unknown channel — skip payload */
        if (len > 0 && len <= FRAME_MAX_PAYLOAD) {
            buf = malloc(len);
            if (buf) { read_exact(fd, buf, len); free(buf); }
        }
        *payload = NULL;
        *payload_len = 0;
        return -1;
    }

    if (len > FRAME_MAX_PAYLOAD) {
        *payload = NULL;
        *payload_len = 0;
        return -1;
    }

    buf = malloc(len + 1);
    if (!buf) {
        *payload = NULL;
        *payload_len = 0;
        return -1;
    }

    if (len > 0) {
        if (read_exact(fd, buf, len) < 0) {
            free(buf);
            *payload = NULL;
            *payload_len = 0;
            return -1;
        }
    }
    buf[len] = '\0';

    *payload = buf;
    *payload_len = len;
    return 0;
}

/* Write a v2 frame to fd.
   Returns 0 on success, -1 on error. */
int
json_frame_write(int fd, int channel, int flags, const char *payload, size_t payload_len)
{
    unsigned char header[FRAME_HEADER_SIZE];
    uint32_t net_len;

    if (g_wire_format == WIRE_NDJSON)
        return ndjson_frame_write(fd, channel, payload, payload_len);

    if (payload_len > FRAME_MAX_PAYLOAD)
        return -1;

    header[0] = (unsigned char)channel;
    header[1] = (unsigned char)flags;
    net_len = htonl((uint32_t)payload_len);
    memcpy(header + 2, &net_len, 4);

    if (write_exact(fd, header, FRAME_HEADER_SIZE) < 0)
        return -1;

    if (payload_len > 0 && payload) {
        if (write_exact(fd, payload, payload_len) < 0)
            return -1;
    }

    return 0;
}

/* Write a v2 frame with a printf-formatted JSON payload.
   Always uses channel flags=0 (plain JSON). */
int
json_frame_write_fmt(int fd, int channel, const char *fmt, ...)
{
    char smallbuf[SERVER_MAX_LINE];
    char *buf = smallbuf;
    va_list ap, ap2;
    int len;
    int result;

    va_start(ap, fmt);
    va_copy(ap2, ap);
    len = vsnprintf(buf, sizeof(smallbuf), fmt, ap);
    va_end(ap);

    if (len < 0) {
        va_end(ap2);
        return -1;
    }

    if (len >= (int)sizeof(smallbuf)) {
        buf = malloc(len + 1);
        if (!buf) {
            va_end(ap2);
            return -1;
        }
        vsnprintf(buf, len + 1, fmt, ap2);
    }
    va_end(ap2);

    result = json_frame_write(fd, channel, 0, buf, len);
    if (buf != smallbuf)
        free(buf);
    return result;
}


/* ================================================================
 * Protocol version detection
 * ================================================================ */

/* Detect protocol version by peeking at the first byte (3-way).
 * 0x00–0x05 (channel ID)   → PROTOCOL_V2       (binary framing)
 * 0x7B ('{')                → PROTOCOL_V2_NDJSON (newline-delimited JSON)
 * >= 0x20 (other printable) → PROTOCOL_V1        (text protocol)
 *
 * No v1 command starts with '{', so this is unambiguous.
 * Reads one byte via recv(MSG_PEEK), stores it in *first_byte.
 * Returns PROTOCOL_V1, PROTOCOL_V2, PROTOCOL_V2_NDJSON, or -1 on error. */
int
protocol_detect_version(int fd, char *first_byte)
{
    unsigned char byte;
    ssize_t n;

    n = recv(fd, &byte, 1, MSG_PEEK);
    if (n <= 0)
        return -1;

    *first_byte = (char)byte;

    if (byte <= CHAN_MAX)
        return PROTOCOL_V2;
    else if (byte == '{')
        return PROTOCOL_V2_NDJSON;
    else
        return PROTOCOL_V1;
}


/* ================================================================
 * v2 JSON Session Handler
 *
 * Maps JSON messages on channels 0-2 to the existing C handlers.
 * ================================================================ */

/* Helper: get the write fd for a session */
static inline int
v2_wfd(client_session_t *session)
{
    return (session->write_fd >= 0) ? session->write_fd : session->fd;
}

/* Send a v2 error response on the given channel */
static void
v2_send_error(int wfd, int channel, const char *message)
{
    char escaped[1024];
    json_escape(message, escaped, sizeof(escaped));
    json_frame_write_fmt(wfd, channel,
        "{\"type\":\"error\",\"message\":\"%s\"}", escaped);
}

/* Handle Channel 0: CONTROL messages */
static int
v2_handle_control(client_session_t *session, server_config_t *config,
                  const char *payload)
{
    char type_buf[64];
    int wfd = v2_wfd(session);

    if (!json_get_string(payload, "type", type_buf, sizeof(type_buf))) {
        v2_send_error(wfd, CHAN_CONTROL, "missing type field");
        return 0;
    }

    if (strcmp(type_buf, "auth") == 0) {
        char token_buf[SERVER_TOKEN_HEXLEN + 16];

        if (!json_get_string(payload, "token", token_buf, sizeof(token_buf))) {
            v2_send_error(wfd, CHAN_CONTROL, "token required");
            return 0;
        }

        if (session->authenticated) {
            json_frame_write_fmt(wfd, CHAN_CONTROL,
                "{\"type\":\"auth_ok\",\"message\":\"already authenticated\"}");
            return 0;
        }

        if (protocol_secure_compare(token_buf, config->auth_token)) {
            session->authenticated = 1;
            init_bash_for_session(config);
            observe_init(wfd, OBSERVE_LEVEL_OFF);
            debug_init(session->fd, wfd);
            json_frame_write_fmt(wfd, CHAN_CONTROL,
                "{\"type\":\"auth_ok\",\"capabilities\":[\"state\",\"command\",\"observe\",\"debug\"]}");
        } else {
            v2_send_error(wfd, CHAN_CONTROL, "invalid token");
        }
        return 0;

    } else if (strcmp(type_buf, "ping") == 0) {
        json_frame_write_fmt(wfd, CHAN_CONTROL, "{\"type\":\"pong\"}");
        return 0;

    } else if (strcmp(type_buf, "disconnect") == 0) {
        json_frame_write_fmt(wfd, CHAN_CONTROL, "{\"type\":\"disconnect_ok\"}");
        return 1;  /* Signal to close */

    } else if (strcmp(type_buf, "configure") == 0) {
        int obs_level = 0;
        int actual_level;
        json_get_int(payload, "observability", &obs_level);
        actual_level = observe_set_level(obs_level);
        json_frame_write_fmt(wfd, CHAN_CONTROL,
            "{\"type\":\"configured\",\"observability\":%d}", actual_level);
        return 0;

    } else {
        v2_send_error(wfd, CHAN_CONTROL, "unknown control message type");
        return 0;
    }
}

/* Handle Channel 1: COMMAND messages.
 * For EVAL, we need to capture stdout/stderr and send them as JSON frames.
 * This reuses the capture_output logic from server_session.c but formats
 * the output as JSON frames instead of text protocol lines. */
static int
v2_handle_command(client_session_t *session, server_config_t *config,
                  const char *payload)
{
    char type_buf[64];
    char id_buf[128];
    int wfd = v2_wfd(session);

    (void)config;

    if (!session->authenticated) {
        v2_send_error(wfd, CHAN_COMMAND, "not authenticated");
        return 0;
    }

    if (!json_get_string(payload, "type", type_buf, sizeof(type_buf))) {
        v2_send_error(wfd, CHAN_COMMAND, "missing type field");
        return 0;
    }

    /* Get optional request ID */
    if (!json_get_string(payload, "id", id_buf, sizeof(id_buf)))
        id_buf[0] = '\0';

    if (strcmp(type_buf, "eval") == 0) {
        char cmd_buf[SERVER_MAX_CMD];
        char *cmd_copy;
        char stdout_path[] = "/tmp/bash-srv-XXXXXX";
        char stderr_path[] = "/tmp/bash-srv-XXXXXX";
        int stdout_tmpfd, stderr_tmpfd;
        int saved_stdout, saved_stderr;
        int exit_code;
        char *out_data, *err_data;
        size_t out_len, err_len;
        char *out_b64, *err_b64;
        char escaped_id[256];

        if (!json_get_string(payload, "command", cmd_buf, sizeof(cmd_buf))) {
            v2_send_error(wfd, CHAN_COMMAND, "command required");
            return 0;
        }

        /* Create temp files for output capture */
        stdout_tmpfd = mkstemp(stdout_path);
        if (stdout_tmpfd < 0) {
            v2_send_error(wfd, CHAN_COMMAND, "temp file creation failed");
            return 0;
        }
        stderr_tmpfd = mkstemp(stderr_path);
        if (stderr_tmpfd < 0) {
            close(stdout_tmpfd);
            unlink(stdout_path);
            v2_send_error(wfd, CHAN_COMMAND, "temp file creation failed");
            return 0;
        }
        unlink(stdout_path);
        unlink(stderr_path);

        saved_stdout = dup(STDOUT_FILENO);
        saved_stderr = dup(STDERR_FILENO);
        if (saved_stdout < 0 || saved_stderr < 0) {
            v2_send_error(wfd, CHAN_COMMAND, "dup failed");
            close(stdout_tmpfd);
            close(stderr_tmpfd);
            if (saved_stdout >= 0) close(saved_stdout);
            if (saved_stderr >= 0) close(saved_stderr);
            return 0;
        }

        dup2(stdout_tmpfd, STDOUT_FILENO);
        dup2(stderr_tmpfd, STDERR_FILENO);

        cmd_copy = strdup(cmd_buf);
        if (!cmd_copy) {
            fflush(stdout); fflush(stderr);
            dup2(saved_stdout, STDOUT_FILENO);
            dup2(saved_stderr, STDERR_FILENO);
            close(saved_stdout); close(saved_stderr);
            close(stdout_tmpfd); close(stderr_tmpfd);
            v2_send_error(wfd, CHAN_COMMAND, "out of memory");
            return 0;
        }

        parse_and_execute(cmd_copy, "bash-server", SEVAL_NONINT | SEVAL_NOHIST);
        exit_code = last_command_exit_value;

        fflush(stdout); fflush(stderr);
        dup2(saved_stdout, STDOUT_FILENO);
        dup2(saved_stderr, STDERR_FILENO);

        /* Read captured output */
        lseek(stdout_tmpfd, 0, SEEK_SET);
        lseek(stderr_tmpfd, 0, SEEK_SET);

        {
            /* Inline read_all_fd since it's static in server_session.c */
            char tmp[4096];
            ssize_t n;
            size_t cap;

            out_data = NULL; out_len = 0; cap = 0;
            while ((n = read(stdout_tmpfd, tmp, sizeof(tmp))) > 0) {
                if (out_len + n + 1 > cap) {
                    cap = cap ? cap * 2 : 8192;
                    if (cap > SERVER_MAX_OUTPUT) cap = SERVER_MAX_OUTPUT;
                    out_data = realloc(out_data, cap);
                    if (!out_data) { out_len = 0; break; }
                }
                if (out_len + (size_t)n >= cap) n = cap - out_len - 1;
                memcpy(out_data + out_len, tmp, n);
                out_len += n;
                if (out_len >= SERVER_MAX_OUTPUT - 1) break;
            }
            if (out_data) out_data[out_len] = '\0';

            err_data = NULL; err_len = 0; cap = 0;
            while ((n = read(stderr_tmpfd, tmp, sizeof(tmp))) > 0) {
                if (err_len + n + 1 > cap) {
                    cap = cap ? cap * 2 : 8192;
                    if (cap > SERVER_MAX_OUTPUT) cap = SERVER_MAX_OUTPUT;
                    err_data = realloc(err_data, cap);
                    if (!err_data) { err_len = 0; break; }
                }
                if (err_len + (size_t)n >= cap) n = cap - err_len - 1;
                memcpy(err_data + err_len, tmp, n);
                err_len += n;
                if (err_len >= SERVER_MAX_OUTPUT - 1) break;
            }
            if (err_data) err_data[err_len] = '\0';
        }

        close(stdout_tmpfd); close(stderr_tmpfd);
        close(saved_stdout); close(saved_stderr);

        /* Escape ID for JSON */
        if (id_buf[0])
            json_escape(id_buf, escaped_id, sizeof(escaped_id));

        /* Send stdout */
        out_b64 = (out_data && out_len > 0)
            ? protocol_base64_encode(out_data, out_len) : NULL;
        if (id_buf[0]) {
            json_frame_write_fmt(wfd, CHAN_COMMAND,
                "{\"type\":\"stdout\",\"id\":\"%s\",\"data\":\"%s\",\"encoding\":\"base64\"}",
                escaped_id, out_b64 ? out_b64 : "");
        } else {
            json_frame_write_fmt(wfd, CHAN_COMMAND,
                "{\"type\":\"stdout\",\"data\":\"%s\",\"encoding\":\"base64\"}",
                out_b64 ? out_b64 : "");
        }

        /* Send stderr */
        err_b64 = (err_data && err_len > 0)
            ? protocol_base64_encode(err_data, err_len) : NULL;
        if (id_buf[0]) {
            json_frame_write_fmt(wfd, CHAN_COMMAND,
                "{\"type\":\"stderr\",\"id\":\"%s\",\"data\":\"%s\",\"encoding\":\"base64\"}",
                escaped_id, err_b64 ? err_b64 : "");
        } else {
            json_frame_write_fmt(wfd, CHAN_COMMAND,
                "{\"type\":\"stderr\",\"data\":\"%s\",\"encoding\":\"base64\"}",
                err_b64 ? err_b64 : "");
        }

        /* Send completion */
        if (id_buf[0]) {
            json_frame_write_fmt(wfd, CHAN_COMMAND,
                "{\"type\":\"complete\",\"id\":\"%s\",\"exit_code\":%d}",
                escaped_id, exit_code);
        } else {
            json_frame_write_fmt(wfd, CHAN_COMMAND,
                "{\"type\":\"complete\",\"exit_code\":%d}", exit_code);
        }

        free(out_data); free(err_data);
        free(out_b64); free(err_b64);
        return 0;

    } else {
        v2_send_error(wfd, CHAN_COMMAND, "unknown command type");
        return 0;
    }
}

/* Handle Channel 2: STATE messages.
 * Maps JSON get/set/inspect to existing state_handle_* functions.
 * Since state_handle_* functions write v1 text responses, we need
 * to intercept their output.  We do this by using a pipe: the state
 * handler writes to a pipe, we read the v1 response and convert to JSON. */
static int
v2_handle_state(client_session_t *session, server_config_t *config,
                const char *payload)
{
    char type_buf[64];
    char target_buf[64];
    char name_buf[256];
    int wfd = v2_wfd(session);
    int pipefd[2];
    char v1_response[SERVER_MAX_LINE];
    int n;

    (void)config;

    if (!session->authenticated) {
        v2_send_error(wfd, CHAN_STATE, "not authenticated");
        return 0;
    }

    if (!json_get_string(payload, "type", type_buf, sizeof(type_buf))) {
        v2_send_error(wfd, CHAN_STATE, "missing type field");
        return 0;
    }

    if (strcmp(type_buf, "get") == 0) {
        if (!json_get_string(payload, "target", target_buf, sizeof(target_buf))) {
            v2_send_error(wfd, CHAN_STATE, "missing target field");
            return 0;
        }
        if (!json_get_string(payload, "name", name_buf, sizeof(name_buf))) {
            v2_send_error(wfd, CHAN_STATE, "missing name field");
            return 0;
        }

        /* Use pipe to capture v1 handler output, then convert to JSON */
        if (pipe(pipefd) < 0) {
            v2_send_error(wfd, CHAN_STATE, "pipe creation failed");
            return 0;
        }

        if (strcmp(target_buf, "var") == 0) {
            state_handle_get_var(pipefd[1], name_buf);
        } else if (strcmp(target_buf, "function") == 0) {
            state_handle_get_func(pipefd[1], name_buf);
        } else if (strcmp(target_buf, "alias") == 0) {
            state_handle_get_alias(pipefd[1], name_buf);
        } else {
            close(pipefd[0]); close(pipefd[1]);
            v2_send_error(wfd, CHAN_STATE, "unknown target");
            return 0;
        }

        close(pipefd[1]);
        n = protocol_read_line(pipefd[0], v1_response, sizeof(v1_response));
        close(pipefd[0]);

        if (n < 0) {
            v2_send_error(wfd, CHAN_STATE, "internal error reading response");
            return 0;
        }

        /* Parse v1 response and convert to JSON.
           Format: "VALUE <name> <base64> [attrs]" or "FUNC <name> <b64>"
                   or "ALIAS <name> <b64>" or "ERR <msg>" */
        if (strncmp(v1_response, "VALUE ", 6) == 0) {
            /* Parse: VALUE name base64 [attrs] */
            char *rp = v1_response + 6;
            char rname[256], rval[SERVER_MAX_LINE], rattrs[256];
            size_t ri = 0;

            /* Name */
            while (*rp && *rp != ' ' && ri < sizeof(rname) - 1)
                rname[ri++] = *rp++;
            rname[ri] = '\0';
            while (*rp == ' ') rp++;

            /* Base64 value */
            ri = 0;
            while (*rp && *rp != ' ' && ri < sizeof(rval) - 1)
                rval[ri++] = *rp++;
            rval[ri] = '\0';
            while (*rp == ' ') rp++;

            /* Attributes (optional) */
            ri = 0;
            while (*rp && ri < sizeof(rattrs) - 1)
                rattrs[ri++] = *rp++;
            rattrs[ri] = '\0';

            /* Decode base64 value for JSON response */
            {
                size_t decoded_len;
                char *decoded = protocol_base64_decode(rval, &decoded_len);
                char escaped_val[SERVER_MAX_LINE];
                char escaped_name[512];

                json_escape(rname, escaped_name, sizeof(escaped_name));
                if (decoded) {
                    json_escape(decoded, escaped_val, sizeof(escaped_val));
                    free(decoded);
                } else {
                    escaped_val[0] = '\0';
                }

                if (rattrs[0]) {
                    /* Convert comma-separated attrs to JSON array */
                    char json_attrs[512];
                    char *ap = json_attrs;
                    char *aend = json_attrs + sizeof(json_attrs) - 2;
                    char *tok = rattrs;
                    *ap++ = '[';
                    while (*tok && ap < aend) {
                        char *comma = strchr(tok, ',');
                        if (ap != json_attrs + 1) *ap++ = ',';
                        *ap++ = '"';
                        if (comma) {
                            size_t alen = comma - tok;
                            if (ap + alen + 1 < aend) {
                                memcpy(ap, tok, alen);
                                ap += alen;
                            }
                            tok = comma + 1;
                        } else {
                            size_t alen = strlen(tok);
                            if (ap + alen + 1 < aend) {
                                memcpy(ap, tok, alen);
                                ap += alen;
                            }
                            tok += strlen(tok);
                        }
                        *ap++ = '"';
                    }
                    *ap++ = ']';
                    *ap = '\0';

                    json_frame_write_fmt(wfd, CHAN_STATE,
                        "{\"type\":\"value\",\"target\":\"%s\",\"name\":\"%s\","
                        "\"value\":\"%s\",\"attributes\":%s}",
                        target_buf, escaped_name, escaped_val, json_attrs);
                } else {
                    json_frame_write_fmt(wfd, CHAN_STATE,
                        "{\"type\":\"value\",\"target\":\"%s\",\"name\":\"%s\","
                        "\"value\":\"%s\"}", target_buf, escaped_name, escaped_val);
                }
            }

        } else if (strncmp(v1_response, "FUNC ", 5) == 0 ||
                   strncmp(v1_response, "ALIAS ", 6) == 0) {
            /* Parse: FUNC/ALIAS name base64 */
            int skip = (v1_response[0] == 'F') ? 5 : 6;
            char *rp = v1_response + skip;
            char rname[256], rval[SERVER_MAX_LINE];
            size_t ri = 0;

            while (*rp && *rp != ' ' && ri < sizeof(rname) - 1)
                rname[ri++] = *rp++;
            rname[ri] = '\0';
            while (*rp == ' ') rp++;

            ri = 0;
            while (*rp && ri < sizeof(rval) - 1)
                rval[ri++] = *rp++;
            rval[ri] = '\0';

            {
                size_t decoded_len;
                char *decoded = protocol_base64_decode(rval, &decoded_len);
                char escaped_val[SERVER_MAX_LINE];
                char escaped_name[512];

                json_escape(rname, escaped_name, sizeof(escaped_name));
                if (decoded) {
                    json_escape(decoded, escaped_val, sizeof(escaped_val));
                    free(decoded);
                } else {
                    escaped_val[0] = '\0';
                }

                json_frame_write_fmt(wfd, CHAN_STATE,
                    "{\"type\":\"value\",\"target\":\"%s\",\"name\":\"%s\","
                    "\"value\":\"%s\"}", target_buf, escaped_name, escaped_val);
            }

        } else if (strncmp(v1_response, "ERR ", 4) == 0) {
            v2_send_error(wfd, CHAN_STATE, v1_response + 4);
        } else {
            v2_send_error(wfd, CHAN_STATE, "unexpected response format");
        }

        return 0;

    } else if (strcmp(type_buf, "set") == 0) {
        if (!json_get_string(payload, "target", target_buf, sizeof(target_buf))) {
            v2_send_error(wfd, CHAN_STATE, "missing target field");
            return 0;
        }
        if (!json_get_string(payload, "name", name_buf, sizeof(name_buf))) {
            v2_send_error(wfd, CHAN_STATE, "missing name field");
            return 0;
        }

        if (strcmp(target_buf, "var") == 0) {
            char value_buf[SERVER_MAX_CMD];
            char arg_buf[SERVER_MAX_CMD];
            char *attr_ptrs[8];
            char attr_storage[256];
            int nattrs;

            if (!json_get_string(payload, "value", value_buf, sizeof(value_buf))) {
                v2_send_error(wfd, CHAN_STATE, "missing value field");
                return 0;
            }

            /* Build v1-style argument: "name value [--flags]" */
            snprintf(arg_buf, sizeof(arg_buf), "%s %s", name_buf, value_buf);

            /* Check for attributes array */
            nattrs = json_get_string_array(payload, "attributes",
                attr_storage, sizeof(attr_storage), attr_ptrs, 8);
            {
                int ai;
                size_t pos = strlen(arg_buf);
                for (ai = 0; ai < nattrs && pos < sizeof(arg_buf) - 16; ai++) {
                    if (strcmp(attr_ptrs[ai], "exported") == 0)
                        pos += snprintf(arg_buf + pos, sizeof(arg_buf) - pos, " --export");
                    else if (strcmp(attr_ptrs[ai], "readonly") == 0)
                        pos += snprintf(arg_buf + pos, sizeof(arg_buf) - pos, " --readonly");
                    else if (strcmp(attr_ptrs[ai], "integer") == 0)
                        pos += snprintf(arg_buf + pos, sizeof(arg_buf) - pos, " --integer");
                }
            }

            /* Capture v1 handler response via pipe */
            if (pipe(pipefd) < 0) {
                v2_send_error(wfd, CHAN_STATE, "pipe creation failed");
                return 0;
            }
            state_handle_set_var(pipefd[1], arg_buf);
            close(pipefd[1]);
            n = protocol_read_line(pipefd[0], v1_response, sizeof(v1_response));
            close(pipefd[0]);

            if (n >= 0 && strncmp(v1_response, "OK", 2) == 0) {
                char esc_name[512];
                json_escape(name_buf, esc_name, sizeof(esc_name));
                json_frame_write_fmt(wfd, CHAN_STATE,
                    "{\"type\":\"set_ok\",\"target\":\"var\",\"name\":\"%s\"}",
                    esc_name);
            } else {
                v2_send_error(wfd, CHAN_STATE,
                    (n >= 0 && strncmp(v1_response, "ERR ", 4) == 0)
                    ? v1_response + 4 : "set failed");
            }

        } else if (strcmp(target_buf, "alias") == 0) {
            char value_buf[SERVER_MAX_CMD];
            char arg_buf[SERVER_MAX_CMD];

            if (!json_get_string(payload, "value", value_buf, sizeof(value_buf))) {
                v2_send_error(wfd, CHAN_STATE, "missing value field");
                return 0;
            }

            snprintf(arg_buf, sizeof(arg_buf), "%s %s", name_buf, value_buf);

            if (pipe(pipefd) < 0) {
                v2_send_error(wfd, CHAN_STATE, "pipe creation failed");
                return 0;
            }
            state_handle_set_alias(pipefd[1], arg_buf);
            close(pipefd[1]);
            n = protocol_read_line(pipefd[0], v1_response, sizeof(v1_response));
            close(pipefd[0]);

            if (n >= 0 && strncmp(v1_response, "OK", 2) == 0) {
                char esc_name[512];
                json_escape(name_buf, esc_name, sizeof(esc_name));
                json_frame_write_fmt(wfd, CHAN_STATE,
                    "{\"type\":\"set_ok\",\"target\":\"alias\",\"name\":\"%s\"}",
                    esc_name);
            } else {
                v2_send_error(wfd, CHAN_STATE,
                    (n >= 0 && strncmp(v1_response, "ERR ", 4) == 0)
                    ? v1_response + 4 : "set failed");
            }

        } else {
            v2_send_error(wfd, CHAN_STATE, "unknown target for set");
        }

        return 0;

    } else if (strcmp(type_buf, "unset") == 0) {
        if (!json_get_string(payload, "target", target_buf, sizeof(target_buf))) {
            v2_send_error(wfd, CHAN_STATE, "missing target field");
            return 0;
        }
        if (!json_get_string(payload, "name", name_buf, sizeof(name_buf))) {
            v2_send_error(wfd, CHAN_STATE, "missing name field");
            return 0;
        }

        if (pipe(pipefd) < 0) {
            v2_send_error(wfd, CHAN_STATE, "pipe creation failed");
            return 0;
        }

        if (strcmp(target_buf, "var") == 0)
            state_handle_unset_var(pipefd[1], name_buf);
        else if (strcmp(target_buf, "function") == 0)
            state_handle_unset_func(pipefd[1], name_buf);
        else if (strcmp(target_buf, "alias") == 0)
            state_handle_unset_alias(pipefd[1], name_buf);
        else {
            close(pipefd[0]); close(pipefd[1]);
            v2_send_error(wfd, CHAN_STATE, "unknown target for unset");
            return 0;
        }

        close(pipefd[1]);
        n = protocol_read_line(pipefd[0], v1_response, sizeof(v1_response));
        close(pipefd[0]);

        if (n >= 0 && strncmp(v1_response, "OK", 2) == 0) {
            char esc_name[512];
            json_escape(name_buf, esc_name, sizeof(esc_name));
            json_frame_write_fmt(wfd, CHAN_STATE,
                "{\"type\":\"unset_ok\",\"target\":\"%s\",\"name\":\"%s\"}",
                target_buf, esc_name);
        } else {
            v2_send_error(wfd, CHAN_STATE,
                (n >= 0 && strncmp(v1_response, "ERR ", 4) == 0)
                ? v1_response + 4 : "unset failed");
        }

        return 0;

    } else if (strcmp(type_buf, "inspect") == 0) {
        char query_buf[64];

        if (!json_get_string(payload, "query", query_buf, sizeof(query_buf))) {
            v2_send_error(wfd, CHAN_STATE, "missing query field");
            return 0;
        }

        /* Capture multi-line v1 inspect output via pipe */
        if (pipe(pipefd) < 0) {
            v2_send_error(wfd, CHAN_STATE, "pipe creation failed");
            return 0;
        }

        state_handle_inspect(pipefd[1], query_buf);
        close(pipefd[1]);

        /* Read all inspect lines and build JSON array */
        {
            /* Collect items as a JSON array string */
            char *items_buf = NULL;
            size_t items_cap = 0, items_len = 0;
            char line_buf[SERVER_MAX_LINE];

            #define ITEMS_APPEND(s, slen) do { \
                if (items_len + (slen) + 1 > items_cap) { \
                    items_cap = items_cap ? items_cap * 2 : 4096; \
                    items_buf = realloc(items_buf, items_cap); \
                    if (!items_buf) { items_len = 0; goto inspect_done; } \
                } \
                memcpy(items_buf + items_len, (s), (slen)); \
                items_len += (slen); \
            } while (0)

            ITEMS_APPEND("[", 1);

            while ((n = protocol_read_line(pipefd[0], line_buf, sizeof(line_buf))) >= 0) {
                if (strcmp(line_buf, "INSPECT-END") == 0)
                    break;

                /* Parse each VALUE/FUNC/ALIAS/TRAP line */
                if (items_len > 1)
                    ITEMS_APPEND(",", 1);

                /* Convert line to JSON object */
                if (strncmp(line_buf, "VALUE ", 6) == 0) {
                    char *lp = line_buf + 6;
                    char lname[256], lval[SERVER_MAX_LINE], lattrs[256];
                    size_t li = 0;

                    while (*lp && *lp != ' ' && li < sizeof(lname) - 1)
                        lname[li++] = *lp++;
                    lname[li] = '\0';
                    while (*lp == ' ') lp++;

                    li = 0;
                    while (*lp && *lp != ' ' && li < sizeof(lval) - 1)
                        lval[li++] = *lp++;
                    lval[li] = '\0';
                    while (*lp == ' ') lp++;

                    li = 0;
                    while (*lp && li < sizeof(lattrs) - 1)
                        lattrs[li++] = *lp++;
                    lattrs[li] = '\0';

                    {
                        size_t dlen;
                        char *decoded = protocol_base64_decode(lval, &dlen);
                        char esc_n[512], esc_v[SERVER_MAX_LINE];
                        char item[SERVER_MAX_LINE * 2];
                        int ilen;

                        json_escape(lname, esc_n, sizeof(esc_n));
                        if (decoded) {
                            json_escape(decoded, esc_v, sizeof(esc_v));
                            free(decoded);
                        } else {
                            esc_v[0] = '\0';
                        }

                        if (lattrs[0]) {
                            /* Build attrs array */
                            char ja[512];
                            char *jp = ja;
                            char *jend = ja + sizeof(ja) - 2;
                            char *tok = lattrs;
                            *jp++ = '[';
                            while (*tok && jp < jend) {
                                char *comma = strchr(tok, ',');
                                if (jp != ja + 1) *jp++ = ',';
                                *jp++ = '"';
                                size_t tlen = comma ? (size_t)(comma - tok) : strlen(tok);
                                if (jp + tlen + 1 < jend) { memcpy(jp, tok, tlen); jp += tlen; }
                                *jp++ = '"';
                                tok = comma ? comma + 1 : tok + tlen;
                            }
                            *jp++ = ']'; *jp = '\0';

                            ilen = snprintf(item, sizeof(item),
                                "{\"name\":\"%s\",\"value\":\"%s\",\"attributes\":%s}",
                                esc_n, esc_v, ja);
                        } else {
                            ilen = snprintf(item, sizeof(item),
                                "{\"name\":\"%s\",\"value\":\"%s\"}",
                                esc_n, esc_v);
                        }
                        if (ilen > 0)
                            ITEMS_APPEND(item, ilen);
                    }

                } else if (strncmp(line_buf, "FUNC ", 5) == 0) {
                    char *lp = line_buf + 5;
                    char lname[256], lval[SERVER_MAX_LINE];
                    size_t li = 0;

                    while (*lp && *lp != ' ' && li < sizeof(lname) - 1)
                        lname[li++] = *lp++;
                    lname[li] = '\0';
                    while (*lp == ' ') lp++;
                    li = 0;
                    while (*lp && li < sizeof(lval) - 1)
                        lval[li++] = *lp++;
                    lval[li] = '\0';

                    {
                        size_t dlen;
                        char *decoded = protocol_base64_decode(lval, &dlen);
                        char esc_n[512], esc_v[SERVER_MAX_LINE];
                        char item[SERVER_MAX_LINE * 2];
                        int ilen;

                        json_escape(lname, esc_n, sizeof(esc_n));
                        if (decoded) {
                            json_escape(decoded, esc_v, sizeof(esc_v));
                            free(decoded);
                        } else {
                            esc_v[0] = '\0';
                        }

                        ilen = snprintf(item, sizeof(item),
                            "{\"name\":\"%s\",\"definition\":\"%s\"}",
                            esc_n, esc_v);
                        if (ilen > 0)
                            ITEMS_APPEND(item, ilen);
                    }

                } else if (strncmp(line_buf, "ALIAS ", 6) == 0) {
                    char *lp = line_buf + 6;
                    char lname[256], lval[SERVER_MAX_LINE];
                    size_t li = 0;

                    while (*lp && *lp != ' ' && li < sizeof(lname) - 1)
                        lname[li++] = *lp++;
                    lname[li] = '\0';
                    while (*lp == ' ') lp++;
                    li = 0;
                    while (*lp && li < sizeof(lval) - 1)
                        lval[li++] = *lp++;
                    lval[li] = '\0';

                    {
                        size_t dlen;
                        char *decoded = protocol_base64_decode(lval, &dlen);
                        char esc_n[512], esc_v[SERVER_MAX_LINE];
                        char item[SERVER_MAX_LINE * 2];
                        int ilen;

                        json_escape(lname, esc_n, sizeof(esc_n));
                        if (decoded) {
                            json_escape(decoded, esc_v, sizeof(esc_v));
                            free(decoded);
                        } else {
                            esc_v[0] = '\0';
                        }

                        ilen = snprintf(item, sizeof(item),
                            "{\"name\":\"%s\",\"value\":\"%s\"}",
                            esc_n, esc_v);
                        if (ilen > 0)
                            ITEMS_APPEND(item, ilen);
                    }

                } else if (strncmp(line_buf, "TRAP ", 5) == 0) {
                    char *lp = line_buf + 5;
                    char lsig[64], lval[SERVER_MAX_LINE];
                    size_t li = 0;

                    while (*lp && *lp != ' ' && li < sizeof(lsig) - 1)
                        lsig[li++] = *lp++;
                    lsig[li] = '\0';
                    while (*lp == ' ') lp++;
                    li = 0;
                    while (*lp && li < sizeof(lval) - 1)
                        lval[li++] = *lp++;
                    lval[li] = '\0';

                    {
                        size_t dlen;
                        char *decoded = protocol_base64_decode(lval, &dlen);
                        char esc_s[128], esc_v[SERVER_MAX_LINE];
                        char item[SERVER_MAX_LINE * 2];
                        int ilen;

                        json_escape(lsig, esc_s, sizeof(esc_s));
                        if (decoded) {
                            json_escape(decoded, esc_v, sizeof(esc_v));
                            free(decoded);
                        } else {
                            esc_v[0] = '\0';
                        }

                        ilen = snprintf(item, sizeof(item),
                            "{\"signal\":\"%s\",\"command\":\"%s\"}",
                            esc_s, esc_v);
                        if (ilen > 0)
                            ITEMS_APPEND(item, ilen);
                    }
                }
            }

inspect_done:
            close(pipefd[0]);

            ITEMS_APPEND("]", 1);
            if (items_buf) {
                items_buf[items_len] = '\0';
                {
                    char esc_q[128];
                    json_escape(query_buf, esc_q, sizeof(esc_q));
                    json_frame_write_fmt(wfd, CHAN_STATE,
                        "{\"type\":\"inspect_result\",\"query\":\"%s\",\"data\":%s}",
                        esc_q, items_buf);
                }
                free(items_buf);
            } else {
                v2_send_error(wfd, CHAN_STATE, "out of memory");
            }
            #undef ITEMS_APPEND
        }

        return 0;

    } else {
        v2_send_error(wfd, CHAN_STATE, "unknown state message type");
        return 0;
    }
}

/* Main v2 session handler — reads frames and dispatches by channel */
int
json_session_handle(client_session_t *session, server_config_t *config)
{
    int channel, flags;
    char *payload;
    size_t payload_len;
    int done = 0;
    int wfd = v2_wfd(session);

    session->protocol_version = PROTOCOL_V2;

    fprintf(stderr, "bash-server[%d]: json_session_handle fd=%d (protocol v2)\n",
            (int)getpid(), session->fd);
    fflush(stderr);

    while (!done) {
        if (json_frame_read(session->fd, &channel, &flags, &payload, &payload_len) < 0) {
            break;  /* Connection closed or error */
        }

        switch (channel) {
        case CHAN_CONTROL:
            done = v2_handle_control(session, config, payload);
            break;
        case CHAN_COMMAND:
            done = v2_handle_command(session, config, payload);
            break;
        case CHAN_STATE:
            done = v2_handle_state(session, config, payload);
            break;
        case CHAN_OBSERVE: {
            /* CHAN_OBSERVE: client can subscribe to events */
            char obs_type[64];
            if (!session->authenticated) {
                v2_send_error(wfd, CHAN_OBSERVE, "not authenticated");
            } else if (json_get_string(payload, "type", obs_type, sizeof(obs_type)) &&
                       strcmp(obs_type, "subscribe") == 0) {
                int level = 1;
                json_get_int(payload, "level", &level);
                int actual = observe_set_level(level);
                json_frame_write_fmt(wfd, CHAN_OBSERVE,
                    "{\"type\":\"subscribed\",\"level\":%d}", actual);
            } else if (json_get_string(payload, "type", obs_type, sizeof(obs_type)) &&
                       strcmp(obs_type, "unsubscribe") == 0) {
                observe_set_level(0);
                json_frame_write_fmt(wfd, CHAN_OBSERVE,
                    "{\"type\":\"unsubscribed\"}");
            } else {
                v2_send_error(wfd, CHAN_OBSERVE, "unknown observe message type");
            }
            break;
        }
        case CHAN_PTY: {
            /* CHAN_PTY: spawn and relay PTY sessions */
            char pty_type[64];
            if (!session->authenticated) {
                v2_send_error(wfd, CHAN_PTY, "not authenticated");
            } else if (json_get_string(payload, "type", pty_type, sizeof(pty_type)) &&
                       strcmp(pty_type, "spawn") == 0) {
                /* pty_handle_spawn takes over the session loop.
                   It returns when the PTY exits or client disconnects. */
                pty_handle_spawn(session->fd, wfd, payload);
                /* After PTY exits, resume normal frame loop */
            } else {
                v2_send_error(wfd, CHAN_PTY, "send 'spawn' first to start PTY session");
            }
            break;
        }
        case CHAN_DEBUG:
            debug_handle_message(session->fd, wfd, payload);
            break;
        default:
            v2_send_error(wfd, CHAN_CONTROL, "unknown channel");
            break;
        }

        free(payload);
    }

    debug_cleanup();
    observe_cleanup();
    return 0;
}
