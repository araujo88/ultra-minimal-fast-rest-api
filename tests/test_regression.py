"""Regression suite for ultra-minimal-fast-rest-api.

Covers the behaviours the old print-only scripts never checked: correct status
codes, JSON validity/escaping, SQL-injection resistance, malformed network
input, TCP fragmentation, concurrency/backpressure, slow clients, and
persistence across a restart.

Run:  cd tests && pip install -r requirements.txt && pytest -v
"""
import json
import socket
import subprocess
import time
from concurrent.futures import ThreadPoolExecutor

import requests

from support import HOST, PORT, REPO_ROOT, SERVER_BIN, raw_send, wait_closed

TIMEOUT = 5


def get_list(base):
    r = requests.get(f"{base}users", timeout=TIMEOUT)
    assert r.status_code == 200
    return r.json()  # raises if the body is not valid JSON


def find_by_name(rows, name):
    return [row for row in rows if row.get("name") == name]


def healthy(base):
    """The server still answers a normal request (liveness probe)."""
    r = requests.get(f"{base}livez", timeout=TIMEOUT)
    return r.status_code == 200


# --------------------------------------------------------------------------- #
# CRUD lifecycle
# --------------------------------------------------------------------------- #

class TestCrud:
    def test_empty_list_is_valid_json_array(self, server):
        assert get_list(server) == []

    def test_create_read(self, server):
        payload = {"name": "Giga", "surname": "Chad", "age": 29, "height": 1.80}
        r = requests.post(f"{server}users", data=payload, timeout=TIMEOUT)
        assert r.status_code == 201
        assert r.json()["msg"] == "success"

        rows = get_list(server)
        assert len(rows) == 1
        row = rows[0]
        assert row["name"] == "Giga"
        assert row["surname"] == "Chad"
        assert row["age"] == 29          # numeric, unquoted in JSON
        assert abs(row["height"] - 1.80) < 1e-9
        assert row["Id"] == 1

        one = requests.get(f"{server}users/1", timeout=TIMEOUT).json()
        assert one["name"] == "Giga"

    def test_update(self, server):
        requests.post(f"{server}users",
                      data={"name": "A", "surname": "B", "age": 1, "height": 1.0},
                      timeout=TIMEOUT)
        r = requests.put(f"{server}users/1",
                         data={"name": "John", "surname": "Smith", "age": 33, "height": 1.77},
                         timeout=TIMEOUT)
        assert r.status_code == 200
        one = requests.get(f"{server}users/1", timeout=TIMEOUT).json()
        assert one["name"] == "John"
        assert one["age"] == 33

    def test_delete(self, server):
        requests.post(f"{server}users",
                      data={"name": "A", "surname": "B", "age": 1, "height": 1.0},
                      timeout=TIMEOUT)
        r = requests.delete(f"{server}users/1", timeout=TIMEOUT)
        assert r.status_code == 204          # No Content
        assert r.content == b""              # 204 carries no body
        assert "Content-Length" not in r.headers  # ... and no Content-Length
        assert get_list(server) == []
        # The resource is gone now -> 404.
        assert requests.get(f"{server}users/1", timeout=TIMEOUT).status_code == 404


# --------------------------------------------------------------------------- #
# Prepared-statement reuse: the write statements (INSERT/UPDATE/DELETE) are
# compiled once and reused across requests (reset + clear_bindings between
# calls). These exercise that reuse with distinct payloads so a stale/leaked
# binding, or a botched reset, would show up as cross-contamination between rows.
# --------------------------------------------------------------------------- #

