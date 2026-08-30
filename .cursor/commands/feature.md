# Implement feature

Add a feature to this network-facing C server without regressing its
invariants. Repo context, architecture, and the invariant list:
[AGENTS.md](../../AGENTS.md). Read it before designing.

## 1. Design against the architecture
- Decide which module owns each part of the change and keep it there:
  transport/accept/routing (`server.c`), request parsing (`http.c`), response
  building (`response.c`), endpoint behavior (`views.c`), persistence
  (`database.c`), concurrency (`threadpool.c`). Don't spread one concern across
  modules or duplicate existing machinery (e.g. response building already lives
  in `response.c`; parsing in `http.c`).
- If it changes the data model, edit `include/models.h` only, and keep
  `NUM_COLS` in sync with `TABLE_COLS` (the `_Static_assert` enforces it).
- Write down the feature's contract (inputs, outputs, status codes, error
  behavior, ownership) before coding — you will be reviewed against it.

## 2. Preserve invariants
Every new path must uphold them (see AGENTS.md):
- Frame requests via `recv_request`; never assume one `recv` is a whole request.
- Route on parsed `(method, target)`; no `strstr` over raw bytes.
- Bind all SQL values with `sqlite3_bind_*`; never concatenate input into SQL.
- Build responses/JSON with the bounded, escaping helpers; fail closed (`500`)
  rather than emit truncated/unescaped output.
- Hold `db_write_lock` across `step()` + `sqlite3_changes()` for writes.
- Keep buffers bounded; keep the `SIGINT` handler async-signal-safe; fail a bad
  request with the right status, never `exit()`.

## 3. Test it (adversarially)
- Add behavioral tests to `tests/test_regression.py` for the happy path **and**:
  malformed/oversized/fragmented input, wrong methods/ids, empty and maximum
  values, concurrency where relevant, and persistence across restart.
- If you added or changed a parser, extend `tests/http_smoke.c` (unit + fuzz).
- Ask: "what wrong implementation would still pass these tests?" — then close
  that gap.

## 4. Document truthfully
- Update `README.md` (endpoints/config) and, if you touched a contract or
  invariant, `AGENTS.md`. Don't claim behavior the code doesn't implement.

## 5. Verify and ship
- Full local gate green: `make format-check`, `make cppcheck`, `make strict`,
  `make http-test`, `pytest -v`, `make valgrind`.
- Branch off `main`; one feature per PR; split unrelated changes. Open a PR
  describing the contract and how the tests exercise it.
