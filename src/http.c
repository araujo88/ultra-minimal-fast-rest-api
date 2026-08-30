#define _GNU_SOURCE // strcasestr
#include "../include/http.h"
#include <string.h>
#include <strings.h> // strncasecmp
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <sys/socket.h>

// ---------------------------------------------------------------------------
// Request framing over the TCP byte stream
// ---------------------------------------------------------------------------

static size_t parse_content_length(const char *buf)
{
    const char *h = strcasestr(buf, "content-length:");
    if (!h)
        return 0;
    h += strlen("content-length:");
    while (*h == ' ' || *h == '\t')
        h++;
    long v = strtol(h, NULL, 10);
    return (v > 0) ? (size_t)v : 0;
}

char *find_body(char *buf)
{
    char *p = strstr(buf, "\r\n\r\n");
    if (p)
        return p + 4;
    p = strstr(buf, "\n\n");
    if (p)
        return p + 2;
    return NULL;
}

ssize_t recv_request(int fd, char *buf, size_t cap, size_t *len)
{
    for (;;)
    {
        // Try to frame one complete request out of what is already buffered.
        if (*len > 0)
        {
            buf[*len] = '\0'; // NUL-terminate for the text header search
            char *p = strstr(buf, "\r\n\r\n");
            size_t sep = 4;
            if (!p)
            {
                p = strstr(buf, "\n\n");
                sep = 2;
            }
            if (p)
            {
                size_t header_len = (size_t)(p - buf) + sep;
                // Bound the Content-Length search to this request's headers so a
                // pipelined follow-up request's Content-Length is not misread.
                char saved = buf[header_len];
                buf[header_len] = '\0';
                size_t need = header_len + parse_content_length(buf);
                buf[header_len] = saved;

                if (need <= *len)
                    return (ssize_t)need; // one full request is buffered
                if (need >= cap)
                    return -1; // request larger than the buffer
            }
            else if (*len >= cap - 1)
            {
                return -1; // no header terminator and the buffer is full
            }
        }

        ssize_t n = recv(fd, buf + *len, cap - 1 - *len, 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            return 0; // peer closed; any partial bytes are discarded
        *len += (size_t)n;
    }
}

int request_keep_alive(const char *req)
{
    // Default per HTTP version: keep-alive for 1.1, close for 1.0/unknown.
    const char *v = strstr(req, "HTTP/1.");
    int http11 = (v != NULL && v[7] == '1');

    // An explicit Connection header wins over the version default.
    const char *conn = strcasestr(req, "connection:");
    if (conn)
    {
        conn += strlen("connection:");
        while (*conn == ' ' || *conn == '\t')
            conn++;
        if (strncasecmp(conn, "close", 5) == 0)
            return 0;
        if (strncasecmp(conn, "keep-alive", 10) == 0)
            return 1;
    }
    return http11;
}

// ---------------------------------------------------------------------------
// Request-line, path, id and form parsing (bounded, structured)
// ---------------------------------------------------------------------------

int parse_request_line(const char *buf, char *method, size_t msz,
                       char *target, size_t tsz)
{
    const char *p = buf;
    size_t i = 0;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i < msz - 1)
        method[i++] = *p++;
    method[i] = '\0';
    if (*p != ' ')
        return 0;
    p++;

    i = 0;
    while (*p && *p != ' ' && *p != '\r' && *p != '\n' && i < tsz - 1)
        target[i++] = *p++;
    target[i] = '\0';
    return i > 0;
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static void url_decode(char *dst, size_t dstsz, const char *src, size_t srclen)
{
    size_t di = 0;
    for (size_t si = 0; si < srclen && di + 1 < dstsz; si++)
    {
        char c = src[si];
        if (c == '+')
        {
            dst[di++] = ' ';
        }
        else if (c == '%' && si + 2 < srclen &&
                 hexval((unsigned char)src[si + 1]) >= 0 &&
                 hexval((unsigned char)src[si + 2]) >= 0)
        {
            dst[di++] = (char)((hexval((unsigned char)src[si + 1]) << 4) |
                               hexval((unsigned char)src[si + 2]));
            si += 2;
        }
        else
        {
            dst[di++] = c;
        }
    }
    dst[di] = '\0';
}

void parse_form(const char *body, char model_string[NUM_COLS][STR_LEN])
{
    for (int i = 0; i < NUM_COLS; i++)
        model_string[i][0] = '\0';
    if (!body)
        return;

    const char *p = body;
    while (*p)
    {
        const char *amp = strchr(p, '&');
        const char *end = amp ? amp : p + strlen(p);
        const char *eq = memchr(p, '=', (size_t)(end - p));
        if (eq)
        {
            size_t klen = (size_t)(eq - p);
            size_t vlen = (size_t)(end - (eq + 1));
            char key[128];
            url_decode(key, sizeof(key), p, klen);
            for (int i = 0; i < NUM_COLS; i++)
            {
                if (strcmp(key, TABLE_COLS[i][0]) == 0)
                    url_decode(model_string[i], STR_LEN, eq + 1, vlen);
            }
        }
        if (!amp)
            break;
        p = amp + 1;
    }
}

int parse_id(const char *s, unsigned int *out)
{
    if (!s || !*s)
        return 0;
    errno = 0;
    char *end = NULL;
    unsigned long v = strtoul(s, &end, 10);
    if (end == s || *end != '\0' || errno != 0 || v > UINT_MAX)
        return 0;
    *out = (unsigned int)v;
    return 1;
}
