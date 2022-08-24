#ifndef _VIEWS_H
#define _VIEWS_H 1

#include <database.h>

void root_view(void *client_socket);
void get_users_view(void *client_socket);
void get_user_view(void *client_socket, unsigned int id);
void delete_user_view(void *client_socket, unsigned int id);
void update_user_view(void *client_socket, unsigned int id, char struct_string[NUM_COLS][STR_LEN]);
void create_user_view(void *client_socket, char struct_string[NUM_COLS][STR_LEN]);
void error_not_found(void *client_socket);

#endif