# Benchmarks

Performance measurements for ultra-minimal-fast-rest-api, how to reproduce them,
and what they mean. **Absolute numbers are hardware-dependent — treat the
*shape* and *ratios* as the takeaway, not the exact figures.** All numbers below
were taken on the development machine (WSL2, 16 vCPU) and will differ on yours.

## Reproducing

```bash
make bench          # builds nothing extra; drives the running server
```

`make bench` (via `bench/run_bench.sh`) starts the server, seeds ~40 rows, and
runs `bench/bench.py` — a dependency-free load generator — over two connection
models and a sweep of concurrency levels. The client frames responses by
`Content-Length` (not by connection close), so it is correct with and without
keep-alive.

Scenarios: `livez` (`GET /livez`, no DB), `list` (`GET /users`), `get_one`
(`GET /users/1`), `create` (`POST /users`). Concurrency `c` = number of
concurrent client connections (one worker process each). Each cell reports
throughput (req/s) and the latency distribution — **p50, p90, p95, p99, and
max** — because the mean hides the tail that matters.

## 1. Connection model: per-request vs keep-alive

The server closed the connection after each response until HTTP/1.1 keep-alive
was added. Keep-alive reuses one connection for many requests, removing
per-request TCP setup.

| Scenario            | per-request @ c=1 | keep-alive @ c=1 | per-request @ c=16 | keep-alive @ c=16 |
| ------------------- | ----------------: | ---------------: | -----------------: | ----------------: |
| `GET /livez` (no DB)|            ~9,900 |          ~24,000 |            ~16,700 |           ~40,000 |
| `GET /users` (list) |            ~6,700 |          ~10,600 |            ~14,500 |           ~13,700 |
| `GET /users/1`      |            ~8,100 |          ~19,500 |            ~15,200 |           ~23,700 |
| `POST /users`       |            ~6,400 |          ~12,300 |            ~14,300 |           ~15,200 |

*(requests/sec; higher is better)*

**Takeaway:** keep-alive helps most for single/low-concurrency clients and cheap
endpoints (`GET /livez`, `GET /users/1` ≈ 2.4× at c=1), where connection setup
is a large fraction of the work. At higher concurrency the worker pool and the
database become the ceiling, so the gain shrinks.

## 2. Latency distribution

Throughput alone is misleading — a mean of ~1 ms can hide a 1-second worst case.
Latency percentiles at c=16 (milliseconds):

| Scenario | model         |  p50 |  p90 |  p95 |   p99 |     max |
| -------- | ------------- | ---: | ---: | ---: | ----: | ------: |
| `get_one`| per-request   | 0.75 | 1.37 | 2.39 |  3.20 | ~1086   |
| `get_one`| keep-alive    | 0.32 | 0.74 | 0.92 | 11.62 |   58.6  |
| `create` | per-request   | 0.69 | 1.22 | 1.65 | 18.27 | ~1063   |
| `create` | keep-alive    | 0.28 | 0.50 | 0.65 | 25.98 |   57.7  |

**Takeaway:** the median is sub-millisecond, but the tail is where the design
shows through. Per-request has a **~1-second `max`** — a handful of connections
stall in the accept backlog under load — while keep-alive, having no per-request
connection setup, keeps `max` bounded (~58 ms). The `create` p99/max also carry
occasional WAL-checkpoint spikes. This is why the benchmark reports p95/p99/max,
not just the mean.

## 3. Write throughput: WAL

Writes were the original bottleneck. With SQLite's default `synchronous=FULL`
and a rollback journal, every `INSERT` does an `fsync`, and the writer is
serialized by `db_write_lock`, so `POST /users` plateaued at **~170 req/s**
regardless of concurrency (p50 ~100 ms at c=16).

Opening the connection in **WAL journal mode with `synchronous=NORMAL`**
(`open_database()`) moved that to **~12,000–15,000 req/s** — a ~40–90×
improvement — and writes now scale with concurrency instead of serializing
behind one fsync per insert.

Trade-off: under `synchronous=NORMAL` an application crash is still safe; only an
OS/power crash can lose the last few committed transactions. Set
`synchronous=FULL` for strict durability (giving back most of the speedup).

## 4. Thread count

The pool is thread-per-connection with a bounded queue (`--threads`, default 8).
Does adding workers help? Sweep at c=32, ~2s/cell:

| `--threads` | `get_one` req/s (per-request) | `create` req/s (per-request) |
| ----------: | ----------------------------: | ---------------------------: |
|           8 |                       ~11,900 |                       ~9,900 |
|          32 |                       ~12,800 |                       ~9,800 |
|          64 |                       ~13,900 |                       ~8,900 |

**Takeaway:** more threads gives only marginal read gains (~17% from 8→64,
diminishing) and **does not help writes** (a single-file SQLite database has one
writer; no thread count parallelizes it). Thread count is not the bottleneck at
these concurrencies — the ceilings are the single shared SQLite connection (for
reads) and the single writer (for writes).