class TestStatementReuse:
    def test_repeated_inserts_keep_distinct_values(self, server):
        people = [
            {"name": f"n{i}", "surname": f"s{i}", "age": i, "height": 1.0 + i / 100}
            for i in range(1, 11)
        ]
        for p in people:
            assert requests.post(f"{server}users", data=p, timeout=TIMEOUT).status_code == 201

        rows = get_list(server)
        assert len(rows) == 10
        by_id = {row["Id"]: row for row in rows}
        for i, p in enumerate(people, start=1):
            row = by_id[i]  # ids are assigned 1..10 in insertion order
            assert row["name"] == p["name"]
            assert row["surname"] == p["surname"]
            assert row["age"] == p["age"]
            assert abs(row["height"] - p["height"]) < 1e-9

    def test_repeated_updates_are_independent(self, server):
        for _ in range(5):
            requests.post(f"{server}users",
                          data={"name": "x", "surname": "y", "age": 0, "height": 1.0},
                          timeout=TIMEOUT)
        # Update each row to a distinct value over the reused UPDATE statement.
        for i in range(1, 6):
            r = requests.put(f"{server}users/{i}",
                             data={"name": f"u{i}", "surname": f"v{i}", "age": i * 10, "height": 2.0 + i},
                             timeout=TIMEOUT)
            assert r.status_code == 200
        for i in range(1, 6):
            one = requests.get(f"{server}users/{i}", timeout=TIMEOUT).json()
            assert one["name"] == f"u{i}"
            assert one["age"] == i * 10

    def test_repeated_deletes_report_correct_change_count(self, server):
        for _ in range(5):
            requests.post(f"{server}users",
                          data={"name": "x", "surname": "y", "age": 0, "height": 1.0},
                          timeout=TIMEOUT)
        # Delete an existing row (204), then re-delete the same id (404): the
        # reused DELETE statement must still report changes correctly after reset.
        assert requests.delete(f"{server}users/3", timeout=TIMEOUT).status_code == 204
        assert requests.delete(f"{server}users/3", timeout=TIMEOUT).status_code == 404
        # A non-existent id over the reused statement is still a 404, not a 204.
        assert requests.delete(f"{server}users/999", timeout=TIMEOUT).status_code == 404
        remaining = sorted(row["Id"] for row in get_list(server))
        assert remaining == [1, 2, 4, 5]


# --------------------------------------------------------------------------- #
# Health / liveness / readiness endpoints
# --------------------------------------------------------------------------- #

class TestHealth:
    def test_root_removed_is_404(self, server):
        assert requests.get(server, timeout=TIMEOUT).status_code == 404

    def test_livez(self, server):
        r = requests.get(f"{server}livez", timeout=TIMEOUT)
        assert r.status_code == 200
        assert r.json()["status"] == "ok"

    def test_readyz(self, server):
        r = requests.get(f"{server}readyz", timeout=TIMEOUT)
        assert r.status_code == 200
        assert r.json()["status"] == "ok"

    def test_health(self, server):
        r = requests.get(f"{server}health", timeout=TIMEOUT)
        assert r.status_code == 200
        assert r.json()["status"] == "ok"

    def test_health_wrong_method_405(self, server):
        assert requests.post(f"{server}livez", timeout=TIMEOUT).status_code == 405


# --------------------------------------------------------------------------- #
# HTTP status codes
# --------------------------------------------------------------------------- #

class TestStatusCodes:
    def test_non_numeric_id_is_400(self, server):
        assert requests.get(f"{server}users/abc", timeout=TIMEOUT).status_code == 400

    def test_unknown_route_is_404(self, server):
        assert requests.get(f"{server}nope", timeout=TIMEOUT).status_code == 404

    def test_method_not_allowed_is_405(self, server):
        # DELETE/PUT on the collection have no handler -> 405
        assert requests.delete(f"{server}users", timeout=TIMEOUT).status_code == 405
        assert requests.put(f"{server}users", data={"x": "1"}, timeout=TIMEOUT).status_code == 405

    def test_get_missing_id_is_404(self, server):
        r = requests.get(f"{server}users/99999", timeout=TIMEOUT)
        assert r.status_code == 404
        assert r.json()["msg"]  # valid JSON error body

    def test_update_missing_id_is_404(self, server):
        r = requests.put(f"{server}users/99999",
                         data={"name": "X", "surname": "Y", "age": 2, "height": 1.1},
                         timeout=TIMEOUT)
        assert r.status_code == 404

    def test_delete_missing_id_is_404(self, server):
        assert requests.delete(f"{server}users/99999", timeout=TIMEOUT).status_code == 404

    def test_update_existing_id_is_200(self, server):
        requests.post(f"{server}users",
                      data={"name": "A", "surname": "B", "age": 1, "height": 1.0},
                      timeout=TIMEOUT)
        assert requests.put(f"{server}users/1",
                            data={"name": "C", "surname": "D", "age": 2, "height": 1.2},
                            timeout=TIMEOUT).status_code == 200


