#include "../include/database.h"
#include <string.h>
#include <time.h>

sqlite3 *db;
int rc;
char *db_err_msg = (char)0;

void create_table()
{
    int i = 0;
    char sql[SQL_QUERY_SIZE];

    sprintf(sql, "DROP TABLE IF EXISTS %s;"
                 "CREATE TABLE %s(Id INTEGER PRIMARY KEY,",
            TABLE_NAME, TABLE_NAME);

    for (i = 0; i < NUM_COLS; i++)
    {
        sprintf(sql + strlen(sql), " %s %s,", TABLE_COLS[i][0], TABLE_COLS[i][1]);
    }

    sprintf(sql + strlen(sql) - 1, ");");

    rc = sqlite3_exec(db, sql, 0, 0, &db_err_msg);

    check_sql(NULL);
}

void get_entries(char *buffer)
{
    char sql[SQL_QUERY_SIZE];

    sprintf(sql, "SELECT * FROM %s;", TABLE_NAME);

    sprintf(buffer + strlen(buffer), "[\n");

    rc = sqlite3_exec(db, sql, callback, buffer, &db_err_msg);

    if (strcmp(buffer, "") == 0)
    {
        sprintf(buffer, "{}");
    }
    else
    {
        sprintf(buffer + strlen(buffer) - 2, "\n]");
    }

    check_sql(NULL);
}

void get_entry(unsigned int id, char *buffer)
{
    char sql[SQL_QUERY_SIZE];

    sprintf(sql, "SELECT * FROM %s WHERE Id = %u;", TABLE_NAME, id);

    rc = sqlite3_exec(db, sql, callback, buffer, &db_err_msg);

    if (strcmp(buffer, "") == 0)
    {
        sprintf(buffer, "{}");
    }
    else
    {
        sprintf(buffer + strlen(buffer) - 2, "\n");
    }

    check_sql(NULL);
}

void create_entry(char struct_string[NUM_COLS][STR_LEN], char *buffer)
{
    int i = 0;
    char sql[SQL_QUERY_SIZE];

    sprintf(sql, "INSERT INTO %s (", TABLE_NAME);
    for (i = 0; i < NUM_COLS; i++)
    {
        sprintf(sql + strlen(sql), "%s, ", TABLE_COLS[i][0]);
    }
    sprintf(sql + strlen(sql) - 2, ") VALUES (");
    for (i = 0; i < NUM_COLS; i++)
    {
        sprintf(sql + strlen(sql), "'%s', ", struct_string[i]);
    }
    sprintf(sql + strlen(sql) - 2, ");");

    rc = sqlite3_exec(db, sql, 0, 0, &db_err_msg);

    check_sql(buffer);
}

void update_entry(unsigned int id, char struct_string[NUM_COLS][STR_LEN], char *buffer)
{
    int i;
    char sql[SQL_QUERY_SIZE];

    sprintf(sql, "UPDATE %s SET", TABLE_NAME);
    for (i = 0; i < NUM_COLS; i++)
    {
        sprintf(sql + strlen(sql), " %s = '%s',", TABLE_COLS[i][0], struct_string[i]);
    }
    sprintf(sql + strlen(sql) - 1, " WHERE Id = %u;", id);

    rc = sqlite3_exec(db, sql, 0, 0, &db_err_msg);

    check_sql(buffer);
}

void delete_entry(unsigned int id, char *buffer)
{
    char sql[SQL_QUERY_SIZE];

    sprintf(sql, "DELETE FROM %s WHERE Id = %u;", TABLE_NAME, id);

    rc = sqlite3_exec(db, sql, 0, 0, &db_err_msg);

    check_sql(buffer);
}

int callback(void *buffer, int argc, char *argv[], char *azColName[])
{
    sprintf(buffer + strlen(buffer), "{\n");
    for (int i = 0; i < argc; i++)
    {
        if ((strcmp(azColName[i], "Id") == 0) || (strcmp(TABLE_COLS[1][i], "INTEGER") == 0))
        {
            sprintf(buffer + strlen(buffer), "\t\"%s\": %s,\n", azColName[i], argv[i] ? argv[i] : "NULL");
        }
        else
        {
            sprintf(buffer + strlen(buffer), "\t\"%s\": \"%s\",\n", azColName[i], argv[i] ? argv[i] : "NULL");
        }
    }
    sprintf(buffer + strlen(buffer) - 2, "},\n");

    return 0;
}

void open_database()
{
    rc = sqlite3_open("sqlite3.db", &db);
    check_connection(rc);
}

void check_version()
{
    sqlite3_stmt *res;

    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    rc = sqlite3_prepare_v2(db, "SELECT SQLITE_VERSION()", -1, &res, 0);

    check_sql(NULL);

    rc = sqlite3_step(res);

    if (rc == SQLITE_ROW)
    {
        printf("[%s] - ", current_date);
        printf("\033[0;33mSQLite version %s\n\033[0m", sqlite3_column_text(res, 0));
    }

    sqlite3_finalize(res);
}

void close_database()
{
    sqlite3_close(db);
}

void check_connection()
{
    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    if (rc != SQLITE_OK)
    {
        printf("[%s] - ", current_date);
        fprintf(stderr, "\033[0;33mCannot open database: %s\n\033[0m", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(EXIT_FAILURE);
    }
}

void check_sql(char *buffer)
{
    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;
    printf("[%s] - ", current_date);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "\033[0;33mSQL error: %s\n\033[0m", db_err_msg);
        if (buffer != NULL)
        {
            sprintf(buffer, "{\"msg\": \"error\"}");
        }
    }
    else
    {
        fprintf(stdout, "\033[0;33mSQL query executed successfuly\n\033[0m");
        if (buffer != NULL)
        {
            sprintf(buffer, "{\"msg\": \"success\"}");
        }
    }
}