## 5. Thread scaling and memory (small worker stacks)

Because the pool is thread-per-connection, the number of simultaneously *active*
keep-alive connections is bounded by the worker count. Workers are given a small
stack (`WORKER_STACK_SIZE`, 512 KB — their real peak use is a few fixed buffers,
well under 64 KB) so a high `--threads` count stays cheap. The default pthread
stack is ~8 MB of *virtual* address space per thread, which makes thousands of
threads prohibitively expensive.

| `--threads` | worker stack | VmPeak (virtual) | VmRSS (resident) |
| ----------: | ------------ | ---------------: | ---------------: |
|        1000 | 8 MB (old)   |          ~8.2 GB |          ~16 MB  |
|        1000 | 512 KB       |          ~521 MB |          ~7.7 MB |
|        4000 | 512 KB       |          ~2.0 GB |          ~20 MB  |
|       10000 | 512 KB       |          ~5.0 GB |          ~45 MB  |

**Takeaway:** small stacks cut virtual memory ~16× and let the pool scale to
**10,000 workers** in ~5 GB virtual / ~45 MB resident. If `pthread_create`
eventually fails (e.g. `RLIMIT_NPROC`), the pool runs with the workers it did
create and logs how many, rather than crashing.

This is still a **1:1 kernel-thread** model, not lightweight (Go-style) threads:
a blocking `recv()` pins a kernel thread for the connection's lifetime, and the
`MAX_KEEPALIVE_REQUESTS` cap + `SO_RCVTIMEO` idle timeout bound how long one
connection holds a worker. Small stacks make thread-per-connection scale
*further*, not *free*.

## 6. Compiler optimization: `-O0` vs `-O2`

The build compiles at `-O2` (kept debuggable with `-g`; override with
`make OPT=-O0`). To isolate the flag, both binaries were built from the same
commit and run **interleaved** on the same machine.

End-to-end on shared CPUs (the Python load generator and the server competing
for the 16 vCPUs), the difference was **within run-to-run noise** — ±30–50%
swings dwarfed any flag effect. That is expected: the client and the `recv`/
`send` syscalls dominate, and the server's own compute is a small slice of each
request.

Pinning the server to a **single core** (so server CPU is the actual bottleneck)
separates the two kinds of endpoint:

| Scenario (c=6, keep-alive, server pinned to 1 core) | `-O0` req/s | `-O2` req/s |    delta |
| --------------------------------------------------- | ----------: | ----------: | -------: |
| `GET /livez` (no DB — pure parse/route/response)     |    ~46,900  |    ~47,800  | ~0% (noise) |
| `GET /users/1` (SQLite read + JSON build)            |    ~27,600  |    ~30,500  |    ~+10% |

*(medians of 5 interleaved rounds; WSL2, 16 vCPU; hardware-dependent)*

**Takeaway:** `-O2` helps only where there is real compute — the read +
JSON-escaping path gains ~10% when the server core is saturated. The pure-I/O
path (`livez`) is syscall-bound and unchanged. In normal operation (server not
CPU-pinned) even the `get_one` gain disappears into noise. So `-O2` is a genuine
but **modest and situational** win — free to keep, not a headline number.

## What we did not do (and why)

- **A response cache** — reads are already tens of thousands/sec from the OS page
  cache; a cache would optimize the fast path while adding invalidation and
  cross-thread-locking risk. The write path, not read latency, was the ceiling.
- **A lock-free / sharded writer** — a single-file SQLite database has one
  writer by design; WAL already captured the realistic win.
- **An epoll event loop + user-space coroutines** — this is the real lever for
  *many thousands of concurrent connections* (decoupling connection count from
  thread count, the nginx/Redis and Go-runtime model). It is a transport-core
  rewrite and a large amount of complexity/dependency for a localhost mock
  server, so it is intentionally out of scope.

## Levers, ranked

1. **WAL + `synchronous=NORMAL`** — done; the big write win.
2. **HTTP keep-alive** — done; removes per-request connection setup.
3. **Small worker stacks** — done; lets `--threads` scale to thousands cheaply.
4. **`-O2` builds + `TCP_NODELAY`** — done; cheap CPU/latency wins. The workload
   is largely syscall/DB-bound, so the effect is modest and situational (see §6:
   ~10% on the compute-heavy read path when the server core is saturated, within
   noise otherwise).
5. **Cached write prepared statements** — done; the write paths compile their
   statement once and reuse it instead of prepare/finalize per request, saving
   that CPU under `db_write_lock`. Reads stay lock-free and uncached (a shared
   statement can't be stepped by two reader threads at once).
6. **Per-thread SQLite read connections** (WAL allows concurrent readers) — would
   parallelize reads instead of serializing on the one shared connection. Not
   done; the highest-value remaining read change if read concurrency matters.
7. **epoll event loop** — the ceiling-buster for many concurrent connections;
   biggest effort, out of scope for this project.
