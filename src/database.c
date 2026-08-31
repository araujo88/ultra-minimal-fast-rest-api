#include "../include/database.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

sqlite3 *db;

// Serializes writes over the single shared connection. sqlite3_changes()
// reports the row count of the most recently completed INSERT/UPDATE/DELETE on
// the connection, so the step()+sqlite3_changes() pair in update/delete is only
// meaningful if no other writer's statement can complete in between. All write
// paths (create/update/delete) take this lock so that window is exclusive.
// Reads (SELECT) do not affect the change counter and stay lock-free; the
// connection itself is used in SQLite's default serialized threading mode.
static pthread_mutex_t db_write_lock = PTHREAD_MUTEX_INITIALIZER;

// ---------------------------------------------------------------------------
// Bounded string builder + JSON serialization
//
// Every append is bounded by the destination capacity, so hostile or merely
// large database contents can never overflow the response buffer. String
// values are JSON-escaped, and numeric columns are emitted as bare JSON
// numbers only when the stored value actually parses as a number; otherwise
// they are quoted.
//
// If the content ever exceeds the caller's buffer, the strbuf records that it
// was truncated instead of silently emitting a half-written object. The read
// paths propagate that as a failure so the view can return 500 rather than a
// 200 carrying invalid JSON. Bounded output is thus either complete-and-valid
// or an explicit error -- never a truncated body advertised as success.
// ---------------------------------------------------------------------------

typedef struct
{
    char *buf;
    size_t cap;
    size_t len;
    int truncated; // set once any append did not fit; the buffer is then
                   // incomplete and callers must NOT treat it as valid JSON
} strbuf_t;

static void sb_init(strbuf_t *sb, char *buf, size_t cap)
{
    sb->buf = buf;
    sb->cap = cap;
    sb->len = 0;
    sb->truncated = 0;
    if (cap > 0)
        buf[0] = '\0';
}

static void sb_puts(strbuf_t *sb, const char *s)
{
    if (sb->cap == 0)
    {
        if (*s)
            sb->truncated = 1;
        return;
    }
    while (*s && sb->len + 1 < sb->cap)
        sb->buf[sb->len++] = *s++;
    sb->buf[sb->len] = '\0';
    if (*s)
        sb->truncated = 1; // ran out of room before consuming the whole string
}

static void sb_putc(strbuf_t *sb, char c)
{
    if (sb->cap == 0 || sb->len + 1 >= sb->cap)
    {
        sb->truncated = 1;
        return;
    }
    sb->buf[sb->len++] = c;
    sb->buf[sb->len] = '\0';
}

static void sb_put_json_string(strbuf_t *sb, const char *s)
{
    char esc[8];
    sb_putc(sb, '"');
    for (; *s; s++)
    {
        unsigned char c = (unsigned char)*s;
        switch (c)
        {
        case '"':
            sb_puts(sb, "\\\"");
            break;
        case '\\':
            sb_puts(sb, "\\\\");
            break;
        case '\n':
            sb_puts(sb, "\\n");
            break;
        case '\r':
            sb_puts(sb, "\\r");
            break;
        case '\t':
            sb_puts(sb, "\\t");
            break;
        default:
            if (c < 0x20)
            {
                snprintf(esc, sizeof(esc), "\\u%04x", c);
                sb_puts(sb, esc);
            }
            else
            {
                sb_putc(sb, (char)c);
            }
        }
    }
    sb_putc(sb, '"');
}

// Emit a value for a column typed INT/REAL: a bare JSON number when the text
// is genuinely numeric, JSON null when the column is NULL, otherwise a quoted
// (escaped) string so the output stays valid JSON.
static void sb_put_numeric(strbuf_t *sb, const char *value)
{
    if (value == NULL)
    {
        sb_puts(sb, "null");
        return;
    }
    char *end = NULL;
    strtod(value, &end);
    if (end != value && *end == '\0')
        sb_puts(sb, value); // valid number, emit bare
    else
        sb_put_json_string(sb, value);
}

