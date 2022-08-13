#ifndef _DATABASE_H
#define _DATABASE_H 1

#include <sqlite3.h>
#include <stdlib.h>
#include <stdio.h>

void check_connection();
void check_query();
void open_database();
void close_database();
void check_version();
void check_statement();
void create_table_user();

#endif