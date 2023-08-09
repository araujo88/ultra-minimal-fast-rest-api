#include "../include/server.h"
#include "../include/views.h"
#include "../include/routes.h"
#include "../include/database.h"
#include "../include/settings.h"
#include "../include/models.h"

char url_get_root[URL_MAX_LEN];
char url_get_entries[URL_MAX_LEN];
char url_get_entry[URL_MAX_LEN];
char url_post_entry[URL_MAX_LEN];
char url_put_entry[URL_MAX_LEN];
char url_delete_entry[URL_MAX_LEN];

char *routes[NUM_ROUTES] = {url_get_root, url_get_entries, url_get_entry, url_put_entry, url_delete_entry, url_post_entry};

void create_server(int server_socket, char *ip, int port, int max_connections, thread_pool_t *pool)
{
    printf("Creating socket ...\n");
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    check_socket(server_socket);
    printf("Socket created!\n");

    struct sockaddr_in server_address;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(port);
    server_address.sin_addr.s_addr = inet_addr(ip);

    printf("Binding socket ...\n");
    check_bind(server_socket, &server_address);
    printf("Binding done!\n");

    printf("Initializing database connection...\n");
    open_database();
    check_version();

    create_table();

    generate_routes();

    // --------------- create dummy database --------------- //

    // char user1[NUM_COLS][STR_LEN] = {"John", "Doe", "30", "1.75"};
    // create_entry(user1, NULL);
    // char user2[NUM_COLS][STR_LEN] = {"Billy", "Bob", "31", "1.60"};
    // create_entry(user2, NULL);

    // --------------- end creating dummy database --------------- //

    printf("Waiting for incoming requests... (press Ctrl+C to quit)\n");
    while (true)
    {
        check_listen(server_socket, max_connections);

        int *client_socket = malloc(sizeof(int));
        struct sockaddr_in *client_address = NULL;

        check_accept(server_socket, client_socket, (struct sockaddr *)client_address);

        if (check_client_ip(client_socket, (struct sockaddr *)client_address))
        {
            thread_pool_add_task(pool, (void *)send_data, (void *)client_socket);
        }
    }
}

void *send_data(void *client_socket)
{
    int i;
    char request[BUFFER_SIZE] = {0};
    char content[BUFFER_SIZE] = {0};

    get_request(*(int *)client_socket, request, content);

    for (i = 0; i < NUM_ROUTES; i++)
    {
        if (strstr(request, routes[i]) != NULL)
        {
            if (strstr(request, url_get_root) != NULL)
            {
                root_view(client_socket);
            }
            else if (strstr(request, url_get_entry) != NULL)
            {
                int i = 0;
                char id[256] = {0};
                char *tmp;
                strcpy(request, strstr(request, url_get_entry));
                tmp = request;
                tmp = tmp + strlen(url_get_entry);
                while (tmp[i] != 'H')
                {
                    id[i] = tmp[i];
                    i++;
                }
                id[i] = '\0';
                get_user_view(client_socket, atoi(id));
            }
            else if (strstr(request, url_post_entry) != NULL)
            {
                int i;
                char field[512];
                char model_string[NUM_COLS][STR_LEN];

                sprintf(field, "%s=", TABLE_COLS[0][0]);
                const char *temp = strstr(request, url_post_entry);
                if (temp)
                {
                    memmove(request, temp, strlen(temp) + 1);
                }
                else
                {
                    // Handle the error, such as logging it or returning an error code
                }

                temp = strstr(content, field);
                if (temp)
                {
                    memmove(content, temp, strlen(temp) + 1);
                }
                else
                {
                    // Handle the error
                }

                char *token = strtok(content, "=&");

                while (token != NULL)
                {
                    for (i = 0; i < NUM_COLS; i++)
                    {
                        if (strcmp(token, TABLE_COLS[i][0]) == 0)
                        {
                            token = strtok(NULL, "=&");
                            strcpy(model_string[i], token);
                        }
                    }
                    token = strtok(NULL, "=&");
                }
                create_user_view(client_socket, model_string);
            }
            else if (strstr(request, url_put_entry) != NULL)
            {
                int i = 0;
                char id[256] = {0};
                char *tmp;
                char field[512] = {0};
                char model_string[NUM_COLS][STR_LEN];

                sprintf(field, "%s=", TABLE_COLS[0][0]);
                strcpy(request, strstr(request, url_put_entry));
                tmp = request;
                tmp = tmp + strlen(url_put_entry);
                while (tmp[i] != 'H')
                {
                    id[i] = tmp[i];
                    i++;
                }
                id[i] = '\0';
                strcpy(content, strstr(content, field));

                char *token = strtok(content, "=&");

                while (token != NULL)
                {
                    for (i = 0; i < NUM_COLS; i++)
                    {
                        if (strcmp(token, TABLE_COLS[i][0]) == 0)
                        {
                            token = strtok(NULL, "=&");
                            strcpy(model_string[i], token);
                        }
                    }
                    token = strtok(NULL, "=&");
                }
                update_user_view(client_socket, atoi(id), model_string);
            }
            else if (strstr(request, url_delete_entry) != NULL)
            {
                int i = 0;
                char id[256] = {0};
                char *tmp;
                strcpy(request, strstr(request, url_delete_entry));
                tmp = request;
                tmp = tmp + strlen(url_delete_entry);
                while (tmp[i] != 'H')
                {
                    id[i] = tmp[i];
                    i++;
                }
                id[i] = '\0';
                delete_user_view(client_socket, atoi(id));
            }
            else if (strstr(request, url_get_entries) != NULL)
            {
                get_users_view(client_socket);
            }
        }
    }
    error_not_found(client_socket);
    close(*(int *)client_socket);
    free(client_socket);
}

void get_request(int client_socket, char *request, char *content)
{
    int i = 0;
    char client_message[BUFFER_SIZE];

    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    if ((recv(client_socket, &client_message, sizeof(client_message), 0)) < 0)
    {
        perror("Receive error:");
        printf("Error code: %d\n", errno);
        exit(1);
    }

    while (client_message[i] != '\n')
    {
        strncat(request, &client_message[i], 1);
        i++;
    }

    if (strstr(client_message, "Content-Type: application/x-www-form-urlencoded") != NULL)
    {
        strcpy(content, strstr(client_message, "Content-Type: application/x-www-form-urlencoded"));
    }

    printf("[%s] - ", current_date);
    printf("%s\n", request);
}

bool check_client_ip(int *client_socket, struct sockaddr *client_address)
{
    int i;
    struct sockaddr_in *pV4Addr = (struct sockaddr_in *)&client_address;
    struct in_addr ipAddr = pV4Addr->sin_addr;
    char client_ip_address[INET_ADDRSTRLEN];
    char server_message[BUFFER_SIZE];

    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    inet_ntop(AF_INET, &ipAddr, client_ip_address, INET_ADDRSTRLEN);

    for (i = 0; i < NUM_ALLOWED_HOSTS; i++)
    {
        if (strcmp(client_ip_address, ALLOWED_HOSTS[i]) == 0)
        {
            return true;
        }
    }

    printf("[%s] - ", current_date);
    printf("HTTP/1.1 403 Forbidden\n");

    sprintf(server_message, "HTTP/1.1 403 Forbidden\nDate: %s\nContent-Type: text/html\nContent-Length: 0\n\n", current_date);
    send(*(int *)client_socket, &server_message, sizeof(server_message), 0);

    memset(server_message, 0, sizeof(server_message));

    return false;
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
