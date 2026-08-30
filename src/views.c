#include "../include/views.h"
#include "../include/server.h"

// Send the whole buffer, tolerating partial writes and EINTR. A short send()
// would otherwise silently truncate a response.
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
            return; // client-local failure
        }
        sent += (size_t)n;
    }
}

// Emit a small fixed response (used for the fail-closed 500 when a read result
// does not fit the response buffer). Body is short and known, so it always fits.
static void send_status(int fd, const char *status, const char *ctype, const char *body)
{
    char msg[BUFFER_SIZE];
    int n = snprintf(msg, sizeof(msg),
                     "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n\r\n%s",
                     status, ctype, strlen(body), body);
    if (n > 0)
        send_all(fd, msg, (size_t)n < sizeof(msg) ? (size_t)n : sizeof(msg));
    printf("HTTP/1.1 %s\n", status);
}

// Log a timestamp and emit a small JSON status response (404/500 paths).
static void respond_json(void *client_socket, const char *status, const char *body)
{
    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;
    printf("[%s] - ", current_date);
    send_status(*(int *)client_socket, status, "application/json", body);
}

void root_view(void *client_socket)
{
    char server_message[BUFFER_SIZE] = {0};
    char *content;
    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;
    content = "Hello world!";
    printf("[%s] - ", current_date);
    printf("\033[0;32mHTTP/1.1 200 OK\033[0m\n");
    snprintf(server_message, sizeof(server_message), "HTTP/1.1 200 OK\r\nDate: %s\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n%s", current_date, strlen(content), content);
    send_all(*(int *)client_socket, server_message, strlen(server_message));
}

void get_users_view(void *client_socket)
{
    char server_message[BUFFER_SIZE] = {0};
    char content[BUFFER_SIZE / 2] = "";
    char *current_date;

    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    if (get_entries(content, sizeof(content)) != 0)
    {
        printf("[%s] - ", current_date);
        printf("\033[0;31mHTTP/1.1 500 Internal Server Error\033[0m\n");
        send_status(*(int *)client_socket, "500 Internal Server Error",
                    "application/json", "{\"msg\": \"response too large\"}");
        return;
    }

    printf("[%s] - ", current_date);
    printf("\033[0;32mHTTP/1.1 200 OK\033[0m\n");
    snprintf(server_message, sizeof(server_message), "HTTP/1.1 200 OK\r\nDate: %s\r\nContent-Type: application/json\r\nContent-Length: %ld\r\n\r\n%s", current_date, strlen(content), content);
    send_all(*(int *)client_socket, server_message, strlen(server_message));
}

void get_user_view(void *client_socket, unsigned int id)
{
    char server_message[BUFFER_SIZE] = {0};
    char content[BUFFER_SIZE / 2] = "";
    char *current_date;

    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    int rc = get_entry(id, content, sizeof(content));
    if (rc == DB_NOT_FOUND)
    {
        respond_json(client_socket, "404 Not Found", "{\"msg\": \"not found\"}");
        return;
    }
    if (rc != DB_OK)
    {
        // DB_ERROR here is a prepare/step failure (a single row cannot overflow
        // the buffer), so report a neutral error rather than "too large".
        respond_json(client_socket, "500 Internal Server Error", "{\"msg\": \"error\"}");
        return;
    }

    printf("[%s] - ", current_date);
    printf("\033[0;32mHTTP/1.1 200 OK\033[0m\n");
    snprintf(server_message, sizeof(server_message), "HTTP/1.1 200 OK\r\nDate: %s\r\nContent-Type: application/json\r\nContent-Length: %ld\r\n\r\n%s", current_date, strlen(content), content);
    send_all(*(int *)client_socket, server_message, strlen(server_message));
}

void delete_user_view(void *client_socket, unsigned int id)
{
    char server_message[BUFFER_SIZE] = {0};
    char content[BUFFER_SIZE / 2] = "";
    char *current_date;

    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    int rc = delete_entry(id, content, sizeof(content));
    if (rc == DB_NOT_FOUND)
    {
        respond_json(client_socket, "404 Not Found", "{\"msg\": \"not found\"}");
        return;
    }
    if (rc != DB_OK)
    {
        respond_json(client_socket, "500 Internal Server Error", "{\"msg\": \"error\"}");
        return;
    }

    printf("[%s] - ", current_date);
    printf("\033[0;32mHTTP/1.1 200 OK\033[0m\n");
    snprintf(server_message, sizeof(server_message), "HTTP/1.1 200 OK\r\nDate: %s\r\nContent-Type: application/json\r\nContent-Length: %ld\r\n\r\n%s", current_date, strlen(content), content);
    send_all(*(int *)client_socket, server_message, strlen(server_message));
}

void update_user_view(void *client_socket, unsigned int id, char struct_string[NUM_COLS][STR_LEN])
{
    char server_message[BUFFER_SIZE] = {0};
    char content[BUFFER_SIZE / 2] = "";
    char *current_date;

    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    int rc = update_entry(id, struct_string, content, sizeof(content));
    if (rc == DB_NOT_FOUND)
    {
        respond_json(client_socket, "404 Not Found", "{\"msg\": \"not found\"}");
        return;
    }
    if (rc != DB_OK)
    {
        respond_json(client_socket, "500 Internal Server Error", "{\"msg\": \"error\"}");
        return;
    }

    printf("[%s] - ", current_date);
    printf("\033[0;32mHTTP/1.1 200 OK\033[0m\n");
    snprintf(server_message, sizeof(server_message), "HTTP/1.1 200 OK\r\nDate: %s\r\nContent-Type: application/json\r\nContent-Length: %ld\r\n\r\n%s", current_date, strlen(content), content);
    send_all(*(int *)client_socket, server_message, strlen(server_message));
}

void create_user_view(void *client_socket, char struct_string[NUM_COLS][STR_LEN])
{
    char server_message[BUFFER_SIZE] = {0};
    char content[BUFFER_SIZE / 2] = "";
    char *current_date;

    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    if (create_entry(struct_string, content, sizeof(content)) != DB_OK)
    {
        respond_json(client_socket, "500 Internal Server Error", "{\"msg\": \"error\"}");
        return;
    }

    printf("[%s] - ", current_date);
    printf("\033[0;32mHTTP/1.1 201 Created\033[0m\n");
    snprintf(server_message, sizeof(server_message), "HTTP/1.1 201 Created\r\nDate: %s\r\nContent-Type: application/json\r\nContent-Length: %ld\r\n\r\n%s", current_date, strlen(content), content);
    send_all(*(int *)client_socket, server_message, strlen(server_message));
}

void error_not_found(void *client_socket)
{
    char server_message[BUFFER_SIZE] = {0};
    char *content;
    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;
    content = "<html><h1>Error 404 - not found</h1></html>";
    printf("[%s] - ", current_date);
    printf("\033[0;31mHTTP/1.1 404 Not Found\033[0m\n");
    snprintf(server_message, sizeof(server_message), "HTTP/1.1 404 Not Found\r\nDate: %s\r\nContent-Type: text/html\r\nContent-Length: %ld\r\n\r\n%s", current_date, strlen(content), content);
    send_all(*(int *)client_socket, server_message, strlen(server_message));
}