# --------------------------------------------------------------------------- #
# JSON validity and escaping (hostile database contents)
# --------------------------------------------------------------------------- #

class TestJsonEscaping:
    HOSTILE = [
        'Alice "The Destroyer"',   # embedded double quotes
        r"C:\tmp\foo",             # backslashes
        "line1\nline2\ttab",       # control characters
        "unicode: café ☕",         # multibyte UTF-8
    ]

    def test_hostile_values_roundtrip_as_valid_json(self, server):
        for i, name in enumerate(self.HOSTILE):
            requests.post(f"{server}users",
                          data={"name": name, "surname": "X", "age": i, "height": 1.0},
                          timeout=TIMEOUT)
        # get_list() parses the body; invalid JSON would raise here.
        rows = get_list(server)
        got = sorted(row["name"] for row in rows)
        assert got == sorted(self.HOSTILE)

    def test_oversized_list_fails_closed_not_truncated(self, server):
        # The response buffer is fixed. A list that would overflow it must NOT
        # come back as a 200 carrying a truncated, unparseable array. The
        # serializer fails closed: a clean 500 with a valid JSON error body.
        for i in range(150):  # well past the ~62-row buffer limit
            requests.post(f"{server}users",
                          data={"name": f"User{i}", "surname": "Smith", "age": i, "height": 1.5},
                          timeout=TIMEOUT)
        r = requests.get(f"{server}users", timeout=TIMEOUT)
        assert r.status_code == 500
        assert r.json()["msg"]  # valid JSON error object, not a severed array
        assert healthy(server)  # and the server keeps serving afterwards

    def test_nan_inf_hex_float_round_trip_as_quoted_strings(self, server):
        """Issue #41: strtod() accepted nan/inf/hex-float as JSON numbers and
        emitted them bare (unquoted), producing a response that fails to
        parse as JSON. Values strtod() accepts but JSON does not must come
        back quoted instead, and genuine numbers must still be emitted bare.
        """
        requests.post(f"{server}users",
                    data={"name": "nan-case", "surname": "X", "age": "nan", "height": "inf"},
                    timeout=TIMEOUT)
        requests.post(f"{server}users",
                    data={"name": "hex-case", "surname": "X", "age": 1, "height": "0x1p4"},
                    timeout=TIMEOUT)

        # get_list() calls .json(), which raises if the body fails to parse --
        # this alone reproduces issue #41's JSONDecodeError before the fix.
        rows = get_list(server)

        nan_row = find_by_name(rows, "nan-case")[0]
        assert nan_row["age"] == "nan"       # quoted string, not a bare token
        assert nan_row["height"] == "inf"

        hex_row = find_by_name(rows, "hex-case")[0]
        assert hex_row["height"] == "0x1p4"  # quoted string, not a bare token
        assert hex_row["age"] == 1           # genuine number, still bare/unaffected


# --------------------------------------------------------------------------- #
# Client IP allowlist (ALLOWED_HOSTS env override)
# --------------------------------------------------------------------------- #

class TestAllowlist:
    BASE = f"http://{HOST}:{PORT}/"

    def test_default_allows_localhost(self, server):
        # No ALLOWED_HOSTS set -> compile-time default includes 127.0.0.1.
        assert requests.get(f"{self.BASE}livez", timeout=TIMEOUT).status_code == 200

    def test_restricted_list_forbids_localhost(self, server_manager):
        server_manager(env={"ALLOWED_HOSTS": "10.1.2.3"})
        assert requests.get(f"{self.BASE}livez", timeout=TIMEOUT).status_code == 403

    def test_wildcard_allows_everyone(self, server_manager):
        server_manager(env={"ALLOWED_HOSTS": "*"})
        assert requests.get(f"{self.BASE}livez", timeout=TIMEOUT).status_code == 200

    def test_localhost_in_custom_list_allowed(self, server_manager):
        server_manager(env={"ALLOWED_HOSTS": "10.1.2.3, 127.0.0.1"})
        assert requests.get(f"{self.BASE}livez", timeout=TIMEOUT).status_code == 200

    def test_garbage_only_falls_back_to_default(self, server_manager):
        # No syntactically valid IPv4 -> fall back to the restrictive default
        # (which includes 127.0.0.1) rather than denying everyone.
        server_manager(env={"ALLOWED_HOSTS": "not-an-ip;drop table"})
        assert requests.get(f"{self.BASE}livez", timeout=TIMEOUT).status_code == 200

    def test_invalid_entries_are_ignored_valid_kept(self, server_manager):
        # Garbage tokens are dropped; the valid one still governs. Localhost is
        # not in the list, so it is forbidden.
        server_manager(env={"ALLOWED_HOSTS": "not-an-ip, 999.999.1.1, 10.1.2.3"})
        assert requests.get(f"{self.BASE}livez", timeout=TIMEOUT).status_code == 403


