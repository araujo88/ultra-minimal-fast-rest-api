#include "../include/views.h"
#include "../include/server.h"

void root(void *client_socket)
{
    char server_message[BUFFER_SIZE];
    char *content;
    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;
    content = "<html><body><h1>Hello from the server!</h1></body></html>";
    printf("\033[0;32mHTTP/1.0 200 OK\033[0m\n");
    sprintf(server_message, "HTTP/1.0 200 OK\nDate: %s\nContent-Type: text/html\nContent-Length: %ld\n\n%s", current_date, strlen(content), content);
    send(*(int *)client_socket, &server_message, sizeof(server_message), 0);
    memset(server_message, 0, sizeof(server_message));
}

void users(void *client_socket)
{
    char server_message[BUFFER_SIZE];
    char *content;
    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;
    content = "<html><body><h1>User list</h1><p> - John doe</p><p> - Mary Smith</p><p> - Billy Bob</p></body></html>";
    printf("\033[0;32mHTTP/1.0 200 OK\033[0m\n");
    sprintf(server_message, "HTTP/1.0 200 OK\nDate: %s\nContent-Type: text/html\nContent-Length: %ld\n\n%s", current_date, strlen(content), content);
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
    printf("\033[0;31mHTTP/1.0 404 Not Found\033[0m\n");
    sprintf(server_message, "HTTP/1.0 404 Not Found\nDate: %s\nContent-Type: text/html\nContent-Length: %ld\n\n%s", current_date, strlen(content), content);
    send(*(int *)client_socket, &server_message, sizeof(server_message), 0);
    memset(server_message, 0, sizeof(server_message));

}