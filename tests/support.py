"""Shared helpers for the regression suite: server lifecycle + raw sockets."""
import os
import signal
import socket
import subprocess
import tempfile
import time

HOST = "127.0.0.1"
PORT = 9002
BASE_URL = f"http://{HOST}:{PORT}/"

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERVER_BIN = os.path.join(REPO_ROOT, "server")
DB_FILE = os.path.join(REPO_ROOT, "sqlite3.db")


def build_server():
    """Compile the server once (raises with output on failure)."""
    r = subprocess.run(["make"], cwd=REPO_ROOT, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(SERVER_BIN):
        raise RuntimeError(f"build failed:\n{r.stdout}\n{r.stderr}")


def _remove_db():
    for path in (DB_FILE, DB_FILE + "-journal", DB_FILE + "-wal", DB_FILE + "-shm"):
        try:
            os.remove(path)
        except FileNotFoundError:
            pass


def _port_open():
    try:
        with socket.create_connection((HOST, PORT), timeout=0.25):
            return True
    except OSError:
        return False


def wait_ready(proc, timeout=10.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if proc.poll() is not None:
            return False
        if _port_open():
            return True
        time.sleep(0.05)
    return False


def wait_closed(timeout=5.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        if not _port_open():
            return True
        time.sleep(0.05)
    return False


class ServerProcess:
    """A single server instance. Optionally starts from a clean database and
    with extra environment variables (e.g. ALLOWED_HOSTS)."""

    def __init__(self, clean_db=True, env=None):
        self.clean_db = clean_db
        self.env = env
        self.proc = None
        self._log = None

    def start(self):
        # A stale instance on the port would make the new one fail to bind.
        assert wait_closed(2.0), "port 9002 still in use before start"
        if self.clean_db:
            _remove_db()
        self._log = tempfile.TemporaryFile(mode="w+")
        proc_env = None
        if self.env:
            proc_env = os.environ.copy()
            proc_env.update(self.env)
        self.proc = subprocess.Popen(
            [SERVER_BIN], cwd=REPO_ROOT, stdout=self._log, stderr=subprocess.STDOUT,
            env=proc_env,
        )
        if not wait_ready(self.proc):
            self.stop()
            raise RuntimeError("server did not become ready")
        return self

    def stop(self, timeout=12.0):
        if self.proc is None:
            return
        if self.proc.poll() is None:
            self.proc.send_signal(signal.SIGINT)
            try:
                self.proc.wait(timeout=timeout)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                self.proc.wait(timeout=5.0)
        self.proc = None
        wait_closed(3.0)


def raw_send(payload, recv=True, timeout=3.0, half_close=True):
    """Send raw bytes; collect the response until the server closes or times out.

    half_close shuts down the write side after sending so the server sees EOF
    and handles a partial/incomplete request promptly (rather than waiting on
    its receive timeout)."""
    s = socket.create_connection((HOST, PORT), timeout=timeout)
    s.settimeout(timeout)
    try:
        try:
            s.sendall(payload)
            if half_close:
                s.shutdown(socket.SHUT_WR)
        except (BrokenPipeError, ConnectionResetError, OSError):
            # The server may respond and close before we finish sending an
            # oversized request; that is graceful handling, not a crash.
            return b""
        if not recv:
            return b""
        chunks = []
        try:
            while True:
                d = s.recv(4096)
                if not d:
                    break
                chunks.append(d)
        except (socket.timeout, ConnectionResetError):
            pass
        return b"".join(chunks)
    finally:
        s.close()