# --------------------------------------------------------------------------- #
# Runtime configuration (CLI flags + env vars)
# --------------------------------------------------------------------------- #

class TestConfig:
    def test_port_via_env(self, server_manager):
        server_manager(env={"PORT": "9010"}, port=9010)
        assert requests.get("http://127.0.0.1:9010/livez", timeout=TIMEOUT).status_code == 200

    def test_port_via_cli_flag(self, server_manager):
        server_manager(args=["--port", "9011"], port=9011)
        assert requests.get("http://127.0.0.1:9011/livez", timeout=TIMEOUT).status_code == 200

    def test_cli_flag_overrides_env(self, server_manager):
        # Env sets 9010, CLI sets 9012; the CLI flag must win.
        server_manager(env={"PORT": "9010"}, args=["--port", "9012"], port=9012)
        assert requests.get("http://127.0.0.1:9012/livez", timeout=TIMEOUT).status_code == 200
        assert wait_closed(0.5, port=9010)  # nothing bound on the (overridden) env port

    def test_thread_count_via_env(self, server_manager):
        server_manager(env={"THREADS": "2"})  # non-default worker count still serves
        assert requests.get(f"http://{HOST}:{PORT}/livez", timeout=TIMEOUT).status_code == 200

    def test_high_thread_count_scales(self, server_manager):
        # Small worker stacks let --threads reach the thousands without
        # exhausting address space; the server must still start and serve.
        server_manager(args=["--threads", "2000"])
        assert requests.get(f"http://{HOST}:{PORT}/livez", timeout=TIMEOUT).status_code == 200

    def test_invalid_port_flag_exits_nonzero(self):
        r = subprocess.run([SERVER_BIN, "--port", "70000"], cwd=REPO_ROOT,
                           capture_output=True, text=True, timeout=10)
        assert r.returncode != 0
        assert "Invalid --port" in r.stderr

    def test_version_flag(self):
        r = subprocess.run([SERVER_BIN, "--version"], cwd=REPO_ROOT,
                           capture_output=True, text=True, timeout=10)
        assert r.returncode == 0
        assert r.stdout.strip()  # prints a version string ("dev" for local builds)


# --------------------------------------------------------------------------- #
# Optional HTTP Basic auth (BASIC_AUTH env)
# --------------------------------------------------------------------------- #

class TestBasicAuth:
    BASE = f"http://{HOST}:{PORT}/"
    AUTH = {"BASIC_AUTH": "user:pass"}

    def test_no_credentials_is_401(self, server_manager):
        server_manager(env=self.AUTH)
        r = requests.get(f"{self.BASE}users", timeout=TIMEOUT)
        assert r.status_code == 401
        assert r.headers.get("WWW-Authenticate", "").startswith("Basic")

    def test_wrong_credentials_is_401(self, server_manager):
        server_manager(env=self.AUTH)
        r = requests.get(f"{self.BASE}users", auth=("user", "nope"), timeout=TIMEOUT)
        assert r.status_code == 401

    def test_correct_credentials_ok(self, server_manager):
        server_manager(env=self.AUTH)
        r = requests.get(f"{self.BASE}users", auth=("user", "pass"), timeout=TIMEOUT)
        assert r.status_code == 200
        assert r.json() == []

    def test_disabled_by_default(self, server):
        # No BASIC_AUTH env -> auth off, no credentials needed.
        assert requests.get(f"{server}users", timeout=TIMEOUT).status_code == 200

    def test_health_endpoints_are_exempt(self, server_manager):
        # Orchestrators probe health without credentials, so those endpoints
        # must stay reachable even when auth is on; /users still requires it.
        server_manager(env=self.AUTH)
        assert requests.get(f"{self.BASE}livez", timeout=TIMEOUT).status_code == 200
        assert requests.get(f"{self.BASE}readyz", timeout=TIMEOUT).status_code == 200
        assert requests.get(f"{self.BASE}users", timeout=TIMEOUT).status_code == 401