int callback(void *arg, int argc, char *argv[], char *azColName[])
{
    strbuf_t *sb = (strbuf_t *)arg;

    sb_puts(sb, "{");
    for (int i = 0; i < argc; i++)
    {
        if (i > 0)
            sb_putc(sb, ',');
        sb_put_json_string(sb, azColName[i]);
        sb_putc(sb, ':');

        int is_numeric = 0;
        if (strcmp(azColName[i], "Id") == 0)
            is_numeric = 1;
        else if (i - 1 >= 0 && i - 1 < NUM_COLS &&
                 (strcmp(TABLE_COLS[i - 1][1], "INT") == 0 ||
                  strcmp(TABLE_COLS[i - 1][1], "REAL") == 0))
            is_numeric = 1;

        if (is_numeric)
            sb_put_numeric(sb, argv[i]);
        else if (argv[i] == NULL)
            sb_puts(sb, "null");
        else
            sb_put_json_string(sb, argv[i]);
    }
    sb_puts(sb, "},");

    // A non-zero return aborts sqlite3_exec (SQLITE_ABORT). Stop the moment the
    // output no longer fits so we never keep scanning rows into a dead buffer.
    return sb->truncated ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Schema
// ---------------------------------------------------------------------------

void create_table()
{
    int i = 0;
    char sql[SQL_QUERY_SIZE];
    char *err = NULL;

    // IF NOT EXISTS: never destroy existing rows on startup.
    snprintf(sql, sizeof(sql),
             "CREATE TABLE IF NOT EXISTS %s(Id INTEGER PRIMARY KEY,", TABLE_NAME);

    for (i = 0; i < NUM_COLS; i++)
    {
        size_t n = strlen(sql);
        snprintf(sql + n, sizeof(sql) - n, " %s %s,", TABLE_COLS[i][0], TABLE_COLS[i][1]);
    }

    size_t n = strlen(sql);
    if (n >= 1)
        snprintf(sql + n - 1, sizeof(sql) - (n - 1), ");"); // overwrite trailing ','

    int rc = sqlite3_exec(db, sql, 0, 0, &err);
    check_sql(rc, err, NULL, 0);
}

// ---------------------------------------------------------------------------
// Read paths (prepared statements, JSON-serialized via callback)
// ---------------------------------------------------------------------------

// Returns 0 on success (buffer holds complete, valid JSON), -1 on SQL error or
// on truncation (buffer contents must then be discarded by the caller).
int get_entries(char *buffer, size_t cap)
{
    char sql[SQL_QUERY_SIZE];
    char *err = NULL;
    strbuf_t sb;

    snprintf(sql, sizeof(sql), "SELECT * FROM %s;", TABLE_NAME);

    sb_init(&sb, buffer, cap);
    sb_puts(&sb, "[");

    int rc = sqlite3_exec(db, sql, callback, &sb, &err);
    if (err != NULL)
        sqlite3_free(err); // SQLITE_ABORT (our truncation stop) also sets err

    // SQLITE_ABORT is our own truncation signal from callback(); a genuine SQL
    // failure is anything else that isn't OK. Either way, fail closed.
    if (sb.truncated || (rc != SQLITE_OK && rc != SQLITE_ABORT))
        return -1;

    if (sb.len > 0 && sb.buf[sb.len - 1] == ',')
        sb.buf[--sb.len] = '\0'; // drop trailing comma from last row
    sb_puts(&sb, "]");

    return sb.truncated ? -1 : 0; // closing bracket must have fit too
}

// Returns 0 on success, -1 on SQL error or truncation. A missing row is a
// success that yields "{}".
int get_entry(unsigned int id, char *buffer, size_t cap)
{
    sqlite3_stmt *stmt = NULL;
    strbuf_t sb;
    sb_init(&sb, buffer, cap);

    int rc = sqlite3_prepare_v2(db, "SELECT * FROM " TABLE_NAME " WHERE Id = ?;", -1, &stmt, NULL);
    if (rc != SQLITE_OK)
    {
        sqlite3_finalize(stmt);
        return DB_ERROR;
    }

    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)id);
    int step = sqlite3_step(stmt);
    if (step != SQLITE_ROW)
    {
        sqlite3_finalize(stmt);
        return (step == SQLITE_DONE) ? DB_NOT_FOUND : DB_ERROR;
    }

    int argc = sqlite3_column_count(stmt);
    char **argv = malloc(sizeof(char *) * argc);
    char **col = malloc(sizeof(char *) * argc);
    for (int i = 0; i < argc; i++)
    {
        argv[i] = (char *)sqlite3_column_text(stmt, i);
        col[i] = (char *)sqlite3_column_name(stmt, i);
    }
    callback(&sb, argc, argv, col);
    free(argv);
    free(col);
    if (sb.len > 0 && sb.buf[sb.len - 1] == ',')
        sb.buf[--sb.len] = '\0'; // drop trailing comma

    sqlite3_finalize(stmt);
    return sb.truncated ? DB_ERROR : DB_OK;
}

