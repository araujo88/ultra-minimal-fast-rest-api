#include "../include/views.h"
#include "../include/server.h"

void root_view(void *client_socket)
{
    char server_message[BUFFER_SIZE];
    char *content;
    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;
    content = "Hello world!";
    printf("[%s] - ", current_date);
    printf("\033[0;32mHTTP/1.0 200 OK\033[0m\n");
    sprintf(server_message, "HTTP/1.0 200 OK\nDate: %s\nContent-Type: text/html\nContent-Length: %ld\n\n%s", current_date, strlen(content), content);
    send(*(int *)client_socket, &server_message, sizeof(server_message), 0);
    memset(server_message, 0, sizeof(server_message));
}

void get_users_view(void *client_socket)
{
    char server_message[BUFFER_SIZE];
    char content[BUFFER_SIZE / 2] = "";
    char *current_date;

    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    get_entries(content + strlen(content));

    printf("[%s] - ", current_date);
    printf("\033[0;32mHTTP/1.0 200 OK\033[0m\n");
    sprintf(server_message, "HTTP/1.0 200 OK\nDate: %s\nContent-Type: application/json\nContent-Length: %ld\n\n%s", current_date, strlen(content), content);
    send(*(int *)client_socket, &server_message, sizeof(server_message), 0);
    memset(server_message, 0, sizeof(server_message));
}

void get_user_view(void *client_socket, unsigned int id)
{
    char server_message[BUFFER_SIZE];
    char content[BUFFER_SIZE / 2] = "";
    char *current_date;

    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    get_entry(id, content);

    printf("[%s] - ", current_date);
    printf("\033[0;32mHTTP/1.1 200 OK\033[0m\n");
    sprintf(server_message, "HTTP/1.1 200 OK\nDate: %s\nContent-Type: application/json\nContent-Length: %ld\n\n%s", current_date, strlen(content), content);
    send(*(int *)client_socket, &server_message, sizeof(server_message), 0);
    memset(server_message, 0, sizeof(server_message));
}

void delete_user_view(void *client_socket, unsigned int id)
{
    char server_message[BUFFER_SIZE];
    char content[BUFFER_SIZE / 2] = "";
    char *current_date;

    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    delete_entry(id, content);

    printf("[%s] - ", current_date);
    printf("\033[0;32mHTTP/1.1 200 OK\033[0m\n");
    sprintf(server_message, "HTTP/1.1 200 OK\nDate: %s\nContent-Type: application/json\nContent-Length: %ld\n\n%s", current_date, strlen(content), content);
    send(*(int *)client_socket, &server_message, sizeof(server_message), 0);
    memset(server_message, 0, sizeof(server_message));
}

void update_user_view(void *client_socket, unsigned int id, model Model)
{
    char struct_string[NUM_COLS][STR_LEN];
    char server_message[BUFFER_SIZE];
    char content[BUFFER_SIZE / 2] = "";
    char *current_date;

    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    struct_to_string_array(&Model, struct_string);
    update_entry(id, struct_string, content);

    printf("[%s] - ", current_date);
    printf("\033[0;32mHTTP/1.1 200 OK\033[0m\n");
    sprintf(server_message, "HTTP/1.1 200 OK\nDate: %s\nContent-Type: application/json\nContent-Length: %ld\n\n%s", current_date, strlen(content), content);
    send(*(int *)client_socket, &server_message, sizeof(server_message), 0);
    memset(server_message, 0, sizeof(server_message));
}

void create_user_view(void *client_socket, model Model)
{
    char struct_string[NUM_COLS][STR_LEN];
    char server_message[BUFFER_SIZE] = "";
    char content[BUFFER_SIZE / 2];
    char *current_date;

    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    struct_to_string_array(&Model, struct_string);
    create_entry(struct_string, content);

    printf("[%s] - ", current_date);
    printf("\033[0;32mHTTP/1.1 201 Created\033[0m\n");
    sprintf(server_message, "HTTP/1.1 201 Created\nDate: %s\nContent-Type: application/json\nContent-Length: %ld\n\n%s", current_date, strlen(content), content);
    send(*(int *)client_socket, &server_message, sizeof(server_message), 0);
    memset(server_message, 0, sizeof(server_message));
}

void error_not_found(void *client_socket)
{
    char server_message[BUFFER_SIZE];
    char *content;
    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;
    content = "<html><h1>Error 404 - not found</h1></html>";
    printf("[%s] - ", current_date);
    printf("\033[0;31mHTTP/1.0 404 Not Found\033[0m\n");
    sprintf(server_message, "HTTP/1.0 404 Not Found\nDate: %s\nContent-Type: text/html\nContent-Length: %ld\n\n%s", current_date, strlen(content), content);
    send(*(int *)client_socket, &server_message, sizeof(server_message), 0);
    memset(server_message, 0, sizeof(server_message));
}