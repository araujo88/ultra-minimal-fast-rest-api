#include "../include/views.h"
#include "../include/server.h"
#include "../include/response.h"

#define GREEN "\033[0;32m"
#define RED "\033[0;31m"

// Log the request-time prefix and a coloured status line to the console.
static void log_status(const char *color, const char *status)
{
    response_log_prefix();
    printf("%sHTTP/1.1 %s\033[0m\n", color, status);
}

// Liveness: the process is up and answering. No dependency checks.
void livez_view(void *client_socket)
{
    int fd = *(int *)client_socket;
    log_status(GREEN, "200 OK");
    response_send(fd, "200 OK", "application/json", "{\"status\": \"ok\"}");
}

// Readiness / health: 200 only if the database answers, else 503.
void health_view(void *client_socket)
{
    int fd = *(int *)client_socket;
    if (db_ok())
    {
        log_status(GREEN, "200 OK");
        response_send(fd, "200 OK", "application/json", "{\"status\": \"ok\"}");
    }
    else
    {
        log_status(RED, "503 Service Unavailable");
        response_send(fd, "503 Service Unavailable", "application/json",
                      "{\"status\": \"unavailable\"}");
    }
}

void get_users_view(void *client_socket)
{
    int fd = *(int *)client_socket;
    char content[BUFFER_SIZE / 2] = "";

    if (get_entries(content, sizeof(content)) != DB_OK)
    {
        log_status(RED, "500 Internal Server Error");
        response_send(fd, "500 Internal Server Error", "application/json",
                      "{\"msg\": \"response too large\"}");
        return;
    }

    log_status(GREEN, "200 OK");
    response_send(fd, "200 OK", "application/json", content);
}

void get_user_view(void *client_socket, unsigned int id)
{
    int fd = *(int *)client_socket;
    char content[BUFFER_SIZE / 2] = "";

    int rc = get_entry(id, content, sizeof(content));
    if (rc == DB_NOT_FOUND)
    {
        log_status(RED, "404 Not Found");
        response_send(fd, "404 Not Found", "application/json", "{\"msg\": \"not found\"}");
        return;
    }
    if (rc != DB_OK)
    {
        // A single row cannot overflow the buffer, so DB_ERROR here is a real
        // prepare/step failure -- report a neutral error, not "too large".
        log_status(RED, "500 Internal Server Error");
        response_send(fd, "500 Internal Server Error", "application/json", "{\"msg\": \"error\"}");
        return;
    }

    log_status(GREEN, "200 OK");
    response_send(fd, "200 OK", "application/json", content);
}

void delete_user_view(void *client_socket, unsigned int id)
{
    int fd = *(int *)client_socket;

    int rc = delete_entry(id, NULL, 0); // 204 on success carries no body
    if (rc == DB_NOT_FOUND)
    {
        log_status(RED, "404 Not Found");
        response_send(fd, "404 Not Found", "application/json", "{\"msg\": \"not found\"}");
        return;
    }
    if (rc != DB_OK)
    {
        log_status(RED, "500 Internal Server Error");
        response_send(fd, "500 Internal Server Error", "application/json", "{\"msg\": \"error\"}");
        return;
    }

    log_status(GREEN, "204 No Content");
    response_send_no_content(fd);
}

void update_user_view(void *client_socket, unsigned int id, char struct_string[NUM_COLS][STR_LEN])
{
    int fd = *(int *)client_socket;
    char content[BUFFER_SIZE / 2] = "";

    int rc = update_entry(id, struct_string, content, sizeof(content));
    if (rc == DB_NOT_FOUND)
    {
        log_status(RED, "404 Not Found");
        response_send(fd, "404 Not Found", "application/json", "{\"msg\": \"not found\"}");
        return;
    }
    if (rc != DB_OK)
    {
        log_status(RED, "500 Internal Server Error");
        response_send(fd, "500 Internal Server Error", "application/json", "{\"msg\": \"error\"}");
        return;
    }

    log_status(GREEN, "200 OK");
    response_send(fd, "200 OK", "application/json", content);
}

void create_user_view(void *client_socket, char struct_string[NUM_COLS][STR_LEN])
{
    int fd = *(int *)client_socket;
    char content[BUFFER_SIZE / 2] = "";

    if (create_entry(struct_string, content, sizeof(content)) != DB_OK)
    {
        log_status(RED, "500 Internal Server Error");
        response_send(fd, "500 Internal Server Error", "application/json", "{\"msg\": \"error\"}");
        return;
    }

    log_status(GREEN, "201 Created");
    response_send(fd, "201 Created", "application/json", content);
}

void error_not_found(void *client_socket)
{
    int fd = *(int *)client_socket;
    log_status(RED, "404 Not Found");
    response_send(fd, "404 Not Found", "text/html", "<html><h1>Error 404 - not found</h1></html>");
}
