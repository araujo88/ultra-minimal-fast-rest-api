# ultra-minimal-fast-rest-api

A small, dependency-light REST-style HTTP server in C: POSIX sockets, a custom
bounded thread pool, and SQLite, with the data model and its CRUD routes
derived from a single compile-time declaration.

It is intended as a **mock/CRUD API for local development**, not as a
public-facing web server. See [SECURITY.md](SECURITY.md) for the threat model
and [AUDIT.md](AUDIT.md) for the security review this codebase was hardened
against.

## Features

- **HTTP/1.1 request handling** with proper framing over the TCP byte stream
  (headers + `Content-Length` body; a request is not assumed to be one `recv`),
  including **keep-alive** (persistent connections) and pipelined requests.
- **Structured routing** on exact `METHOD` + path — no substring matching of
  raw bytes.
- **CRUD over SQLite** using prepared statements with bound parameters (no user
  input is concatenated into SQL).
- **Bounded JSON serialization** that escapes all values and fails closed
  (`500`) rather than emitting a truncated body.
- **Custom thread pool**: fixed workers, a bounded task queue with backpressure,
  and clean shutdown on `SIGINT`.
- **Compile-time model**: define your table once in `include/models.h`; schema,
  SQL, JSON, and routes follow.
- **Client IP allowlist**, configurable at runtime via `ALLOWED_HOSTS`.

## Quick start (Linux)

Requirements: `gcc`, `make`, `libsqlite3-dev`.

```bash
make          # build ./server  (use `make strict` for -Werror)
./server      # listens on 0.0.0.0:9002
```

Or use the helper: `./run_locally.sh` (clean build + run).

```bash
curl -s -X POST localhost:9002/users -d 'name=Giga&surname=Chad&age=29&height=1.80'
curl -s localhost:9002/users
```

## Running with Docker

Requirements: `docker`, `docker compose`.

```bash
./run_container.sh        # docker compose up --build
```

