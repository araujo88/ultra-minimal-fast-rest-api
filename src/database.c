#include "../include/database.h"
#include "../include/models.h"
#include <string.h>

sqlite3 *db;
int rc;
char *db_err_msg = (char)0;

void create_table_user()
{
    // SQL statement
    char *sql = "DROP TABLE IF EXISTS Users;"
                "CREATE TABLE Users(Id INTEGER PRIMARY KEY, Name TEXT, Surname TEXT);";

    // wrapper for qlite3_prepare_v2(), sqlite3_step(), and sqlite3_finalize()
    rc = sqlite3_exec(db, sql, 0, 0, &db_err_msg);

    check_sql(rc, db, db_err_msg);
}

void get_users()
{
    char *sql = "SELECT * FROM Users;";

    rc = sqlite3_exec(db, sql, callback, 0, &db_err_msg);

    check_sql(rc, db, db_err_msg);
}

void get_users_string(char *string)
{
    char *sql = "SELECT * FROM Users;";

    rc = sqlite3_exec(db, sql, callback, string, &db_err_msg);

    check_sql(rc, db, db_err_msg);
}

void get_col_names(char *string)
{
    sqlite3_stmt *stmt;

    rc = sqlite3_prepare_v2(db, "pragma table_info (Users)", -1, &stmt, NULL);

    if (rc == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            sprintf(string + strlen(string), "<th>%s</th>", sqlite3_column_text(stmt, 1));
        }
    }
    sprintf(string + strlen(string), "\n");

    check_sql(rc, db, db_err_msg);
}

void get_user(unsigned int id, char *string)
{
    char sql[128];
    sprintf(sql, "SELECT * FROM Users WHERE Id = %u;", id);

    rc = sqlite3_exec(db, sql, callback, string, &db_err_msg);

    check_sql(rc, db, db_err_msg);
}

void insert_user(user User)
{
    char sql[128];
    sprintf(sql, "INSERT INTO Users (Name, Surname) VALUES ('%s', '%s');", User.name, User.surname);

    rc = sqlite3_exec(db, sql, 0, 0, &db_err_msg);

    check_sql(rc, db, db_err_msg);
}

void update_user(unsigned int id, user User)
{
    char sql[128];
    sprintf(sql, "UPDATE Users SET Name = '%s', Surname = '%s' WHERE Id = %u;", User.name, User.surname, id);

    rc = sqlite3_exec(db, sql, 0, 0, &db_err_msg);

    check_sql(rc, db, db_err_msg);
}

void delete_user(unsigned int id)
{
    char sql[128];
    sprintf(sql, "DELETE FROM Users WHERE Id = %u;", id);

    rc = sqlite3_exec(db, sql, 0, 0, &db_err_msg);

    check_sql(rc, db, db_err_msg);
}

int callback(void *arg, int argc, char *argv[], char *azColName[])
{

    sprintf(arg + strlen(arg), "<tr>");
    for (int i = 0; i < argc; i++)
    {
        // printf("%s = %s\n", azColName[i], argv[i] ? argv[i] : "NULL");
        sprintf(arg + strlen(arg), "<td>%s</td>", argv[i] ? argv[i] : "NULL");
    }
    sprintf(arg + strlen(arg), "</tr>\n");
    // printf("\n");

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

    rc = sqlite3_prepare_v2(db, "SELECT SQLITE_VERSION()", -1, &res, 0);

    check_statement(rc, db);

    rc = sqlite3_step(res);

    if (rc == SQLITE_ROW)
    {
        printf("SQLite version %s\n", sqlite3_column_text(res, 0));
    }

    sqlite3_finalize(res);
}

void close_database()
{
    sqlite3_close(db);
}

void check_connection()
{
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        exit(EXIT_FAILURE);
    }
}

void check_statement()
{
    if (rc != SQLITE_OK)
    {

        fprintf(stderr, "Failed to fetch data: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);

        exit(EXIT_FAILURE);
    }
}

void check_sql()
{
    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "SQL error: %s\n", db_err_msg);
        sqlite3_free(db_err_msg);
        sqlite3_close(db);

        exit(EXIT_FAILURE);
    }
    // else {
    //     fprintf(stdout, "SQL query executed successfuly\n");
    // }
}