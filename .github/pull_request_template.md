<!--
Thanks for contributing. This template mirrors the rules in AGENTS.md / CLAUDE.md.
Delete any section that genuinely does not apply, but do not delete a section just
to avoid filling it in. Keep one logical change per PR — split unrelated work.
-->

## Summary

<!-- What does this change do, and why? One or two sentences. Link the issue it
     closes (e.g. "Closes #123") or the discussion that motivated it. -->

Closes #

## Type of change

<!-- Check all that apply. -->

- [ ] Bug fix (playbook: `.cursor/commands/bugfix.md`)
- [ ] New feature / endpoint (playbook: `.cursor/commands/feature.md`)
- [ ] Performance (include before/after — see the Benchmarks section)
- [ ] Refactor (no behavior change)
- [ ] Docs / comments only
- [ ] Build / CI / tooling
- [ ] Security hardening

## What changed

<!-- The concrete edits, per module where it helps a reviewer. Name the files and
     the reasoning, not just "updated X". Which module owns the concern
     (transport / parsing / routing / response / persistence / thread pool)? -->

-

## Local CI gate

<!-- CI runs these staged (format → static analysis → build+parser tests →
     regression+valgrind). Run the equivalent locally BEFORE opening the PR.
     Paste notable output where it's interesting; check what you actually ran. -->

- [ ] `make format-check`  (clang-format, pinned 15.0.7 in CI)
- [ ] `make cppcheck`
- [ ] `make strict`  (builds `-Wall -Wextra -Wpedantic -Werror`)
- [ ] `make http-test`  (parser unit + fuzz under ASan/UBSan)
- [ ] `cd tests && pytest`  (regression suite)  →  _N_ passed
- [ ] `make valgrind`  (memcheck: 0 errors, no leaks)

<!-- N/A for docs-only PRs; say so rather than leaving it blank. -->

## Tests

<!-- New behavior needs a test. Adversarial coverage (malformed input,
     concurrency, persistence across restart) is expected — a happy-path-only
     test is not sufficient. -->

- [ ] Added/updated tests in `tests/test_regression.py` (and/or `tests/http_smoke.c`)
- [ ] Covered the failure/adversarial cases, not just the happy path
- [ ] N/A and here's why:

Describe the coverage:

-

## Invariants

<!-- The invariants in AGENTS.md ("Invariants") were established by the security
     review; breaking one is a blocking defect. Confirm this PR preserves them,
     or if it MUST change one, say which and prove the new behavior with tests. -->

- [ ] This change preserves all invariants in [AGENTS.md](../blob/main/AGENTS.md#invariants-do-not-regress-these).
- [ ] It intentionally changes an invariant — explained below, with tests:

<!-- Quick reminders of the ones most often at risk:
     1. Framing via recv_request() (never assume one recv == one request)
     2. Structured routing on parsed (method, target), never strstr over raw bytes
     3. Parameterized SQL only (sqlite3_bind_*), identifiers only from models.h
     4. Bounded, escaped JSON via strbuf (overflow → DB_ERROR → 500)
     5. Serialized writers: db_write_lock across step()+sqlite3_changes(); reads lock-free
     6. Bounded thread pool: queue never overwrites unconsumed tasks
     7. Async-signal-safe shutdown: SIGINT handler only sets the flag
     8. Request-local failure: never exit() the process on a bad request
     9. Every fixed buffer bounded (snprintf / bounded loops)
    10. NUM_COLS == rows of TABLE_COLS (_Static_assert)
    11. CRLF HTTP/1.1 with correct Content-Length -->

## Security considerations

<!-- This server targets local dev / mock APIs (see SECURITY.md, AUDIT.md), but
     the input-handling discipline still holds. Fill this in for anything that
     touches parsing, sockets, SQL, auth, the allowlist, or response building. -->

- [ ] No new unbounded copy / format-string / sentinel scan (`strcpy`/`strcat`/`sprintf`/unbounded `strncat` stay out).
- [ ] Untrusted input (request bytes, headers, env vars) stays bounded and validated.
- [ ] No secret, credential, or `BASIC_AUTH` value is logged or echoed.
- [ ] N/A — this PR doesn't touch any of the above.

## Benchmarks

<!-- Required for the "Performance" type; otherwise delete this section.
     See docs/BENCHMARKS.md for method. Be honest about noise — an unmeasurable
     or within-noise result is a valid, useful result. Isolate the change and,
     where the effect is small, run interleaved rounds. -->

- [ ] `make bench` before/after (or a scoped A/B), method noted:

## Docs & comments

- [ ] Comments and docs match actual behavior (a misleading comment is treated as a bug here).
- [ ] Updated `AGENTS.md` / `README.md` / `docs/BENCHMARKS.md` if behavior, commands, or invariants changed.

## Checklist

- [ ] Branched off `main` (no direct commits to `main`); **one logical change** in this PR.
- [ ] Self-reviewed the diff; no stray debug output, dead code, or unrelated churn.
- [ ] CI is green.

---

<sub>Reviews use the adversarial rubric in [`.cursor/commands/review.md`](../blob/main/.cursor/commands/review.md).</sub>
