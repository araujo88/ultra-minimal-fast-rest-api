# Contributing

Thanks for your interest in improving ultra-minimal-fast-rest-api. This is
network-facing C, so correctness, memory safety, and clear invariants matter
more than feature velocity. Please read this before opening a pull request.

By participating you agree to the [Code of Conduct](CODE_OF_CONDUCT.md).

## Prerequisites

- `gcc`, `make`, `libsqlite3-dev`
- For the full local checks: `clang-format`, `cppcheck`, `valgrind`, and Python
  (`pip install -r tests/requirements.txt`)

## Build and run

```bash
make            # build ./server  (make strict for -Werror)
./server        # or ./run_locally.sh
```

## Before you open a PR

Run the local equivalent of CI and make sure it all passes:

```bash
make format-check      # clang-format (run `make format` to auto-apply)
make cppcheck          # static analysis
make strict            # build with -Werror
make http-test         # parser unit tests + fuzz under ASan/UBSan
cd tests && pytest -v  # regression suite
cd .. && make valgrind # runtime memory check
```

CI ([`.github/workflows/ci.yml`](.github/workflows/ci.yml)) runs the same stages
(lint → static analysis → build → tests + valgrind) and must be green.

## Ground rules

1. **Branch off `main`; never push to `main` directly.** One logical change per
   PR — split unrelated changes into separate PRs.
2. **Don't regress an invariant.** [`AGENTS.md`](AGENTS.md) lists the invariants
   this codebase depends on (request framing, parameterized SQL, bounded/escaped
   JSON, serialized DB writers, async-signal-safe shutdown, bounded buffers,
   etc.). If a change genuinely requires altering one, say so explicitly in the
   PR description and prove the new behavior with tests.
3. **Add tests for new behavior**, including adversarial cases (malformed input,
   concurrency, persistence). A happy-path-only test is not enough — see the
   existing `tests/` suite for the expected bar.
4. **Keep comments and docs truthful.** A comment that overstates behavior is
   treated as a bug.
5. **Match the existing style** (Allman braces, 4-space indent) — `make format`
   applies it. Keep the tree warning-clean under `-Werror` and clean under
   ASan/UBSan/Valgrind.

## Changing the data model

The model is compile-time (`include/models.h`). Edit `TABLE_NAME` / `TABLE_COLS`
and keep `NUM_COLS` in sync (a `_Static_assert` enforces it). See the README's
"Defining your model".

## Pull request checklist

- [ ] Branched off `main`, single logical change
- [ ] `make format-check`, `make cppcheck`, `make strict` pass
- [ ] `make http-test` and `make valgrind` pass
- [ ] `pytest` suite passes; new/changed behavior has tests (incl. adversarial)
- [ ] No invariant weakened without explicit justification
- [ ] README / AGENTS updated if behavior or contracts changed

## Reporting security issues

Do **not** open a public issue for vulnerabilities. Follow
[SECURITY.md](SECURITY.md).
