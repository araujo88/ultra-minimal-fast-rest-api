#include "../include/server.h"
#include "../include/views.h"
#include "../include/routes.h"
#include "../include/database.h"

void create_server(int server_socket, char *ip, int port, int max_connections)
{
    printf("Creating socket ...\n");
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    check_socket(server_socket);
    printf("Socket created!\n");

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr("127.0.0.1");

    printf("Binding socket ...\n");
    check_bind(server_socket, &server_address);
    printf("Binding done!\n");

    printf("Initializing database connection...\n");
    open_database();
    check_version();

    printf("Creating tables...\n");
    create_table_user();

    printf("Waiting for incoming requests... (press Ctrl+C to quit)\n");
    while (true)
    {
        check_listen(server_socket, max_connections);

        int *client_socket = malloc(sizeof(int));
        struct sockaddr_in *client_address = NULL;

        check_accept(server_socket, client_socket, (struct sockaddr *)client_address);
        
        pthread_t t;
        pthread_create(&t, NULL, send_data, (void *)client_socket);
    }
}

void check_socket(int server_socket)
{
    if (server_socket < 0)
    {
        perror("Socket failed: ");
        printf("Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

void check_bind(int server_socket, struct sockaddr_in *server_address)
{
    if ((bind(server_socket, (struct sockaddr *)server_address, sizeof(*server_address))) < 0)
    {
        perror("Bind failed");
        printf("Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

void check_listen(int server_socket, int num_connections)
{
    if ((listen(server_socket, num_connections)) < 0)
    {
        perror("Listen failed");
        printf("Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

void check_accept(int server_socket, int *client_socket, struct sockaddr *client_address)
{
    if ((*client_socket = accept(server_socket, (struct sockaddr *)client_address, (socklen_t *)sizeof(client_address))) < 0)
    {
        perror("Accept failed");
        printf("Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }
}

void *send_data(void *client_socket)
{
    int i;
    char request[BUFFER_SIZE];
    pthread_t self = pthread_self();

    get_request(*(int *)client_socket, request);

    for (i = 0; i < NUM_ROUTES; i++) {
        if (strstr(request, routes[i]) != NULL) {
            if (strstr(request, "GET / ") != NULL) {
                root(client_socket);
                close(*(int *)client_socket);
                free(client_socket);
                pthread_exit(&self);
            }
            else if (strstr(request, "GET /users ") != NULL) {
                users(client_socket);
                close(*(int *)client_socket);
                free(client_socket);
                pthread_exit(&self);
            }
        }
    }
    error_not_found(client_socket);
    pthread_exit(&self);
}

void get_request(int client_socket, char *request)
{
    int i = 0;
    char client_message[BUFFER_SIZE];

    memset(client_message, 0, sizeof(client_message));

    if ((recv(client_socket, &client_message, sizeof(client_message), 0)) < 0)
    {
        perror("Receive error:");
        printf("Error code: %d\n", errno);
        exit(1);
    }

    while (client_message[i] != '\n') {
        strncat(request, &client_message[i], 1);
        i++;
    }

    printf("%s\n", request);
    memset(client_message, 0, sizeof(client_message));
}