#!/bin/bash
# Run the server under Valgrind memcheck, drive representative requests
# (CRUD + a missing id + a malformed request), then shut it down cleanly and
# fail if Valgrind reported any memory error or a definite/indirect leak.
set -u

HOST=127.0.0.1
PORT=9002
BIN=./server

[ -x "$BIN" ] || { echo "build the server first (make)"; exit 1; }
command -v valgrind >/dev/null 2>&1 || { echo "valgrind not installed"; exit 1; }

rm -f sqlite3.db sqlite3.db-wal sqlite3.db-shm
LOG=$(mktemp)

valgrind --leak-check=full \
         --errors-for-leak-kinds=definite,indirect \
         --error-exitcode=1 \
         --log-file="$LOG" \
         "$BIN" >/tmp/valgrind_server.out 2>&1 &
VG=$!

# Valgrind startup is slow; wait (up to ~40s) for the port to accept.
ready=
for _ in $(seq 1 40); do
    if curl -s -o /dev/null "http://$HOST:$PORT/livez" 2>/dev/null; then ready=1; break; fi
    kill -0 "$VG" 2>/dev/null || { echo "server exited during startup"; cat "$LOG"; exit 1; }
    sleep 1
done
[ -n "$ready" ] || { echo "server never became ready"; cat "$LOG"; kill "$VG" 2>/dev/null; exit 1; }

base="http://$HOST:$PORT"
curl -s -o /dev/null -X POST "$base/users" -d 'name=A&surname=B&age=1&height=1.0'
curl -s -o /dev/null "$base/users"
curl -s -o /dev/null "$base/users/1"
curl -s -o /dev/null "$base/users/999"                                    # 404 path
curl -s -o /dev/null -X PUT "$base/users/1" -d 'name=C&surname=D&age=2&height=1.2'
curl -s -o /dev/null -X DELETE "$base/users/1"
curl -s -o /dev/null "$base/nope"                                         # unknown route
curl -s -o /dev/null "$base/livez"                                        # liveness
curl -s -o /dev/null "$base/readyz"                                       # readiness (db_ok)
# Malformed request (no newline) via bash /dev/tcp, then close to send FIN.
{ exec 3<>"/dev/tcp/$HOST/$PORT" && printf 'GARBAGE-NO-NEWLINE' >&3 && exec 3>&-; } 2>/dev/null || true

sleep 1
kill -INT "$VG"                       # clean shutdown frees the pool and DB
for _ in $(seq 1 60); do kill -0 "$VG" 2>/dev/null || break; sleep 1; done
wait "$VG"
code=$?

echo "===== valgrind report ====="
cat "$LOG"
rm -f sqlite3.db sqlite3.db-wal sqlite3.db-shm "$LOG"
exit "$code"