// ---------------------------------------------------------------------------
// Write paths (fully parameterized: no user data reaches SQL text)
// ---------------------------------------------------------------------------

static int create_entry_impl(char struct_string[NUM_COLS][STR_LEN], char *buffer, size_t cap)
{
    char sql[SQL_QUERY_SIZE];
    sqlite3_stmt *stmt = NULL;
    int i;

    snprintf(sql, sizeof(sql), "INSERT INTO %s (", TABLE_NAME);
    for (i = 0; i < NUM_COLS; i++)
    {
        size_t n = strlen(sql);
        snprintf(sql + n, sizeof(sql) - n, "%s%s", TABLE_COLS[i][0], (i + 1 < NUM_COLS) ? ", " : "");
    }
    {
        size_t n = strlen(sql);
        snprintf(sql + n, sizeof(sql) - n, ") VALUES (");
    }
    for (i = 0; i < NUM_COLS; i++)
    {
        size_t n = strlen(sql);
        snprintf(sql + n, sizeof(sql) - n, "?%s", (i + 1 < NUM_COLS) ? ", " : "");
    }
    {
        size_t n = strlen(sql);
        snprintf(sql + n, sizeof(sql) - n, ");");
    }

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        for (i = 0; i < NUM_COLS; i++)
            sqlite3_bind_text(stmt, i + 1, struct_string[i], -1, SQLITE_TRANSIENT);
        rc = sqlite3_step(stmt);
        rc = (rc == SQLITE_DONE) ? SQLITE_OK : rc;
    }
    sqlite3_finalize(stmt);

    check_sql(rc, NULL, buffer, cap);
    return (rc == SQLITE_OK) ? DB_OK : DB_ERROR;
}

static int update_entry_impl(unsigned int id, char struct_string[NUM_COLS][STR_LEN], char *buffer, size_t cap)
{
    char sql[SQL_QUERY_SIZE];
    sqlite3_stmt *stmt = NULL;
    int i;
    int changes = 0;

    snprintf(sql, sizeof(sql), "UPDATE %s SET", TABLE_NAME);
    for (i = 0; i < NUM_COLS; i++)
    {
        size_t n = strlen(sql);
        snprintf(sql + n, sizeof(sql) - n, " %s = ?%s", TABLE_COLS[i][0], (i + 1 < NUM_COLS) ? "," : "");
    }
    {
        size_t n = strlen(sql);
        snprintf(sql + n, sizeof(sql) - n, " WHERE Id = ?;");
    }

    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        for (i = 0; i < NUM_COLS; i++)
            sqlite3_bind_text(stmt, i + 1, struct_string[i], -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, NUM_COLS + 1, (sqlite3_int64)id);
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE)
        {
            changes = sqlite3_changes(db);
            rc = SQLITE_OK;
        }
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_OK)
    {
        check_sql(rc, NULL, buffer, cap);
        return DB_ERROR;
    }
    if (changes == 0)
        return DB_NOT_FOUND; // no row with that Id
    check_sql(SQLITE_OK, NULL, buffer, cap);
    return DB_OK;
}

static int delete_entry_impl(unsigned int id, char *buffer, size_t cap)
{
    sqlite3_stmt *stmt = NULL;
    int changes = 0;

    int rc = sqlite3_prepare_v2(db, "DELETE FROM " TABLE_NAME " WHERE Id = ?;", -1, &stmt, NULL);
    if (rc == SQLITE_OK)
    {
        sqlite3_bind_int64(stmt, 1, (sqlite3_int64)id);
        rc = sqlite3_step(stmt);
        if (rc == SQLITE_DONE)
        {
            changes = sqlite3_changes(db);
            rc = SQLITE_OK;
        }
    }
    sqlite3_finalize(stmt);

    if (rc != SQLITE_OK)
    {
        check_sql(rc, NULL, buffer, cap);
        return DB_ERROR;
    }
    if (changes == 0)
        return DB_NOT_FOUND; // no row with that Id
    check_sql(SQLITE_OK, NULL, buffer, cap);
    return DB_OK;
}

