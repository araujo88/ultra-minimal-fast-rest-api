#include "../include/server.h"
#include "../include/database.h"
#include "../include/threadpool.h"

int server_socket;
thread_pool_t *pool;

void handle_signal(int sig);

int main(int argc, char *argv[])
{
    pool = thread_pool_create(8, 8);
    setvbuf(stdout, NULL, _IONBF, 0);
    signal(SIGINT, handle_signal);
    create_server(server_socket, "0.0.0.0", 9002, 10, pool);
    return 0;
}

void handle_signal(int sig)
{
    printf("\nCaught interrupt signal %d\n", sig);
    printf("Closing database ...\n");
    close_database();
    printf("Database connection closed!\n");
    printf("Closing socket ...\n");
    if (close(server_socket) == 0)
    {
        printf("Socket closed!\n");
    }
    else
    {
        perror("An error occurred while closing the socket: ");
        printf("Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }
    printf("Thread pool cleanup ...\n");
    thread_pool_cleanup(pool);
    printf("All threads terminated.\n");
    exit(EXIT_SUCCESS);
}