"""Regression suite for ultra-minimal-fast-rest-api.

Covers the behaviours the old print-only scripts never checked: correct status
codes, JSON validity/escaping, SQL-injection resistance, malformed network
input, TCP fragmentation, concurrency/backpressure, slow clients, and
persistence across a restart.

Run:  cd tests && pip install -r requirements.txt && pytest -v
"""
import json
import socket
import time
from concurrent.futures import ThreadPoolExecutor

import requests

from support import HOST, PORT, raw_send, wait_closed

TIMEOUT = 5


def get_list(base):
    r = requests.get(f"{base}users", timeout=TIMEOUT)
    assert r.status_code == 200
    return r.json()  # raises if the body is not valid JSON


def find_by_name(rows, name):
    return [row for row in rows if row.get("name") == name]


def healthy(base):
    """The server still answers a normal request."""
    r = requests.get(base, timeout=TIMEOUT)
    return r.status_code == 200 and "Hello world" in r.text


# --------------------------------------------------------------------------- #
# CRUD lifecycle
# --------------------------------------------------------------------------- #

class TestCrud:
    def test_root(self, server):
        r = requests.get(server, timeout=TIMEOUT)
        assert r.status_code == 200
        assert "Hello world" in r.text

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
        assert r.status_code == 200
        assert get_list(server) == []
        # The resource is gone now -> 404.
        assert requests.get(f"{server}users/1", timeout=TIMEOUT).status_code == 404


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


# --------------------------------------------------------------------------- #
# Client IP allowlist (ALLOWED_HOSTS env override)
# --------------------------------------------------------------------------- #

class TestAllowlist:
    BASE = f"http://{HOST}:{PORT}/"

    def test_default_allows_localhost(self, server):
        # No ALLOWED_HOSTS set -> compile-time default includes 127.0.0.1.
        assert requests.get(self.BASE, timeout=TIMEOUT).status_code == 200

    def test_restricted_list_forbids_localhost(self, server_manager):
        server_manager(env={"ALLOWED_HOSTS": "10.1.2.3"})
        assert requests.get(self.BASE, timeout=TIMEOUT).status_code == 403

    def test_wildcard_allows_everyone(self, server_manager):
        server_manager(env={"ALLOWED_HOSTS": "*"})
        assert requests.get(self.BASE, timeout=TIMEOUT).status_code == 200

    def test_localhost_in_custom_list_allowed(self, server_manager):
        server_manager(env={"ALLOWED_HOSTS": "10.1.2.3, 127.0.0.1"})
        assert requests.get(self.BASE, timeout=TIMEOUT).status_code == 200

    def test_garbage_only_falls_back_to_default(self, server_manager):
        # No syntactically valid IPv4 -> fall back to the restrictive default
        # (which includes 127.0.0.1) rather than denying everyone.
        server_manager(env={"ALLOWED_HOSTS": "not-an-ip;drop table"})
        assert requests.get(self.BASE, timeout=TIMEOUT).status_code == 200

    def test_invalid_entries_are_ignored_valid_kept(self, server_manager):
        # Garbage tokens are dropped; the valid one still governs. Localhost is
        # not in the list, so it is forbidden.
        server_manager(env={"ALLOWED_HOSTS": "not-an-ip, 999.999.1.1, 10.1.2.3"})
        assert requests.get(self.BASE, timeout=TIMEOUT).status_code == 403


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
