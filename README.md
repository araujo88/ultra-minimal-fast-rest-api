# ultra-minimal-fast-rest-api

A minimal and fast RESTful API potentially useful for developing mock APIs with basic CRUD (create/read/update/delete) functionality. Written in C using Unix (BSD) sockets, POSIX threads for a multi-threaded server and integrated with SQLite.

## Running on Docker

### Requirements

`docker` <br>
`docker compose`

### Running

`./run_container`

## Running locally (Linux)

### Requirements

`libsqlite3-dev` <br>
`make` <br>
`gcc` <br>

### Running

`./run_locally`

## Getting started

You define your model directly in `include/models.h` as compile-time
constants; the schema, CRUD SQL, JSON serialization, and routes are all built
from it. The provided example is a `users` table:

```c
#define NUM_COLS 4
#define STR_LEN 256
#define TABLE_NAME "users"

static const char *TABLE_COLS[NUM_COLS][2] __attribute__((unused)) = {
    {"name", "TEXT"},
    {"surname", "TEXT"},
    {"age", "INT"},
    {"height", "REAL"},
};
```

To change the model, edit `TABLE_NAME` and the `TABLE_COLS` `{name, type}`
list (supported types: `TEXT`, `INT`, `REAL`), keep `NUM_COLS` equal to the
number of columns, and rebuild. An `Id INTEGER PRIMARY KEY` column is added
automatically.

In the `main.c` file, start the server with `create_server("<ip-address>", <port>, <max_number_of_connections>, pool)`. Default IP address is 0.0.0.0, default port is 9002 and default maximum number of simultaneous connections is 10.

### Tests

Simple Python scripts located in `tests/test1.py` and `tests/test2.py` can be used to exercise each endpoint (they require the `requests` package; `test2.py` also uses `faker`).

The server should be available at `http://localhost:9002`.

## Project structure overview

### settings.h

Contains the server settings. Currently, the only setting is ALLOWED_HOSTS, which contains an array of strings corresponding to the accepted client IP addresses.

### models.h

Contains the database model, edited directly (see [Getting started](#getting-started)). The example is a `users` table with fields `name`, `surname`, `age`, and `height`. Example of a user entry in JSON format:

```
{
    "Id": 1,
    "name": "Giga",
    "surname": "Chad",
    "age": 29,
    "height": 1.80
}
```

### Routing

Requests are dispatched in `server.c` (`route_request`) by exact method and
path against the model's table name. The available routes are:

`GET /` - root with "Hello world" message <br>
`GET /users` - lists all users in .json format <br>
`GET /users/<id>` - list user data by its id <br>
`PUT /users/<id>` - update user data by its id <br>
`DELETE /users/<id>` delete user by its id <br>
`POST /users` - creates a new user <br>

### database.h

Integrates the requests from the HTTP methods to the SQLite database and parses JSON data.

### views.h

The views related to each route and its respective HTTP methods, performing calls to the database.

### server.h

Contains functions that implement a multi-threaded server using Unix sockets and POSIX threads.
