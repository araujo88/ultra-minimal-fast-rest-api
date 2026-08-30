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

    get_entries(content, sizeof(content));

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

    get_entry(id, content, sizeof(content));

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

    delete_entry(id, content, sizeof(content));

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

    update_entry(id, struct_string, content, sizeof(content));

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

    create_entry(struct_string, content, sizeof(content));

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
