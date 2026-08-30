"""Pytest fixtures: build once, manage server lifecycle per test."""
import pytest

from support import BASE_URL, ServerProcess, build_server, _remove_db


@pytest.fixture(scope="session", autouse=True)
def _build():
    build_server()
    yield
    _remove_db()


@pytest.fixture
def server():
    """A fresh server backed by an empty database for a single test."""
    proc = ServerProcess(clean_db=True).start()
    try:
        yield BASE_URL
    finally:
        proc.stop()
        _remove_db()


@pytest.fixture
def server_manager():
    """Factory for tests that need to control start/stop/restart themselves
    (e.g. persistence across a restart). Cleans up all instances afterwards."""
    procs = []

    def factory(clean_db=True):
        proc = ServerProcess(clean_db=clean_db).start()
        procs.append(proc)
        return proc

    try:
        yield factory
    finally:
        for proc in procs:
            proc.stop()
        _remove_db()