The image defaults `ALLOWED_HOSTS=*` because requests forwarded through Docker's
bridge arrive from the gateway IP, not `127.0.0.1` (see
[Configuration](#configuration)).

## Endpoints

For the example `users` model:

| Method & path        | Success           | Notes |
| -------------------- | ----------------- | ----- |
| `GET /`              | `200` text/html   | "Hello world!" |
| `GET /users`         | `200` JSON array  | `[]` when empty; `500` if the list exceeds the response buffer |
| `POST /users`        | `201` JSON        | body is `application/x-www-form-urlencoded` |
| `GET /users/<id>`    | `200` JSON object | `404` if not found, `400` if `<id>` is non-numeric |
| `PUT /users/<id>`    | `200` JSON        | `404` if not found, `400` if `<id>` is non-numeric |
| `DELETE /users/<id>` | `200` JSON        | `404` if not found, `400` if `<id>` is non-numeric |

Unknown routes return `404`; unsupported methods on a known path return `405`;
database/serialization failures return `500`.

## Defining your model

The model lives in [`include/models.h`](include/models.h) as compile-time
constants — the schema, CRUD SQL, JSON serialization, and routes are all built
from it:

```c
#define NUM_COLS 4
#define STR_LEN 256
#define TABLE_NAME "users"

// Left unsized so the initializer sets the row count; a _Static_assert then
// requires it to equal NUM_COLS (so the two can't silently drift).
static const char *TABLE_COLS[][2] __attribute__((unused)) = {
    {"name", "TEXT"},
    {"surname", "TEXT"},
    {"age", "INT"},
    {"height", "REAL"},
};
```

Edit `TABLE_NAME` and the `TABLE_COLS` `{name, type}` list (supported types:
`TEXT`, `INT`, `REAL`), keep `NUM_COLS` equal to the number of columns, and
rebuild. An `Id INTEGER PRIMARY KEY` column is added automatically. Data is
stored in `sqlite3.db` in the working directory and persists across restarts.

Example JSON entry:

```json
{ "Id": 1, "name": "Giga", "surname": "Chad", "age": 29, "height": 1.80 }
```

## Configuration

- **Bind address / port / connections** — arguments to `create_server(...)` in
  [`src/main.c`](src/main.c). Defaults: `0.0.0.0`, `9002`, `10`.
- **`ALLOWED_HOSTS`** (environment variable) — comma-separated IPv4 allowlist,
  or `*` to allow all clients. Invalid entries are ignored; if unset, the
  compile-time default in [`include/settings.h`](include/settings.h) applies
  (`127.0.0.1`). Example: `ALLOWED_HOSTS="127.0.0.1,10.0.0.5" ./server`.

## Performance

Reproduce with `make bench` (builds nothing extra; drives the running server
with `bench/bench.py`, a dependency-free client). It runs two connection models:
one TCP connection per request, and HTTP/1.1 **keep-alive** (one connection
reused for many requests).

Indicative results on the development machine (WSL2, 16 vCPU), 3s per cell —
treat the **shape** as the takeaway, not the absolute numbers, which are
hardware-dependent. `req/s @ c=1` is single-client throughput (where connection
setup matters most); `@ c=16` is under 16 concurrent clients:

| Scenario            | per-request @ c=1 | keep-alive @ c=1 | per-request @ c=16 | keep-alive @ c=16 |
| ------------------- | ----------------: | ---------------: | -----------------: | ----------------: |
| `GET /` (no DB)     |            ~9,900 |          ~24,000 |            ~16,700 |           ~40,000 |
| `GET /users` (list) |            ~6,700 |          ~10,600 |            ~14,500 |           ~13,700 |
| `GET /users/1`      |            ~8,100 |          ~19,500 |            ~15,200 |           ~23,700 |
| `POST /users`       |            ~6,400 |          ~12,300 |            ~14,300 |           ~15,200 |

What this says about "fast":

- **Reads are fast** — tens of thousands of requests/sec at sub-millisecond
  median latency. SQLite reads come from the OS page cache, so an
  application-level response cache would optimize a path that is already fast and
  would add cache-invalidation and cross-thread-locking risk for no real gain.
- **Writes are fast too.** The database opens in **WAL mode with
  `synchronous=NORMAL`** ([`open_database()`](src/database.c)), so the writer
  fsyncs at checkpoints instead of once per transaction. That took `POST /users`
  from ~170 req/s (p50 ~100 ms at c=16) to the numbers above — a ~40–90×
  improvement over the previous `synchronous=FULL` default — and writes now scale
  with concurrency instead of serializing behind one `fsync` per insert.
  - *Durability trade-off:* under `synchronous=NORMAL`, an application crash is
    still safe; only an OS/power crash can lose the last few committed
    transactions. Set `synchronous=FULL` for strict durability (giving back most
    of the write speedup).
- **HTTP/1.1 keep-alive** ([`send_data()`](src/server.c)) reuses a connection for
  many requests, removing per-request TCP setup. The win is largest for
  single/low-concurrency clients and cheap endpoints (`GET /` ~2.4×, `GET
  /users/1` ~2.4× at c=1); at higher concurrency the 8-worker pool and the DB
  become the ceiling, so the gain shrinks.
  - *Trade-off:* this is a **blocking** thread pool, so a kept-alive connection
    pins a worker for its lifetime. With only 8 workers, that limits the number
    of simultaneously *active* persistent connections. Two guards bound the
    damage: a per-connection request cap (`MAX_KEEPALIVE_REQUESTS`, after which
    the server sends `Connection: close`) and the `SO_RCVTIMEO` idle timeout that
    drops a quiet connection. Real scale-out for many persistent clients would
    need event-driven I/O (epoll), which this server does not use.

## Project layout

| File | Responsibility |
| ---- | -------------- |
| `src/main.c` | Entry point, `SIGINT` handler (sets a flag; cleanup runs in normal context) |
| `src/server.c` | Socket setup, accept loop, IP allowlist, routing |
| `src/http.c` | Request framing and request-line / path / id / form parsing |
| `src/response.c` | HTTP response construction (status line, headers, body) |
| `src/views.c` | Per-endpoint handlers; map DB results to HTTP status |
| `src/database.c` | SQLite CRUD (prepared statements) + bounded JSON serialization |
| `src/threadpool.c` | Bounded worker pool with backpressure |
| `include/models.h` | The data model (edit this) |
| `include/settings.h` | Default client allowlist |

## Development

```bash
make strict        # build with -Werror
make format-check  # clang-format check (config in .clang-format); `make format` to apply
make cppcheck      # static analysis
make http-test     # parser unit tests + fuzz loop under ASan/UBSan
make valgrind      # run the server under Valgrind while driving requests
make bench         # throughput benchmark (see Performance)
```

Regression tests (manage their own server instance):

```bash
cd tests && pip install -r requirements.txt && pytest -v
```

CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs, in stages,
lint → static analysis → build → tests + valgrind.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the workflow and invariants, and
[AGENTS.md](AGENTS.md) if you are using an AI coding agent. Report
vulnerabilities per [SECURITY.md](SECURITY.md).

## License

See [LICENSE](LICENSE).
