#include "../include/database.h"
#include "../include/models.h"

sqlite3 *db;
int rc;
char *db_err_msg = (char) 0;

void create_table_user()
{
    // SQL statement
    char *sql = "DROP TABLE IF EXISTS Users;"
                "CREATE TABLE Users(Id INT, Name TEXT, Surname INT);";

    // wrapper for qlite3_prepare_v2(), sqlite3_step(), and sqlite3_finalize()
    rc = sqlite3_exec(db, sql, 0, 0, &db_err_msg);

    check_query(rc, db, db_err_msg);
}

void open_database()
{
    // create connection to the database
    rc = sqlite3_open("sqlite3.db", &db);

    check_connection(rc);
}

void check_version()
{
    sqlite3_stmt *res; // SQL statement

    // compiles the SQL statement into byte-code
    rc = sqlite3_prepare_v2(db, "SELECT SQLITE_VERSION()", -1, &res, 0);    
    
    // checks if data was successfuly fetched
    check_statement(rc, db);
    
    // runs the SQL statement
    rc = sqlite3_step(res);
    
    // prints data from query
    if (rc == SQLITE_ROW) {
        printf("SQLite version %s\n", sqlite3_column_text(res, 0));
    }
    
    // destroys the statement object
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

void check_query()
{
    if (rc != SQLITE_OK) 
    {
        fprintf(stderr, "SQL error: %s\n", db_err_msg);
        sqlite3_free(db_err_msg);        
        sqlite3_close(db);
        exit(EXIT_FAILURE);
    }    
}

void check_statement()
{
    if (rc != SQLITE_OK) {
        
        fprintf(stderr, "Failed to fetch data: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        
        exit(EXIT_FAILURE);
    }    
}