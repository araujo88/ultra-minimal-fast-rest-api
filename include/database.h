#ifndef _DATABASE_H
#define _DATABASE_H 1

#include <sqlite3.h>
#include <stdlib.h>
#include <stdio.h>
#include <models.h>

#define SQL_QUERY_SIZE 1024

void create_table();
void get_entries(char *buffer, size_t cap);
void get_entry(unsigned int id, char *buffer, size_t cap);
void create_entry(char struct_string[NUM_COLS][STR_LEN], char *buffer, size_t cap);
void update_entry(unsigned int id, char struct_string[NUM_COLS][STR_LEN], char *buffer, size_t cap);
void delete_entry(unsigned int id, char *buffer, size_t cap);
void open_database();
void close_database();
void check_connection(int rc);
void check_version();
void check_sql(int rc, char *err, char *buffer, size_t cap);
int callback(void *buffer, int argc, char *argv[], char *azColName[]);

#endif