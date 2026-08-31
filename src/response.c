#define _POSIX_C_SOURCE 200809L // ctime_r
#include "../include/response.h"
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

// Large enough for the status line + headers + the largest body a view emits
// (the read paths fail closed at BUFFER_SIZE/2 before reaching here).
#define RESPONSE_MAX 8192

// Per-worker connection disposition for the response being built (see header).
// Thread-local: each worker serves one connection at a time, no races.
static _Thread_local int g_conn_close = 1;

void response_set_connection_close(int close_after)
{
    g_conn_close = close_after ? 1 : 0;
}

void response_send_all(int fd, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return; // client-local failure
        }
        sent += (size_t)n;
    }
}

// Thread-safe current date, newline stripped.
static void now_str(char *buf, size_t n)
{
    time_t t;
    time(&t);
    char tmp[32];
    ctime_r(&t, tmp);
    tmp[strcspn(tmp, "\n")] = '\0';
    snprintf(buf, n, "%s", tmp);
}

void response_log_prefix(void)
{
    char date[32];
    now_str(date, sizeof(date));
    printf("[%s] - ", date);
}

void response_send(int fd, const char *status, const char *content_type, const char *body)
{
    char date[32];
    now_str(date, sizeof(date));

    const char *conn = g_conn_close ? "close" : "keep-alive";
    char msg[RESPONSE_MAX];
    int n = snprintf(msg, sizeof(msg),
                     "HTTP/1.1 %s\r\nDate: %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: %s\r\n\r\n%s",
                     status, date, content_type, strlen(body), conn, body);

    // Never send a response whose bytes disagree with its Content-Length.
    if (n < 0 || (size_t)n >= sizeof(msg))
        return;
    response_send_all(fd, msg, (size_t)n);
}

void response_send_no_content(int fd)
{
    char date[32];
    now_str(date, sizeof(date));
    const char *conn = g_conn_close ? "close" : "keep-alive";

    // No Content-Length and no body: a 204 is self-delimiting (RFC 7230).
    char msg[128];
    int n = snprintf(msg, sizeof(msg),
                     "HTTP/1.1 204 No Content\r\nDate: %s\r\nConnection: %s\r\n\r\n",
                     date, conn);
    if (n < 0 || (size_t)n >= sizeof(msg))
        return;
    response_send_all(fd, msg, (size_t)n);
}

void response_send_unauthorized(int fd, const char *realm)
{
    char date[32];
    now_str(date, sizeof(date));
    const char *conn = g_conn_close ? "close" : "keep-alive";
    const char *body = "{\"msg\": \"unauthorized\"}";

    char msg[RESPONSE_MAX];
    int n = snprintf(msg, sizeof(msg),
                     "HTTP/1.1 401 Unauthorized\r\nDate: %s\r\nContent-Type: application/json\r\n"
                     "Content-Length: %zu\r\nWWW-Authenticate: Basic realm=\"%s\"\r\n"
                     "Connection: %s\r\n\r\n%s",
                     date, strlen(body), realm, conn, body);
    if (n < 0 || (size_t)n >= sizeof(msg))
        return;
    response_send_all(fd, msg, (size_t)n);
}
