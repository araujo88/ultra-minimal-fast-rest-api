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
        assert requests.get(f"{server}users/1", timeout=TIMEOUT).json() == {}


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

    def test_missing_id_returns_empty_object(self, server):
        r = requests.get(f"{server}users/99999", timeout=TIMEOUT)
        assert r.status_code == 200
        assert r.json() == {}


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

    def test_slow_client_does_not_wedge_pool(self, server):
        # Hold one connection open with a partial request (no terminator)...
        slow = socket.create_connection((HOST, PORT), timeout=TIMEOUT)
        try:
            slow.sendall(b"GET /users HT")  # incomplete, never finished
            # ...a normal request must still be served promptly by another worker.
            start = time.time()
            assert healthy(server)
            assert time.time() - start < 3.0
        finally:
            slow.close()


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
