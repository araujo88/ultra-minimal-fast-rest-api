#ifndef _VIEWS_H
#define _VIEWS_H 1

#include <database.h>

void root_view(void *client_socket);
void get_users_view(void *client_socket);
void get_user_view(void *client_socket, unsigned int id);
void delete_user_view(void *client_socket, unsigned int id);
void update_user_view(void *client_socket, unsigned int id, model Model);
void create_user_view(void *client_socket, model Model);
void error_not_found(void *client_socket);

#endif