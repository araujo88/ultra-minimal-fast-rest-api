#define _GNU_SOURCE  // strcasestr
#include <strings.h> // strncasecmp
#include "../include/server.h"
#include "../include/views.h"
#include "../include/database.h"
#include "../include/settings.h"
#include "../include/models.h"
#include "../include/http.h"
#include "../include/response.h"

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

// Emit an error response (text/html) and log it. Response construction and the
// timestamped log prefix live in the response module (response.c).
static void send_error(int fd, const char *status, const char *html)
{
    response_log_prefix();
    printf("HTTP/1.1 %s\n", status);
    response_send(fd, status, "text/html", html);
}

// HTTP request framing and request-line/path/id/form parsing live in http.c
// (declared in http.h) so the parsers can be unit-tested and fuzzed in
// isolation from transport and routing.

// ---------------------------------------------------------------------------
// Routing (structured: exact method + path, never substring over raw bytes)
// ---------------------------------------------------------------------------

static void route_request(int fd, const char *method, const char *target, const char *body)
{
    char base[128];      // "/users"
    char baseslash[130]; // "/users/"
    snprintf(base, sizeof(base), "/%s", TABLE_NAME);
    snprintf(baseslash, sizeof(baseslash), "/%s/", TABLE_NAME);

    if (strcmp(target, "/livez") == 0)
    {
        if (strcmp(method, "GET") == 0)
            livez_view(&fd);
        else
            send_error(fd, "405 Method Not Allowed", "<html><h1>405 Method Not Allowed</h1></html>");
        return;
    }

    if (strcmp(target, "/readyz") == 0 || strcmp(target, "/health") == 0)
    {
        if (strcmp(method, "GET") == 0)
            health_view(&fd);
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

// Cap on requests served over a single kept-alive connection. Bounds how long
// one client can hold a worker (a persistent connection pins a worker in this
// blocking pool); an idle connection is also dropped by SO_RCVTIMEO.
#define MAX_KEEPALIVE_REQUESTS 100

// Optional HTTP Basic auth (defined further below); used by the request loop.
#define AUTH_REALM "ultra-minimal-fast-rest-api"
static int basic_auth_ok(const char *req);

// Health/liveness endpoints are exempt from Basic auth so orchestrators and
// load balancers can probe them without credentials (the IP allowlist still
// applies).
static int is_health_target(const char *target)
{
    return strcmp(target, "/livez") == 0 || strcmp(target, "/readyz") == 0 ||
           strcmp(target, "/health") == 0;
}

void send_data(void *client_socket)
{
    int fd = *(int *)client_socket;
    free(client_socket); // the fd is owned by this function from here on

    char buf[BUFFER_SIZE];
    size_t len = 0; // bytes currently buffered; may span multiple requests
    int served = 0;

    for (;;)
    {
        ssize_t req_len = recv_request(fd, buf, sizeof(buf), &len);
        if (req_len <= 0)
            break; // clean close, error, idle timeout, or oversized request

        // Isolate this one request from any pipelined bytes while parsing it.
        char saved = buf[req_len];
        buf[req_len] = '\0';

        char method[16];
        char target[2048];
        int ok = parse_request_line(buf, method, sizeof(method), target, sizeof(target));

        // Keep the connection alive only if the client wants it, the request
        // parsed, and we are under the per-connection cap; otherwise close.
        int keep_alive = ok && request_keep_alive(buf) && (served + 1 < MAX_KEEPALIVE_REQUESTS);
        response_set_connection_close(!keep_alive);

        if (!ok)
        {
            send_error(fd, "400 Bad Request", "<html><h1>400 Bad Request</h1></html>");
            break; // a malformed request desynchronizes the stream: stop
        }

        if (!is_health_target(target) && !basic_auth_ok(buf))
        {
            response_log_prefix();
            printf("%s %s -> 401 Unauthorized\n", method, target);
            response_send_unauthorized(fd, AUTH_REALM);
        }
        else
        {
            char *body = find_body(buf);
            response_log_prefix();
            printf("%s %s\n", method, target);
            route_request(fd, method, target, body);
        }

        // Consume this request and shift any pipelined bytes to the front.
        buf[req_len] = saved;
        memmove(buf, buf + req_len, len - (size_t)req_len);
        len -= (size_t)req_len;
        served++;

        if (!keep_alive)
            break;
    }

    close(fd);
}

// ---------------------------------------------------------------------------
// Client IP allowlist (reads the real accepted peer address)
//
// The allowed hosts come from the ALLOWED_HOSTS environment variable when set
// (comma-separated IPv4 addresses; the single value "*" allows all clients),
// otherwise from the compile-time default in settings.h. This is parsed once
// at startup from the single-threaded accept loop, so no locking is needed.
//
// Rationale: behind Docker's bridge network, clients arrive from the gateway
// IP (e.g. 172.17.0.1), not 127.0.0.1, so a hardcoded localhost list would
// reject every forwarded request. Operators set ALLOWED_HOSTS to match their
// deployment (or "*" when the container/network boundary is the real control).
// ---------------------------------------------------------------------------

static char **g_allowed_hosts = NULL;
static int g_allowed_count = 0;
static bool g_allow_all = false;
static char *g_allowed_env_copy = NULL; // backing storage for tokenized env

// Maximum length of the ALLOWED_HOSTS value we will parse. A valid list is
// short (each IPv4 literal is <= 15 chars); anything larger is rejected rather
// than processed.
#define ALLOWED_HOSTS_MAX 4096

static void use_default_allowlist(void)
{
    g_allowed_hosts = malloc(sizeof(char *) * NUM_ALLOWED_HOSTS);
    for (int i = 0; i < NUM_ALLOWED_HOSTS; i++)
        g_allowed_hosts[i] = ALLOWED_HOSTS[i];
    g_allowed_count = NUM_ALLOWED_HOSTS;
}

static void init_allowlist(void)
{
    // getenv() is untrusted input (CWE-807/CWE-20): treat it as such -- bound
    // its length and accept only syntactically valid IPv4 literals (or "*"),
    // discarding anything else. It is also a legitimate operator-controlled
    // configuration channel; only someone who already controls the process
    // environment can set it.
    // Flawfinder: ignore getenv
    const char *env = getenv("ALLOWED_HOSTS");
    if (!env || !*env)
    {
        use_default_allowlist();
        return;
    }

    if (strcmp(env, "*") == 0)
    {
        g_allow_all = true;
        printf("Allowlist: * (all clients permitted)\n");
        return;
    }

    if (strlen(env) > ALLOWED_HOSTS_MAX)
    {
        fprintf(stderr, "ALLOWED_HOSTS too long (> %d bytes); using default allowlist\n",
                ALLOWED_HOSTS_MAX);
        use_default_allowlist();
        return;
    }

    g_allowed_env_copy = strdup(env);
    int cap = 1;
    for (const char *p = env; *p; p++)
        if (*p == ',')
            cap++;
    g_allowed_hosts = malloc(sizeof(char *) * cap);

    for (char *tok = strtok(g_allowed_env_copy, ","); tok != NULL; tok = strtok(NULL, ","))
    {
        while (*tok == ' ' || *tok == '\t')
            tok++;
        char *end = tok + strlen(tok);
        while (end > tok && (end[-1] == ' ' || end[-1] == '\t'))
            *--end = '\0';

        struct in_addr parsed;
        if (inet_pton(AF_INET, tok, &parsed) == 1)
            g_allowed_hosts[g_allowed_count++] = tok; // valid IPv4 literal only
        else if (*tok)
            fprintf(stderr, "ALLOWED_HOSTS: ignoring invalid entry '%s'\n", tok);
    }

    // If nothing valid was provided, fall back to the built-in (restrictive)
    // default rather than silently denying every client.
    if (g_allowed_count == 0)
    {
        fprintf(stderr, "ALLOWED_HOSTS had no valid entries; using default allowlist\n");
        free(g_allowed_hosts);
        free(g_allowed_env_copy);
        g_allowed_env_copy = NULL;
        use_default_allowlist();
    }
}

static void free_allowlist(void)
{
    free(g_allowed_hosts);    // the host pointers are either into g_allowed_env_copy
    free(g_allowed_env_copy); // or static ALLOWED_HOSTS strings -- don't free those
    g_allowed_hosts = NULL;
    g_allowed_env_copy = NULL;
    g_allowed_count = 0;
}

// ---------------------------------------------------------------------------
// Optional HTTP Basic authentication
//
// Enabled only when the BASIC_AUTH environment variable is set to
// "user:password". We base64-encode that once at startup and compare it,
// constant-time, against the token in each request's Authorization header --
// so the untrusted header is never base64-decoded. NOTE: Basic auth over plain
// HTTP only base64-encodes credentials (no encryption); it is minimal auth for
// a trusted/dev network, not a substitute for TLS. See SECURITY.md.
// ---------------------------------------------------------------------------

static char *g_auth_expected = NULL; // base64("user:password"), or NULL if disabled

static char *base64_encode(const char *in)
{
    static const char tbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t len = strlen(in);
    char *out = malloc(4 * ((len + 2) / 3) + 1);
    if (!out)
        return NULL;
    size_t i, o = 0;
    for (i = 0; i + 3 <= len; i += 3)
    {
        unsigned v = (unsigned char)in[i] << 16 | (unsigned char)in[i + 1] << 8 | (unsigned char)in[i + 2];
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = tbl[(v >> 6) & 63];
        out[o++] = tbl[v & 63];
    }
    if (i < len) // 1 or 2 trailing bytes
    {
        int rem = (int)(len - i);
        unsigned v = (unsigned char)in[i] << 16;
        if (rem == 2)
            v |= (unsigned char)in[i + 1] << 8;
        out[o++] = tbl[(v >> 18) & 63];
        out[o++] = tbl[(v >> 12) & 63];
        out[o++] = (rem == 2) ? tbl[(v >> 6) & 63] : '=';
        out[o++] = '=';
    }
    out[o] = '\0';
    return out;
}

static void init_basic_auth(void)
{
    // Runtime env var wins; otherwise fall back to the compile-time default in
    // settings.h (empty = disabled), mirroring the ALLOWED_HOSTS pattern.
    // Flawfinder: ignore getenv
    const char *cred = getenv("BASIC_AUTH");
    if (!cred || !*cred)
        cred = BASIC_AUTH_DEFAULT;
    if (cred && *cred)
    {
        g_auth_expected = base64_encode(cred);
        printf("Basic auth: enabled\n");
    }
}

static void free_basic_auth(void)
{
    free(g_auth_expected);
    g_auth_expected = NULL;
}

// Constant-time string equality (avoids leaking the match length via timing).
static int ct_equal(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    unsigned char diff = (la == lb) ? 0 : 1;
    for (size_t i = 0; i < la && i < lb; i++)
        diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

// Returns 1 if the request is authorized (auth disabled, or a matching
// Authorization: Basic <token>). req must be NUL-terminated at the request end.
static int basic_auth_ok(const char *req)
{
    if (g_auth_expected == NULL)
        return 1; // auth disabled

    const char *h = strcasestr(req, "authorization:");
    if (!h)
        return 0;
    h += strlen("authorization:");
    while (*h == ' ' || *h == '\t')
        h++;
    if (strncasecmp(h, "basic ", 6) != 0)
        return 0;
    h += 6;
    while (*h == ' ' || *h == '\t')
        h++;

    // Copy the token (up to CR/LF/space) and compare it to the expected value.
    char token[512];
    size_t i = 0;
    while (h[i] && h[i] != '\r' && h[i] != '\n' && h[i] != ' ' && i < sizeof(token) - 1)
    {
        token[i] = h[i];
        i++;
    }
    token[i] = '\0';
    return ct_equal(token, g_auth_expected);
}

static bool check_client_ip(int client_socket, struct sockaddr_in *client_address)
{
    if (g_allow_all)
        return true;

    char client_ip_address[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &client_address->sin_addr, client_ip_address, INET_ADDRSTRLEN);

    for (int i = 0; i < g_allowed_count; i++)
    {
        if (strcmp(client_ip_address, g_allowed_hosts[i]) == 0)
            return true;
    }

    response_log_prefix();
    printf("HTTP/1.1 403 Forbidden (%s)\n", client_ip_address);
    send_error(client_socket, "403 Forbidden", "");
    return false;
}

// ---------------------------------------------------------------------------
// Server setup and accept loop
// ---------------------------------------------------------------------------

void create_server(const char *ip, int port, int max_connections, thread_pool_t *pool)
{
    printf("Creating socket ...\n");
    server_socket = make_listening_socket(ip, port);
    printf("Socket created!\n");

    init_allowlist();
    init_basic_auth();

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
    free_allowlist();
    free_basic_auth();
    printf("All threads terminated. Database closed.\n");
}
