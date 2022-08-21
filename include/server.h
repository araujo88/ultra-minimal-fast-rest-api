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

#define BUFFER_SIZE 1024

void check_socket(int server_socket);                                                      // check socket creation
void check_bind(int server_socket, struct sockaddr_in *server_address);                    // check binding
void check_listen(int server_socket, int number_connections);                              // check connection listening
void check_accept(int server_socket, int *client_socket, struct sockaddr *client_address); // check accepting connection
void *send_data(void *client_socket);                                                      // sends data
void get_request(int client_socket, char *request, char *content);                         // displays client request
void create_server(int server_socket, char *ip, int port, int max_connections);
bool check_client_ip(int *client_socket, struct sockaddr *client_address);

#endif