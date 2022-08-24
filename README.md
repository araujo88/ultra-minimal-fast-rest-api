# ultra-minimal-fast-rest-api

A minimal and fast RESTful API potentially useful for developing mock APIs. Written in C using Unix websockets, POSIX threads for multi-threaded server and integrated with SQLite.

## Running on Docker

### Requirements

`docker` <br>
`docker compose`

### Running

`docker compose up --build`

## Running locally (Linux)

### Requirements

`libsqlite3-dev`

### Running

`make clean` <br>
`make generate_models` <br>
`./generate_models` <br>
`make` <br>
`./server` <br>

## Getting started

You can define your model at the xml file named `models.xml`. A user model is provided as example for a database table:

```
<model name="users">
<col name="name">TEXT</col>
<col name="surname">TEXT</col>
<col name="age">INT</col>
<col name="height">REAL</col>
</model>
```

In the `main.c` file, start the server with `create_server(server_socket, "<ip-adress>", <port>, <max_number_of_connections>)`. Default IP address is 0.0.0.0, default port is 9002 and default maximum number of simultaneous connections is 10.

### Tests

A simple Python file located in `tests/tests.py` can be used to test each endpoint.

The server should be available at `http://localhost:9002`.

## Project structure overview

### settings.h

Contains the server settings. Currently, the only setting is ALLOWED_HOSTS, which contains an array of strings corresponding to the accepted client IP addresses.

### models.h

Contains the database model. On this example, the model consists of a simple "user" table with fields "name" and "surname". Example of an user entry in JSON format:

```
{
    "id": 1,
    "name": "Giga",
    "surname": "Chad",
    "age": 29
}
```

### routes.h

Contains the method to automatically generate basic CRUD routes. Example:

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
