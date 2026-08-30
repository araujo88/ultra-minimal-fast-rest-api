#define _GNU_SOURCE // strcasestr
#include "../include/server.h"
#include "../include/views.h"
#include "../include/database.h"
#include "../include/settings.h"
#include "../include/models.h"
#include <limits.h>
#include <ctype.h>

extern int server_socket; // owned/defined by main.c

// ---------------------------------------------------------------------------
// Low-level socket helpers
// ---------------------------------------------------------------------------

static int make_listening_socket(const char *ip, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // allow quick restart

    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    if (inet_pton(AF_INET, ip, &server_address.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid bind address: %s\n", ip);
        exit(EXIT_FAILURE);
    }

    printf("Binding socket ...\n");
    if (bind(fd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    printf("Binding done!\n");
    return fd;
}

// Send the whole buffer, tolerating partial writes and EINTR.
static void send_all(int fd, const char *data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return; // client-local failure: give up on this connection only
        }
        sent += (size_t)n;
    }
}

static void log_date(void)
{
    time_t t;
    time(&t);
    char *d = ctime(&t);
    d[strcspn(d, "\n")] = 0;
    printf("[%s] - ", d);
}

static void send_error(int fd, const char *status, const char *html)
{
    char msg[BUFFER_SIZE];
    int n = snprintf(msg, sizeof(msg),
                     "HTTP/1.1 %s\r\nContent-Type: text/html\r\nContent-Length: %zu\r\n\r\n%s",
                     status, strlen(html), html);
    if (n > 0)
        send_all(fd, msg, (size_t)n);
    log_date();
    printf("HTTP/1.1 %s\n", status);
}

// ---------------------------------------------------------------------------
// HTTP request reception (proper framing over the TCP byte stream)
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

static char *find_body(char *buf)
{
    char *p = strstr(buf, "\r\n\r\n");
    if (p)
        return p + 4;
    p = strstr(buf, "\n\n");
    if (p)
        return p + 2;
    return NULL;
}

// Read a complete request: headers up to the blank line, then Content-Length
// body bytes. Bounded by cap; never assumes one recv() == one request.
// Returns bytes read (>0), 0 on clean peer close before any data, -1 on error.
static ssize_t recv_request(int fd, char *buf, size_t cap)
{
    size_t total = 0;
    long header_end = -1;
    size_t content_length = 0;
    int have_len = 0;

    while (total < cap - 1)
    {
        ssize_t n = recv(fd, buf + total, cap - 1 - total, 0);
        if (n < 0)
        {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break; // peer closed
        total += (size_t)n;
        buf[total] = '\0';

        if (header_end < 0)
        {
            char *p = strstr(buf, "\r\n\r\n");
            size_t sep = 4;
            if (!p)
            {
                p = strstr(buf, "\n\n");
                sep = 2;
            }
            if (p)
            {
                header_end = (long)(p - buf) + (long)sep;
                content_length = parse_content_length(buf);
                have_len = 1;
            }
        }

        if (header_end >= 0)
        {
            size_t body_have = total - (size_t)header_end;
            if (!have_len || body_have >= content_length)
                break; // full request received
        }
    }
    return (ssize_t)total;
}

// ---------------------------------------------------------------------------
// Request-line, path, id and form parsing (bounded, structured)
// ---------------------------------------------------------------------------

static int parse_request_line(const char *buf, char *method, size_t msz,
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

// Parse application/x-www-form-urlencoded body into model_string, matching
// keys against the model columns. Missing fields are left as empty strings,
// values are URL-decoded and bounded. NULL-safe.
static void parse_form(const char *body, char model_string[NUM_COLS][STR_LEN])
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

static int parse_id(const char *s, unsigned int *out)
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

// ---------------------------------------------------------------------------
// Routing (structured: exact method + path, never substring over raw bytes)
// ---------------------------------------------------------------------------

static void route_request(int fd, const char *method, const char *target, const char *body)
{
    char base[128];       // "/users"
    char baseslash[130];  // "/users/"
    snprintf(base, sizeof(base), "/%s", TABLE_NAME);
    snprintf(baseslash, sizeof(baseslash), "/%s/", TABLE_NAME);

    if (strcmp(target, "/") == 0)
    {
        if (strcmp(method, "GET") == 0)
            root_view(&fd);
        else
            send_error(fd, "405 Method Not Allowed", "<html><h1>405 Method Not Allowed</h1></html>");
        return;
    }

    if (strcmp(target, base) == 0)
    {
        if (strcmp(method, "GET") == 0)
        {
            get_users_view(&fd);
        }
        else if (strcmp(method, "POST") == 0)
        {
            char model_string[NUM_COLS][STR_LEN];
            parse_form(body, model_string);
            create_user_view(&fd, model_string);
        }
        else
        {
            send_error(fd, "405 Method Not Allowed", "<html><h1>405 Method Not Allowed</h1></html>");
        }
        return;
    }

    if (strncmp(target, baseslash, strlen(baseslash)) == 0)
    {
        unsigned int id;
        if (!parse_id(target + strlen(baseslash), &id))
        {
            send_error(fd, "400 Bad Request", "<html><h1>400 Bad Request - invalid id</h1></html>");
            return;
        }
        if (strcmp(method, "GET") == 0)
        {
            get_user_view(&fd, id);
        }
        else if (strcmp(method, "PUT") == 0)
        {
            char model_string[NUM_COLS][STR_LEN];
            parse_form(body, model_string);
            update_user_view(&fd, id, model_string);
        }
        else if (strcmp(method, "DELETE") == 0)
        {
            delete_user_view(&fd, id);
        }
        else
        {
            send_error(fd, "405 Method Not Allowed", "<html><h1>405 Method Not Allowed</h1></html>");
        }
        return;
    }

    error_not_found(&fd);
}

// ---------------------------------------------------------------------------
// Per-connection worker entry point
// ---------------------------------------------------------------------------

void send_data(void *client_socket)
{
    int fd = *(int *)client_socket;
    char buf[BUFFER_SIZE] = {0};

    ssize_t n = recv_request(fd, buf, sizeof(buf));
    if (n <= 0)
    {
        close(fd);
        free(client_socket);
        return;
    }

    char method[16];
    char target[2048];
    if (!parse_request_line(buf, method, sizeof(method), target, sizeof(target)))
    {
        send_error(fd, "400 Bad Request", "<html><h1>400 Bad Request</h1></html>");
        close(fd);
        free(client_socket);
        return;
    }

    char *body = find_body(buf);

    log_date();
    printf("%s %s\n", method, target);

    route_request(fd, method, target, body);

    close(fd);
    free(client_socket);
}

// ---------------------------------------------------------------------------
// Client IP allowlist (now reading the real accepted peer address)
// ---------------------------------------------------------------------------

static bool check_client_ip(int client_socket, struct sockaddr_in *client_address)
{
    char client_ip_address[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &client_address->sin_addr, client_ip_address, INET_ADDRSTRLEN);

    for (int i = 0; i < NUM_ALLOWED_HOSTS; i++)
    {
        if (strcmp(client_ip_address, ALLOWED_HOSTS[i]) == 0)
            return true;
    }

    log_date();
    printf("HTTP/1.1 403 Forbidden (%s)\n", client_ip_address);
    send_error(client_socket, "403 Forbidden", "");
    return false;
}

// ---------------------------------------------------------------------------
// Server setup and accept loop
// ---------------------------------------------------------------------------

void create_server(char *ip, int port, int max_connections, thread_pool_t *pool)
{
    printf("Creating socket ...\n");
    server_socket = make_listening_socket(ip, port);
    printf("Socket created!\n");

    printf("Initializing database connection...\n");
    open_database();
    check_version();
    create_table();

    if (listen(server_socket, max_connections) < 0)
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Waiting for incoming requests... (press Ctrl+C to quit)\n");
    while (server_running)
    {
        struct sockaddr_in client_address;
        socklen_t addr_len = sizeof(client_address);
        memset(&client_address, 0, sizeof(client_address));

        int client_fd = accept(server_socket, (struct sockaddr *)&client_address, &addr_len);
        if (client_fd < 0)
        {
            if (errno == EINTR)
                break; // interrupted by SIGINT -> shut down
            perror("Accept failed");
            continue; // transient error: keep serving
        }

        // Bound how long a worker can be held by a slow/stalled client so a
        // handful of idle connections cannot wedge the whole pool (slowloris).
        struct timeval tv = {.tv_sec = 10, .tv_usec = 0};
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        if (!check_client_ip(client_fd, &client_address))
        {
            close(client_fd); // no fd/memory leak on the rejected path
            continue;
        }

        int *client_socket = malloc(sizeof(int));
        if (!client_socket)
        {
            close(client_fd);
            continue;
        }
        *client_socket = client_fd;
        thread_pool_add_task(pool, send_data, client_socket);
    }

    // Clean shutdown, all from normal (non-signal) context.
    printf("\nShutting down ...\n");
    close(server_socket);
    thread_pool_cleanup(pool);
    close_database();
    printf("All threads terminated. Database closed.\n");
}
