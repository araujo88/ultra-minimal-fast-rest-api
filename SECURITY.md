# Security Policy

## Intended use and threat model

This project is a **mock / development CRUD server**. It is meant to run on a
trusted host (localhost, a container, a private network), not to be exposed
directly to untrusted clients on the public internet.

What it is designed to withstand from a client on the connection:

- Malformed / partial / oversized HTTP requests, TCP fragmentation, missing
  terminators, binary and NUL bytes — these must not crash the server or corrupt
  memory; they get an error response or a dropped connection.
- Hostile CRUD input (quotes, `%xx`, `&`, `=`, control characters, long values,
  SQL/JSON metacharacters) — values are bound into prepared statements and
  JSON-escaped, so they are stored/echoed as data, not executed or able to break
  the response.
- Concurrent clients — a bounded thread pool with a serialized DB write path.

What it deliberately does **not** provide (out of scope):

- Authentication, authorization, sessions, or TLS. The `ALLOWED_HOSTS` IP
  allowlist is a coarse convenience, not an authentication mechanism; it is
  trivially spoofable across untrusted networks and defaults to permissive in
  the Docker image.
- Rate limiting / robust DoS protection. A fixed worker pool with a receive
  timeout bounds slow-client damage but is not a hardened front end. Put a real
  reverse proxy in front if you expose it.
- Multi-tenant isolation or hardening of the SQLite file on disk.

The security review that this codebase was hardened against, including the
findings that were fixed, is in [`AUDIT.md`](AUDIT.md).

## Supported versions

This is a personal/educational project without formal releases. Security fixes
are applied to the `main` branch. There is no support commitment for older
commits.

## Reporting a vulnerability

**Please do not open a public issue for a security vulnerability.**

Report it privately via GitHub's private vulnerability reporting:

- <https://github.com/leo-aa88/ultra-minimal-fast-rest-api/security/advisories/new>

**[Maintainers: optionally add a security contact email here as an alternative.]**

Please include:

- affected file(s)/function(s) and commit,
- a description of the issue and its impact,
- a minimal reproduction (request bytes, input values, or a small script), and
- any suggested remediation.

Because this is a best-effort personal project, response times are not
guaranteed, but reports are welcome and will be addressed as time allows. Please
allow a reasonable window for a fix before any public disclosure.

## Scope

In scope: memory-safety, injection (SQL/response), request-parsing defects,
concurrency/data-integrity bugs, resource-exhaustion issues reachable from a
single connection, and any divergence between a documented invariant
([`AGENTS.md`](AGENTS.md)) and runtime behavior.

Out of scope: the absence of the features listed above (auth, TLS, rate
limiting), issues that require an already-compromised host or control of the
process environment (e.g. setting `ALLOWED_HOSTS`), and findings against a fork
that changed the server core.