// Public write entry points: serialize the step()+sqlite3_changes() sequence
// (and the INSERT step) across all writers on the shared connection.

int create_entry(char struct_string[NUM_COLS][STR_LEN], char *buffer, size_t cap)
{
    pthread_mutex_lock(&db_write_lock);
    int r = create_entry_impl(struct_string, buffer, cap);
    pthread_mutex_unlock(&db_write_lock);
    return r;
}

int update_entry(unsigned int id, char struct_string[NUM_COLS][STR_LEN], char *buffer, size_t cap)
{
    pthread_mutex_lock(&db_write_lock);
    int r = update_entry_impl(id, struct_string, buffer, cap);
    pthread_mutex_unlock(&db_write_lock);
    return r;
}

int delete_entry(unsigned int id, char *buffer, size_t cap)
{
    pthread_mutex_lock(&db_write_lock);
    int r = delete_entry_impl(id, buffer, cap);
    pthread_mutex_unlock(&db_write_lock);
    return r;
}

// ---------------------------------------------------------------------------
// Connection lifecycle + diagnostics
// ---------------------------------------------------------------------------

void open_database()
{
    int rc = sqlite3_open("sqlite3.db", &db);
    check_connection(rc);

    // WAL + synchronous=NORMAL: readers no longer block on a writer's commit,
    // and the writer fsyncs at checkpoints instead of once per transaction.
    // This trades a small durability window (only the last few transactions can
    // be lost on an OS/power crash -- an application crash is still safe) for a
    // large write-throughput gain. journal_mode returns a row, so pass a
    // callback that ignores it. Best-effort: on a filesystem that rejects WAL
    // SQLite falls back to the previous mode.
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", NULL, NULL, NULL);
    sqlite3_exec(db, "PRAGMA synchronous=NORMAL;", NULL, NULL, NULL);
}

void check_version()
{
    sqlite3_stmt *res;

    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;

    int rc = sqlite3_prepare_v2(db, "SELECT SQLITE_VERSION()", -1, &res, 0);
    if (rc == SQLITE_OK)
    {
        rc = sqlite3_step(res);
        if (rc == SQLITE_ROW)
        {
            printf("[%s] - ", current_date);
            printf("\033[0;33mSQLite version %s\n\033[0m", sqlite3_column_text(res, 0));
        }
    }
    sqlite3_finalize(res);
}

void close_database()
{
    sqlite3_close(db);
}

// Readiness probe: can the connection execute a trivial query right now?
int db_ok(void)
{
    sqlite3_stmt *stmt = NULL;
    int ok = 0;
    if (sqlite3_prepare_v2(db, "SELECT 1;", -1, &stmt, NULL) == SQLITE_OK &&
        sqlite3_step(stmt) == SQLITE_ROW)
        ok = 1;
    sqlite3_finalize(stmt);
    return ok;
}

void check_connection(int rc)
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

// rc/err are per-call locals (no shared global state). If buffer is non-NULL,
// a JSON status object is written into it (bounded by cap).
void check_sql(int rc, char *err, char *buffer, size_t cap)
{
    char *current_date;
    time_t t;
    time(&t);
    current_date = ctime(&t);
    current_date[strcspn(current_date, "\n")] = 0;
    printf("[%s] - ", current_date);

    if (rc != SQLITE_OK)
    {
        fprintf(stderr, "\033[0;33mSQL error: %s\n\033[0m", err ? err : sqlite3_errmsg(db));
        if (buffer != NULL && cap > 0)
            snprintf(buffer, cap, "{\"msg\": \"error\"}");
    }
    else
    {
        fprintf(stdout, "\033[0;33mSQL query executed successfuly\n\033[0m");
        if (buffer != NULL && cap > 0)
            snprintf(buffer, cap, "{\"msg\": \"success\"}");
    }

    if (err != NULL)
        sqlite3_free(err); // free SQLite-allocated error text (was leaked before)
}