# --------------------------------------------------------------------------- #
# SQL injection resistance
# --------------------------------------------------------------------------- #

class TestSqlInjection:
    def test_injection_is_stored_as_literal(self, server):
        payload = "Robert'); DROP TABLE users;--"
        requests.post(f"{server}users",
                      data={"name": payload, "surname": "Y", "age": 1, "height": 1.0},
                      timeout=TIMEOUT)
        # If the injection executed, the table would be gone and this would 500/crash.
        rows = get_list(server)
        assert len(find_by_name(rows, payload)) == 1

        # Table is intact: a second insert still works and both rows are present.
        requests.post(f"{server}users",
                      data={"name": "after", "surname": "Z", "age": 2, "height": 1.0},
                      timeout=TIMEOUT)
        assert len(get_list(server)) == 2


# --------------------------------------------------------------------------- #
# Malformed / adversarial network input must never crash the server
# --------------------------------------------------------------------------- #

class TestMalformedInput:
    PAYLOADS = {
        "no_newline_garbage": b"GARBAGE-NO-NEWLINE-" * 4,
        "no_H_long_path": b"GET /users/" + b"1" * 600,      # would overflow id[256] pre-fix
        "null_bytes": b"\x00\x01\x02\x03 /users HTTP/1.1\r\n\r\n",
        "huge_id": b"GET /users/999999999999999999999999 HTTP/1.1\r\n\r\n",
        "bare_crlf": b"\r\n\r\n",
        "method_only": b"GET",
    }

    def test_malformed_requests_do_not_crash(self, server):
        for label, payload in self.PAYLOADS.items():
            raw_send(payload)  # must not hang/crash
            assert healthy(server), f"server unhealthy after: {label}"

    def test_immediate_disconnect(self, server):
        s = socket.create_connection((HOST, PORT), timeout=TIMEOUT)
        s.close()  # connect then drop before sending anything
        assert healthy(server)

    def test_oversized_request_line(self, server):
        # ~64 KB request target, far beyond the 8 KB read buffer.
        raw_send(b"GET /" + b"A" * 65000 + b" HTTP/1.1\r\n\r\n")
        assert healthy(server)


# --------------------------------------------------------------------------- #
# TCP framing: a request is not one recv()
# --------------------------------------------------------------------------- #

class TestFraming:
    def test_fragmented_request_is_reassembled(self, server):
        body = b"name=Frag&surname=Ment&age=7&height=1.5"
        head = (
            b"POST /users HTTP/1.1\r\n"
            b"Host: x\r\n"
            b"Connection: close\r\n"  # so the server closes after one response
            b"Content-Type: application/x-www-form-urlencoded\r\n"
            b"Content-Length: %d\r\n\r\n" % len(body)
        )
        s = socket.create_connection((HOST, PORT), timeout=TIMEOUT)
        try:
            # Send headers, then the body split into single bytes with delays.
            s.sendall(head)
            time.sleep(0.2)
            for byte in (body[i:i + 1] for i in range(len(body))):
                s.sendall(byte)
                time.sleep(0.005)
            resp = b""
            s.settimeout(TIMEOUT)
            while True:
                d = s.recv(4096)
                if not d:
                    break
                resp += d
        finally:
            s.close()
        assert b"201 Created" in resp
        rows = get_list(server)
        assert len(find_by_name(rows, "Frag")) == 1

    def test_url_encoded_values_are_decoded(self, server):
        # '+' -> space, %26 -> '&', %3D -> '='
        requests.post(f"{server}users",
                      data={"name": "a b&c=d", "surname": "S", "age": 1, "height": 1.0},
                      timeout=TIMEOUT)
        rows = get_list(server)
        assert len(find_by_name(rows, "a b&c=d")) == 1


# --------------------------------------------------------------------------- #
# HTTP/1.1 keep-alive (persistent + pipelined connections)
# --------------------------------------------------------------------------- #

