#!/usr/bin/env python3
"""Dependency-free load generator for ultra-minimal-fast-rest-api.

The server closes the connection after each response (no HTTP keep-alive), so
this benchmarks the real thing: one TCP connection per request. Each worker
process runs a sequential connection-per-request loop for the duration; N
workers give N concurrent in-flight requests.

Usage:
    python3 bench/bench.py --scenario list --concurrency 16 --duration 5
    python3 bench/bench.py --all            # sweep the standard scenarios/levels

Reports requests/sec, mean and p50/p99 latency, and error count.
"""
import argparse
import multiprocessing as mp
import socket
import time

SCENARIOS = {
    "root": b"GET / HTTP/1.1\r\nHost: b\r\n\r\n",
    "list": b"GET /users HTTP/1.1\r\nHost: b\r\n\r\n",
    "get_one": b"GET /users/1 HTTP/1.1\r\nHost: b\r\n\r\n",
    "create": (
        b"POST /users HTTP/1.1\r\nHost: b\r\n"
        b"Content-Type: application/x-www-form-urlencoded\r\n"
        b"Content-Length: 33\r\n\r\n"
        b"name=U&surname=S&age=1&height=1.5"
    ),
}


def _worker(host, port, payload, deadline):
    count = 0
    errors = 0
    lat = []
    while time.monotonic() < deadline:
        t0 = time.monotonic()
        try:
            s = socket.create_connection((host, port), timeout=5)
            s.sendall(payload)
            # Read until the server closes the connection (one response).
            while s.recv(65536):
                pass
            s.close()
            lat.append(time.monotonic() - t0)
            count += 1
        except OSError:
            errors += 1
    return count, errors, lat


def run(host, port, scenario, concurrency, duration):
    payload = SCENARIOS[scenario]
    deadline = time.monotonic() + duration
    with mp.Pool(concurrency) as pool:
        results = pool.starmap(
            _worker, [(host, port, payload, deadline)] * concurrency
        )
    total = sum(r[0] for r in results)
    errors = sum(r[1] for r in results)
    lat = sorted(l for r in results for l in r[2])
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
    ap.add_argument("--all", action="store_true", help="sweep scenarios x concurrency")
    args = ap.parse_args()

    if args.all:
        for scenario in ("root", "list", "get_one", "create"):
            for c in (1, 8, 16, 32):
                run(args.host, args.port, scenario, c, args.duration)
            print()
    else:
        run(args.host, args.port, args.scenario, args.concurrency, args.duration)


if __name__ == "__main__":
    main()
