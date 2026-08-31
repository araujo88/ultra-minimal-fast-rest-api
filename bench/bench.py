#!/usr/bin/env python3
"""Dependency-free load generator for ultra-minimal-fast-rest-api.

Two connection models:

  * default (connection-per-request): open a socket, send one request, read the
    response, close. This is how the server was used before keep-alive.
  * --keepalive: open one socket per worker and send many requests over it,
    reusing the connection (HTTP/1.1 keep-alive).

Responses are framed by Content-Length (not by connection close), so the client
is correct under both models.

    python3 bench/bench.py --all --duration 3
    python3 bench/bench.py --all --duration 3 --keepalive
"""
import argparse
import multiprocessing as mp
import socket
import time

SCENARIOS = {
    "livez": b"GET /livez HTTP/1.1\r\nHost: b\r\n\r\n",
    "list": b"GET /users HTTP/1.1\r\nHost: b\r\n\r\n",
    "get_one": b"GET /users/1 HTTP/1.1\r\nHost: b\r\n\r\n",
    "create": (
        b"POST /users HTTP/1.1\r\nHost: b\r\n"
        b"Content-Type: application/x-www-form-urlencoded\r\n"
        b"Content-Length: 33\r\n\r\n"
        b"name=U&surname=S&age=1&height=1.5"
    ),
}


def _read_response(sock, buf):
    """Read exactly one HTTP response. Returns (ok, server_closes, leftover)."""
    while True:
        i = buf.find(b"\r\n\r\n")
        if i != -1:
            header = buf[:i].lower()
            cl = 0
            closes = b"connection: close" in header
            for line in header.split(b"\r\n"):
                if line.startswith(b"content-length:"):
                    cl = int(line.split(b":", 1)[1].strip())
                    break
            total = i + 4 + cl
            if len(buf) >= total:
                return True, closes, buf[total:]
        d = sock.recv(65536)
        if not d:
            return False, True, b""  # connection closed before a full response
        buf += d


def _worker(host, port, payload, deadline, keepalive):
    count = errors = 0
    lat = []
    sock = None
    leftover = b""
    while time.monotonic() < deadline:
        t0 = time.monotonic()
        try:
            if sock is None:
                sock = socket.create_connection((host, port), timeout=5)
                sock.settimeout(5)
                leftover = b""
            sock.sendall(payload)
            ok, server_closes, leftover = _read_response(sock, leftover)
            if not ok:
                raise OSError("closed")
            lat.append(time.monotonic() - t0)
            count += 1
            # Honor the server's decision: it sends Connection: close when it
            # ends a keep-alive connection (e.g. its per-connection cap). That is
            # not an error -- just reconnect for the next request.
            if not keepalive or server_closes:
                sock.close()
                sock = None
        except OSError:
            errors += 1
            if sock is not None:
                sock.close()
                sock = None
    if sock is not None:
        sock.close()
    return count, errors, lat


def run(host, port, scenario, concurrency, duration, keepalive):
    payload = SCENARIOS[scenario]
    deadline = time.monotonic() + duration
    with mp.Pool(concurrency) as pool:
        results = pool.starmap(
            _worker, [(host, port, payload, deadline, keepalive)] * concurrency
        )
    total = sum(r[0] for r in results)
    errors = sum(r[1] for r in results)
    lat = sorted(x for r in results for x in r[2])
    rps = total / duration
    if lat:
        mean = sum(lat) / len(lat) * 1e3
        p50 = lat[int(0.50 * (len(lat) - 1))] * 1e3
        p99 = lat[int(0.99 * (len(lat) - 1))] * 1e3
    else:
        mean = p50 = p99 = float("nan")
    print(
        f"{scenario:<8} c={concurrency:<4} "
        f"{rps:9.0f} req/s   mean {mean:6.2f} ms   "
        f"p50 {p50:6.2f} ms   p99 {p99:7.2f} ms   errors {errors}"
    )
    return rps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=9002)
    ap.add_argument("--scenario", choices=list(SCENARIOS), default="list")
    ap.add_argument("--concurrency", type=int, default=16)
    ap.add_argument("--duration", type=float, default=5.0)
    ap.add_argument("--keepalive", action="store_true", help="reuse one connection per worker")
    ap.add_argument("--all", action="store_true", help="sweep scenarios x concurrency")
    args = ap.parse_args()

    mode = "keep-alive (connection reused)" if args.keepalive else "connection-per-request"
    print(f"Model: {mode}")
    if args.all:
        for scenario in ("livez", "list", "get_one", "create"):
            for c in (1, 8, 16, 32):
                run(args.host, args.port, scenario, c, args.duration, args.keepalive)
            print()
    else:
        run(args.host, args.port, args.scenario, args.concurrency, args.duration, args.keepalive)


if __name__ == "__main__":
    main()
