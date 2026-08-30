# Bugfix

Fix a bug in this network-facing C server. Root-cause it; don't paper over the
symptom. Repo context and invariants: [AGENTS.md](../../AGENTS.md).

## 1. Reproduce first
- Write the smallest reproduction you can: a failing `pytest` case in
  `tests/test_regression.py`, a raw-socket case via `tests/support.py`
  `raw_send`, or a parser case in `tests/http_smoke.c`.
- If it's a memory/concurrency bug, reproduce under `make http-test` (ASan/UBSan)
  or `make valgrind`. A bug you can't reproduce, you can't prove you fixed.

## 2. Find the root cause, not the symptom
- Identify which module owns the defect (transport `server.c` / parsing
  `http.c` / response `response.c` / views `views.c` / persistence
  `database.c` / pool `threadpool.c`).
- Ask what *invariant* was violated (see AGENTS.md "Invariants"). The strongest
  fix restores the invariant so the whole class of bug goes away — not just the
  one input. Prefer making the invalid state unrepresentable over adding another
  special case.
- Check the comments near the bug: if a comment overstated behavior, the comment
  is part of the bug — fix it too.

## 3. Fix
- Keep the change minimal and in the owning module. Don't smuggle in unrelated
  refactors — split those into a separate PR.
- Preserve every invariant (framing, parameterized SQL, bounded/escaped JSON,
  serialized DB writers, async-signal-safe shutdown, bounded buffers, correct
  per-request error status, no `exit()` on a bad request).

## 4. Prove it
- The reproduction test must now pass and must have failed before the fix
  (state that explicitly).
- Run the full local gate: `make format-check`, `make cppcheck`, `make strict`,
  `make http-test`, `pytest -v`, `make valgrind`. All green.

## 5. Ship
- Branch off `main`; commit with a message that says the root cause and how the
  fix restores the invariant. Open a PR; keep it to this one fix.
