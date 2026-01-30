/* server_protocol.c -- Protocol handling for bash-server */

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
   along with Bash.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "server.h"
#include <stdarg.h>
#include <ctype.h>

/* Base64 encoding table */
static const char base64_table[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Base64 decoding table (initialized on first use) */
static unsigned char base64_decode_table[256];
static int base64_table_initialized = 0;

/* Initialize base64 decode table */
static void
init_base64_decode_table(void)
{
    int i;
    
    if (base64_table_initialized)
        return;
    
    memset(base64_decode_table, 0xff, sizeof(base64_decode_table));
    for (i = 0; i < 64; i++) {
        base64_decode_table[(unsigned char)base64_table[i]] = i;
    }
    base64_decode_table['='] = 0;  /* Padding character */
    base64_table_initialized = 1;
}

/* Read a line from socket (up to newline or buffer limit) */
int
protocol_read_line(int fd, char *buf, size_t bufsize)
{
    size_t pos = 0;
    ssize_t n;
    char c;
    
    while (pos < bufsize - 1) {
        n = read(fd, &c, 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0) {
            /* EOF */
            if (pos == 0)
                return -1;
            break;
        }
        
        if (c == '\n') {
            break;
        }
        if (c == '\r') {
            continue;  /* Skip CR */
        }
        
        buf[pos++] = c;
    }
    
    buf[pos] = '\0';
    return (int)pos;
}

/* Write a formatted line to socket */
int
protocol_write_line(int fd, const char *fmt, ...)
{
    char buf[SERVER_MAX_LINE];
    va_list ap;
    int len;
    ssize_t written, total;
    
    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    
    if (len < 0)
        return -1;
    
    /* Ensure we have room for newline */
    if (len >= (int)sizeof(buf) - 2)
        len = sizeof(buf) - 3;
    
    /* Add newline */
    buf[len++] = '\n';
    buf[len] = '\0';
    
    /* Write all data */
    total = 0;
    while (total < len) {
        written = write(fd, buf + total, len - total);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        total += written;
    }
    
    return (int)total;
}

/* Parse a command line into command and argument */
int
protocol_parse_command(const char *line, char *cmd, char *arg, size_t argsize)
{
    const char *p = line;
    size_t i;
    
    /* Skip leading whitespace */
    while (*p && isspace((unsigned char)*p))
        p++;
    
    /* Extract command (up to space or end) */
    i = 0;
    while (*p && !isspace((unsigned char)*p) && i < 31) {
        cmd[i++] = toupper((unsigned char)*p);
        p++;
    }
    cmd[i] = '\0';
    
    if (i == 0)
        return -1;  /* Empty command */
    
    /* Skip whitespace between command and argument */
    while (*p && isspace((unsigned char)*p))
        p++;
    
    /* Copy rest as argument */
    strncpy(arg, p, argsize - 1);
    arg[argsize - 1] = '\0';
    
    /* Trim trailing whitespace from argument */
    i = strlen(arg);
    while (i > 0 && isspace((unsigned char)arg[i - 1]))
        arg[--i] = '\0';
    
    return 0;
}

/* Base64 encode data */
char *
protocol_base64_encode(const char *data, size_t len)
{
    char *result;
    size_t outlen;
    size_t i, j;
    unsigned char a, b, c;
    
    /* Calculate output length */
    outlen = ((len + 2) / 3) * 4 + 1;
    result = malloc(outlen);
    if (!result)
        return NULL;
    
    j = 0;
    for (i = 0; i < len; i += 3) {
        a = (unsigned char)data[i];
        b = (i + 1 < len) ? (unsigned char)data[i + 1] : 0;
        c = (i + 2 < len) ? (unsigned char)data[i + 2] : 0;
        
        result[j++] = base64_table[a >> 2];
        result[j++] = base64_table[((a & 0x03) << 4) | (b >> 4)];
        result[j++] = (i + 1 < len) ? base64_table[((b & 0x0f) << 2) | (c >> 6)] : '=';
        result[j++] = (i + 2 < len) ? base64_table[c & 0x3f] : '=';
    }
    
    result[j] = '\0';
    return result;
}

/* Base64 decode data */
char *
protocol_base64_decode(const char *data, size_t *outlen)
{
    char *result;
    size_t len, rlen;
    size_t i, j;
    unsigned char a, b, c, d;
    
    init_base64_decode_table();
    
    len = strlen(data);
    if (len % 4 != 0) {
        *outlen = 0;
        return NULL;
    }
    
    /* Calculate output length */
    rlen = (len / 4) * 3;
    if (len > 0 && data[len - 1] == '=')
        rlen--;
    if (len > 1 && data[len - 2] == '=')
        rlen--;
    
    result = malloc(rlen + 1);
    if (!result) {
        *outlen = 0;
        return NULL;
    }
    
    j = 0;
    for (i = 0; i < len; i += 4) {
        a = base64_decode_table[(unsigned char)data[i]];
        b = base64_decode_table[(unsigned char)data[i + 1]];
        c = base64_decode_table[(unsigned char)data[i + 2]];
        d = base64_decode_table[(unsigned char)data[i + 3]];
        
        if (a == 0xff || b == 0xff) {
            free(result);
            *outlen = 0;
            return NULL;
        }
        
        result[j++] = (a << 2) | (b >> 4);
        if (data[i + 2] != '=')
            result[j++] = ((b & 0x0f) << 4) | (c >> 2);
        if (data[i + 3] != '=')
            result[j++] = ((c & 0x03) << 6) | d;
    }
    
    result[j] = '\0';
    *outlen = j;
    return result;
}

/* Constant-time string comparison for security tokens */
int
protocol_secure_compare(const char *a, const char *b)
{
    size_t len_a = strlen(a);
    size_t len_b = strlen(b);
    size_t i;
    unsigned char result = 0;
    
    /* Compare lengths first (still reveals length, but that's acceptable) */
    if (len_a != len_b)
        return 0;
    
    /* Constant-time comparison */
    for (i = 0; i < len_a; i++) {
        result |= (unsigned char)a[i] ^ (unsigned char)b[i];
    }
    
    return result == 0;
}
