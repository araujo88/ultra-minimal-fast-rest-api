#define _GNU_SOURCE // strcasestr
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

    response_log_prefix();
    printf("%s %s\n", method, target);

    route_request(fd, method, target, body);

    close(fd);
    free(client_socket);
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
    // environment can set it. Flawfinder: ignore getenv
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

void create_server(char *ip, int port, int max_connections, thread_pool_t *pool)
{
    printf("Creating socket ...\n");
    server_socket = make_listening_socket(ip, port);
    printf("Socket created!\n");

    init_allowlist();

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
