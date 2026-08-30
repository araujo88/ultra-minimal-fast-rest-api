# CLAUDE.md

Project guidance for Claude Code. The full agent guide — commands, architecture,
and the invariants that must not regress — lives in AGENTS.md; read it first:

@AGENTS.md

## Claude-specific notes

- **Task playbooks** (follow these for the corresponding task):
  - Reviewing a PR → `.cursor/commands/review.md` (adversarial review rubric).
  - Fixing a bug → `.cursor/commands/bugfix.md`.
  - Implementing a feature → `.cursor/commands/feature.md`.
- **Before proposing a change is done**, run the local CI equivalent:
  `make format-check && make cppcheck && make strict && make http-test`, the
  `pytest` suite, and `make valgrind`.
- **Never commit to `main`.** Branch, then open a PR; keep one logical change per
  PR and split unrelated work.
- **Do not weaken a documented invariant** (AGENTS.md “Invariants”) to make a
  change easier. If a change genuinely requires altering one, call it out
  explicitly and back it with tests.
- **Keep comments truthful.** A comment or doc that overstates behavior is
  treated as a bug in this repo.
