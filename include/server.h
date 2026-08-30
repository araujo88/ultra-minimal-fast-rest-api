#ifndef _SERVER_H
#define _SERVER_H 1

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>  // close socket
#include <errno.h>   // error codes
#include <stdbool.h> // boolean types
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <signal.h>  // for interrupt signal handler
#include <pthread.h> // POSIX threads
#include "threadpool.h"

#define BUFFER_SIZE 8192

// Set to 0 by the SIGINT handler; the accept loop notices this and shuts down
// cleanly from normal thread context (no work is done in the signal handler).
extern volatile sig_atomic_t server_running;

void send_data(void *client_socket);
void create_server(char *ip, int port, int max_connections, thread_pool_t *pool);

#endif
