#ifndef _DATABASE_H
#define _DATABASE_H 1

#include <sqlite3.h>
#include <stdlib.h>
#include <stdio.h>
#include <models.h>

#define SQL_QUERY_SIZE 1024

void create_table();
void get_entries(char *buffer);
void get_entry(unsigned int id, char *buffer);
void create_entry(char struct_string[NUM_COLS][STR_LEN], char *buffer);
void update_entry(unsigned int id, char struct_string[NUM_COLS][STR_LEN], char *buffer);
void delete_entry(unsigned int id, char *buffer);
void open_database();
void close_database();
void check_connection();
void check_version();
void check_sql(char *buffer);
int callback(void *buffer, int argc, char *argv[], char *azColName[]);

#endif