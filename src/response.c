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

    char msg[RESPONSE_MAX];
    int n = snprintf(msg, sizeof(msg),
                     "HTTP/1.1 %s\r\nDate: %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n%s",
                     status, date, content_type, strlen(body), body);

    // Never send a response whose bytes disagree with its Content-Length.
    if (n < 0 || (size_t)n >= sizeof(msg))
        return;
    response_send_all(fd, msg, (size_t)n);
}
