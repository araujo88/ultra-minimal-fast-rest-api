#ifndef _MODELS_H
#define _MODELS_H 1

// ---------------------------------------------------------------------------
// Data model (edit this file to define your model, then rebuild).
//
// The model is expressed directly as compile-time constants -- the rest of the
// server (schema creation, CRUD SQL, JSON serialization, and routing) is built
// from these. To change the model:
//
//   * TABLE_NAME : the SQL table name and the URL path segment (e.g. /users).
//   * TABLE_COLS : one {name, type} pair per column, in order. Types are the
//                  SQLite affinities the JSON serializer understands:
//                  "TEXT", "INT", "REAL". An implicit "Id" INTEGER PRIMARY KEY
//                  column is added automatically and is not listed here.
//   * NUM_COLS   : must equal the number of rows in TABLE_COLS.
//   * STR_LEN    : max bytes stored per field value while parsing a request.
//
// Names must be valid SQL identifiers (they are emitted into SQL and matched
// against form field names). Keep NUM_COLS in sync with TABLE_COLS.
// ---------------------------------------------------------------------------

#define NUM_COLS 4

#define STR_LEN 256

#define TABLE_NAME "users"

static const char *TABLE_COLS[NUM_COLS][2] __attribute__((unused)) = {
    {"name", "TEXT"},
    {"surname", "TEXT"},
    {"age", "INT"},
    {"height", "REAL"},
};

#endif