class TestKeepAlive:
    def test_pipelined_requests_reuse_one_connection(self, server):
        requests.post(f"{server}users",
                      data={"name": "A", "surname": "B", "age": 1, "height": 1.0},
                      timeout=TIMEOUT)
        s = socket.create_connection((HOST, PORT), timeout=TIMEOUT)
        s.settimeout(TIMEOUT)
        try:
            # Two requests sent back-to-back on one connection, before reading.
            s.sendall(b"GET /users/1 HTTP/1.1\r\nHost: x\r\n\r\n"
                      b"GET /livez HTTP/1.1\r\nHost: x\r\n\r\n")
            data = b""
            while data.count(b"HTTP/1.1 200") < 2:
                d = s.recv(4096)
                if not d:
                    break
                data += d
        finally:
            s.close()
        assert data.count(b"HTTP/1.1 200") == 2  # both served on one connection
        assert b"Connection: keep-alive" in data

    def test_pipelined_post_body_is_isolated_from_next_request(self, server):
        # The subtle framing invariant: a pipelined POST's body must be bounded
        # by its Content-Length, not bleed into the request queued right after
        # it. Send POST /users (with body) + a Connection: close request in a
        # single write, then assert the stored row equals the POST body exactly
        # -- if the body were mis-bounded, the trailing bytes of the next request
        # would contaminate the last field (height would become a string).
        body = b"name=Pipe&surname=Lined&age=5&height=1.5"
        post = (b"POST /users HTTP/1.1\r\nHost: x\r\n"
                b"Content-Type: application/x-www-form-urlencoded\r\n"
                b"Content-Length: %d\r\n\r\n" % len(body)) + body
        nxt = b"GET /livez HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"

        s = socket.create_connection((HOST, PORT), timeout=TIMEOUT)
        s.settimeout(TIMEOUT)
        data = b""
        try:
            s.sendall(post + nxt)  # both requests in one write -> pipelined
            while True:
                d = s.recv(4096)
                if not d:
                    break
                data += d
        finally:
            s.close()
        assert b"HTTP/1.1 201 Created" in data  # POST served
        assert b"HTTP/1.1 200 OK" in data        # following request served

        rows = get_list(server)
        assert len(rows) == 1
        r = rows[0]
        assert r["name"] == "Pipe"
        assert r["surname"] == "Lined"
        assert r["age"] == 5
        # height stays a JSON number 1.5; body-bleed would make it a string.
        assert isinstance(r["height"], (int, float)) and abs(r["height"] - 1.5) < 1e-9

    def test_204_delete_preserves_keep_alive(self, server):
        # A 204 has no Content-Length; it must stay self-delimiting so the next
        # pipelined request on the same connection is still framed correctly.
        requests.post(f"{server}users",
                      data={"name": "A", "surname": "B", "age": 1, "height": 1.0},
                      timeout=TIMEOUT)
        s = socket.create_connection((HOST, PORT), timeout=TIMEOUT)
        s.settimeout(TIMEOUT)
        data = b""
        try:
            s.sendall(b"DELETE /users/1 HTTP/1.1\r\nHost: x\r\n\r\n"
                      b"GET /livez HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
            while True:
                d = s.recv(4096)
                if not d:
                    break
                data += d
        finally:
            s.close()
        assert b"204 No Content" in data
        assert b"200 OK" in data  # following request served on the same connection

    def test_connection_close_is_honored(self, server):
        s = socket.create_connection((HOST, PORT), timeout=TIMEOUT)
        s.settimeout(TIMEOUT)
        data = b""
        try:
            s.sendall(b"GET /livez HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
            while True:  # server must close after this response
                d = s.recv(4096)
                if not d:
                    break
                data += d
        finally:
            s.close()
        assert b"HTTP/1.1 200" in data
        assert b"Connection: close" in data

    def test_http10_defaults_to_close(self, server):
        s = socket.create_connection((HOST, PORT), timeout=TIMEOUT)
        s.settimeout(TIMEOUT)
        data = b""
        try:
            s.sendall(b"GET /livez HTTP/1.0\r\nHost: x\r\n\r\n")
            while True:  # HTTP/1.0 defaults to close
                d = s.recv(4096)
                if not d:
                    break
                data += d
        finally:
            s.close()
        assert b"200" in data
        assert b"Connection: close" in data


# --------------------------------------------------------------------------- #
# Concurrency: backpressure (queue 8 / 8 workers) must not drop or corrupt
# --------------------------------------------------------------------------- #

class TestConcurrency:
    def test_many_concurrent_writes_all_persist(self, server):
        N = 60  # far exceeds the 8-slot queue -> exercises backpressure

        def post(i):
            r = requests.post(f"{server}users",
                              data={"name": f"U{i}", "surname": "S", "age": i, "height": 1.5},
                              timeout=TIMEOUT)
            return r.status_code

        with ThreadPoolExecutor(max_workers=40) as pool:
            codes = list(pool.map(post, range(N)))

        assert all(c == 201 for c in codes), f"non-201 responses: {set(codes)}"
        assert len(get_list(server)) == N  # no dropped/overwritten requests

    def test_concurrent_mixed_writes_report_correct_status(self, server):
        # Regression for the sqlite3_changes() race: writes to an existing id and
        # to a missing id run concurrently over the shared connection. Each must
        # report its own outcome (200 vs 404), never the other writer's change
        # count. Deterministic once the writers are serialized.
        requests.post(f"{server}users",
                      data={"name": "A", "surname": "B", "age": 1, "height": 1.0},
                      timeout=TIMEOUT)
        N = 1000
        body = {"name": "C", "surname": "D", "age": 2, "height": 1.2}

        def one(i):
            if i % 2 == 0:
                r = requests.put(f"{server}users/1", data=body, timeout=TIMEOUT)
                return ("exist", r.status_code)
            r = requests.put(f"{server}users/999999", data=body, timeout=TIMEOUT)
            return ("missing", r.status_code)

        with ThreadPoolExecutor(max_workers=32) as pool:
            results = list(pool.map(one, range(N)))

        bad = [r for r in results
               if (r[0] == "exist" and r[1] != 200) or (r[0] == "missing" and r[1] != 404)]
        assert not bad, f"incorrect statuses under concurrency: {bad[:10]}"

    def test_one_slow_client_does_not_block_others(self, server):
        # With spare workers available, a single stalled client must not delay
        # a normal request served by another worker.
        slow = socket.create_connection((HOST, PORT), timeout=TIMEOUT)
        try:
            slow.sendall(b"GET /users HT")  # incomplete, never finished
            start = time.time()
            assert healthy(server)
            assert time.time() - start < 3.0
        finally:
            slow.close()

    # Pool is 8 workers / 8-slot queue (main.c: thread_pool_create(8, 8)) and
    # the worker read timeout is SO_RCVTIMEO = 10s (server.c).
    POOL_WORKERS = 8
    RECV_TIMEOUT = 10.0

    def test_saturated_pool_recovers_within_recv_timeout(self, server):
        # Occupy *every* worker with a stalled, never-terminated request. Unlike
        # the single-client case above, there is now no spare capacity: no worker
        # can service a new request until one of them hits SO_RCVTIMEO. The
        # contract under test is bounded recovery -- the pool must not be wedged
        # forever -- so a fresh request must succeed within RCVTIMEO + margin.
        stalled = []
        try:
            for _ in range(self.POOL_WORKERS):
                s = socket.create_connection((HOST, PORT), timeout=TIMEOUT)
                s.sendall(b"GET /users HT")  # incomplete -> holds a worker ~10s
                stalled.append(s)
            time.sleep(0.5)  # let all stalled connections get picked up

            deadline = time.time() + self.RECV_TIMEOUT + 4.0
            served = False
            while time.time() < deadline:
                try:
                    if requests.get(f"{server}users", timeout=2).status_code == 200:
                        served = True
                        break
                except requests.RequestException:
                    pass  # workers still blocked; retry until the deadline
            assert served, "slow clients wedged every worker; pool never recovered"
        finally:
            for s in stalled:
                s.close()


# --------------------------------------------------------------------------- #
# Persistence across a restart (must NOT drop the table on startup)
# --------------------------------------------------------------------------- #

class TestPersistence:
    def test_data_survives_restart(self, server_manager):
        base = f"http://{HOST}:{PORT}/"

        s1 = server_manager(clean_db=True)
        requests.post(f"{base}users",
                      data={"name": "Persist", "surname": "Me", "age": 1, "height": 1.0},
                      timeout=TIMEOUT)
        s1.stop()
        assert wait_closed(5.0)

        server_manager(clean_db=False)  # reopen the same database file
        rows = requests.get(f"{base}users", timeout=TIMEOUT).json()
        assert len(find_by_name(rows, "Persist")) == 1
