#!/bin/bash
# Build-free benchmark driver: start the server, seed a small dataset, run the
# scenario sweep, then shut the server down. Requires a built ./server.
set -u

HOST=127.0.0.1
PORT=9002
BIN=./server
DURATION="${DURATION:-3}" # seconds per cell
SEED_ROWS="${SEED_ROWS:-40}" # fits under the ~4 KB list response buffer

[ -x "$BIN" ] || { echo "build the server first (make)"; exit 1; }
command -v python3 >/dev/null 2>&1 || { echo "python3 required"; exit 1; }

rm -f sqlite3.db sqlite3.db-wal sqlite3.db-shm
"$BIN" >/tmp/bench_server.out 2>&1 &
SRV=$!

ready=
for _ in $(seq 1 20); do
    if curl -s -o /dev/null "http://$HOST:$PORT/" 2>/dev/null; then ready=1; break; fi
    kill -0 "$SRV" 2>/dev/null || { echo "server exited early"; cat /tmp/bench_server.out; exit 1; }
    sleep 0.5
done
[ -n "$ready" ] || { echo "server never became ready"; kill "$SRV" 2>/dev/null; exit 1; }

echo "Seeding $SEED_ROWS rows ..."
for i in $(seq 1 "$SEED_ROWS"); do
    curl -s -o /dev/null -X POST "http://$HOST:$PORT/users" \
        -d "name=U$i&surname=S&age=$i&height=1.5"
done

echo "Machine: $(nproc) CPUs. Duration: ${DURATION}s per cell. Connection-per-request (no keep-alive)."
echo
python3 "$(dirname "$0")/bench.py" --all --duration "$DURATION"

kill -INT "$SRV" 2>/dev/null
for _ in $(seq 1 20); do kill -0 "$SRV" 2>/dev/null || break; sleep 0.5; done
rm -f sqlite3.db sqlite3.db-wal sqlite3.db-shm
