#include "../include/database.h"
#include "../include/models.h"
#include <string.h>
#include <time.h>

sqlite3 *db;
int rc;
char *db_err_msg = (char)0;

void create_table_user()
{
    char sql[SQL_QUERY_SIZE];

    sprintf(sql, "DROP TABLE IF EXISTS %s;"
                 "CREATE TABLE %s(Id INTEGER PRIMARY KEY, Name TEXT, Surname TEXT);",
            TABLE_NAME, TABLE_NAME);

    rc = sqlite3_exec(db, sql, 0, 0, &db_err_msg);

    check_sql(NULL);
}

void get_users(char *buffer)
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

void get_user(unsigned int id, char *buffer)
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

void create_user(user User, char *buffer)
{
    char sql[SQL_QUERY_SIZE];

    sprintf(sql, "INSERT INTO %s (Name, Surname) VALUES ('%s', '%s');", TABLE_NAME, User.name, User.surname);

    rc = sqlite3_exec(db, sql, 0, 0, &db_err_msg);

    check_sql(buffer);
}

void update_user(unsigned int id, user User, char *buffer)
{
    char sql[SQL_QUERY_SIZE];

    sprintf(sql, "UPDATE %s SET Name = '%s', Surname = '%s' WHERE Id = %u;", TABLE_NAME, User.name, User.surname, id);

    rc = sqlite3_exec(db, sql, 0, 0, &db_err_msg);

    check_sql(buffer);
}

void delete_user(unsigned int id, char *buffer)
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
        if (strcmp(azColName[i], "Id") == 0)
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