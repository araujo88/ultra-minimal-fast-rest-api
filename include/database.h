#ifndef _DATABASE_H
#define _DATABASE_H 1

#include <sqlite3.h>
#include <stdlib.h>
#include <stdio.h>
#include <models.h>

#define SQL_QUERY_SIZE 1024

// Result codes returned by the CRUD functions below, mapped to HTTP status by
// the views: DB_OK -> 2xx, DB_NOT_FOUND -> 404, DB_ERROR -> 500.
#define DB_OK 0
#define DB_ERROR -1
#define DB_NOT_FOUND 1

void create_table();
int get_entries(char *buffer, size_t cap);
int get_entry(unsigned int id, char *buffer, size_t cap);
int create_entry(char struct_string[NUM_COLS][STR_LEN], char *buffer, size_t cap);
int update_entry(unsigned int id, char struct_string[NUM_COLS][STR_LEN], char *buffer, size_t cap);
int delete_entry(unsigned int id, char *buffer, size_t cap);
void open_database();
void close_database();
void check_connection(int rc);
void check_version();
void check_sql(int rc, char *err, char *buffer, size_t cap);
int callback(void *buffer, int argc, char *argv[], char *azColName[]);

#endif