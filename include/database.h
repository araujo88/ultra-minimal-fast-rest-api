#ifndef _DATABASE_H
#define _DATABASE_H 1

#include <sqlite3.h>
#include <stdlib.h>
#include <stdio.h>
#include <models.h>

void create_table_user();
void get_users();
void get_users_string(char *string);
void get_user(unsigned int id, char *string);
void insert_user(user User);
void update_user(unsigned int id, user User);
void delete_user(unsigned int id);

void check_connection();
void get_col_names(char *string);
void open_database();
void close_database();
void check_version();
void check_statement();
void check_sql();
int callback(void *arg, int argc, char *argv[], char *azColName[]);

#endif