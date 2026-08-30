# AGENTS.md

Guidance for AI coding agents (and humans) working in this repository. Read this
before changing code. It is the source of truth for build/test commands,
architecture, and the invariants that must not regress.

## What this is

A small REST-style HTTP server in C (POSIX sockets, a custom bounded thread
pool, SQLite) whose data model and CRUD routes are derived from a single
compile-time declaration in `include/models.h`. It targets **local
development / mock APIs**, not untrusted public exposure. Background:
[`AUDIT.md`](AUDIT.md) (the security review this code was hardened against) and
[`SECURITY.md`](SECURITY.md) (threat model).

## Commands

| Task | Command |
| ---- | ------- |
| Build | `make` (artifacts: `./server`, `obj/`) |
| Build, warnings as errors | `make strict` |
| Format check / apply | `make format-check` / `make format` (clang-format, `.clang-format`) |
| Static analysis | `make cppcheck` |
| Parser unit + fuzz (ASan/UBSan) | `make http-test` |
| Valgrind memcheck (drives the server) | `make valgrind` |
| Regression tests | `cd tests && pip install -r requirements.txt && pytest -v` |
| Run | `./server` (or `./run_locally.sh`) |
| Clean | `make clean` |

Requirements: `gcc`, `make`, `libsqlite3-dev`; for the full checks also
`clang-format`, `cppcheck`, `valgrind`, and Python (`pytest`, `requests`).

**Before opening a PR, the local equivalent of CI must pass:** `make
format-check`, `make cppcheck`, `make strict`, `make http-test`, the `pytest`
suite, and `make valgrind`.

## Architecture

Request path:

```
client → accept() [server.c] → thread pool queue [threadpool.c]
       → worker: recv_request() framing [http.c]
       → parse request line / id / form [http.c]
       → route_request() exact method+path [server.c]
       → view handler [views.c]
       → SQLite prepared statement [database.c]
       → bounded JSON build [database.c]
       → response construction [response.c] → send() → close()
```

Module responsibilities:

- `main.c` — startup, install `SIGINT` handler (sets `server_running = 0` only),
  then `create_server()`.
- `server.c` — socket/bind/listen/accept loop, `ALLOWED_HOSTS` allowlist,
  `route_request()`. Owns transport and routing, not parsing or responses.
- `http.c` / `http.h` — pure request parsing: `recv_request` (the only I/O),
  `find_body`, `parse_request_line`, `parse_form`, `parse_id`. No sockets in
  the parsers themselves so they can be unit-tested/fuzzed (`tests/http_smoke.c`).
- `response.c` / `response.h` — `response_send` (status line + Date +
  Content-Type + Content-Length + body), `response_send_all`,
  `response_log_prefix`. The one place responses are built.
- `views.c` — per-endpoint handlers; translate `DB_OK` / `DB_NOT_FOUND` /
  `DB_ERROR` into `200`/`201` / `404` / `500`.
- `database.c` — SQLite CRUD via prepared statements + the bounded JSON string
  builder. Owns the DB write lock.
- `threadpool.c` — one mutex, a bounded ring-buffer queue with backpressure,
  two condition variables, clean shutdown.
- `include/models.h` — the data model. `include/settings.h` — default allowlist.

## Invariants (do not regress these)

These were established by the security review. Breaking one is a blocking defect.

1. **Framing, not one `recv`.** All request bytes are read via
   `recv_request()`, which reads headers then `Content-Length` body bytes,
   bounded by the buffer. Never assume a single `recv` is a whole request.
2. **Structured routing.** Dispatch on parsed `(method, target)` via exact
   `strcmp`/prefix — never `strstr` over the raw request.
3. **Parameterized SQL.** All values reach SQLite through
   `sqlite3_bind_*` on prepared statements. Never `sprintf`/concatenate user
   input into SQL. Identifiers come only from `models.h`.
4. **Bounded, escaped JSON.** Output is built with the length-checked `strbuf`
   builder that escapes strings; on overflow it sets `truncated` and the read
   path returns `DB_ERROR` → `500`. Never emit an unbounded/unescaped body.
5. **Serialized writers.** `create/update/delete_entry` hold `db_write_lock`
   across `step()` + `sqlite3_changes()` — the change count is connection-global
   and races otherwise. Reads stay lock-free.
6. **Bounded thread pool.** `queue_push` never overwrites unconsumed tasks;
   producers block on `slot_available`. `active_tasks` is initialized and
   coordinated under the single pool mutex.
7. **Async-signal-safe shutdown.** The `SIGINT` handler only writes a
   `volatile sig_atomic_t`. Cleanup (join, close DB, free) happens in normal
   context after the accept loop exits.
8. **Request-local failure.** A malformed request or DB error fails that one
   request (with the right status). It must not `exit()` the process.
9. **Every fixed buffer is bounded.** Use `snprintf`/bounded loops. No
   `strcpy`/`strcat`/`sprintf`/unbounded `strncat`; no scanning to a sentinel
   without a length bound.
10. **`NUM_COLS` == rows of `TABLE_COLS`.** Enforced by `_Static_assert` in
    `models.h`; keep them in sync.
11. **Responses are CRLF HTTP/1.1** with a correct `Content-Length`.
    `response_send` refuses to send a body whose length disagrees with the
    header.

If you must change one of these, say so explicitly in the PR and prove the new
behavior with tests.

## Conventions

- C, Allman braces, 4-space indent (enforced by `.clang-format`; run
  `make format` before committing).
- Keep the code warning-clean under `-Wall -Wextra -Wpedantic -Werror`, and
  clean under ASan/UBSan and Valgrind.
- Match the surrounding style; keep comments truthful — a misleading comment is
  treated as a bug here.
- Put logic in the module that owns the concern (transport vs parsing vs routing
  vs response vs persistence). Don't duplicate `send_all`-style machinery.

## Tests

`tests/test_regression.py` is the behavioral suite; `tests/support.py` /
`tests/conftest.py` build the server and manage its lifecycle (fresh DB per
test, restart for persistence, `ALLOWED_HOSTS` overrides). New behavior needs a
test, and adversarial coverage (malformed input, concurrency, persistence) is
expected — a happy-path-only test is not sufficient. `tests/http_smoke.c` is the
C parser harness run by `make http-test`.

## Workflow

- Branch off `main`; never commit directly to `main`. One logical change per PR;
  split unrelated changes.
- Open a PR; CI must be green. Reviews use the rubric in
  [`.cursor/commands/review.md`](.cursor/commands/review.md).
- Task playbooks: fixing a bug → [`.cursor/commands/bugfix.md`](.cursor/commands/bugfix.md);
  adding a feature → [`.cursor/commands/feature.md`](.cursor/commands/feature.md).

## Gotchas

- `ctime` is not thread-safe; response/date formatting uses `ctime_r` in
  `response.c`. Don't reintroduce `ctime` on a worker path.
- SQLite runs in its default serialized mode over one shared connection; that
  makes calls memory-safe but does **not** make `sqlite3_changes()` correct
  across threads (see invariant 5).
- The connection opens in **WAL journal mode with `synchronous=NORMAL`**
  (`open_database()`) for write throughput. This is orthogonal to the threading
  mode above. Trade-off: an OS/power crash can lose the last few committed
  transactions (an application crash is safe).
- The IP allowlist reads the real accepted peer address. Behind Docker's bridge,
  clients appear as the gateway IP — hence the image sets `ALLOWED_HOSTS=*`.
- `sqlite3.db` is created in the working directory; tests and `make valgrind`
  clean it up.
