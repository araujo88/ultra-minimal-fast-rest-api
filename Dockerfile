# Build and run the server on a current, supported base image.
FROM ubuntu:24.04

# Only what is needed to compile and run: a C toolchain, make, and SQLite.
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc \
        make \
        libc6-dev \
        libsqlite3-dev \
        libsqlite3-0 \
    && rm -rf /var/lib/apt/lists/*

# Run as an unprivileged user that owns the working directory (the build writes
# object files/binaries and the server writes sqlite3.db into its CWD).
RUN useradd -m -u 1001 appuser && mkdir -p /app && chown appuser:appuser /app
WORKDIR /app
COPY --chown=appuser:appuser . /app
USER appuser

# Build at image-build time, so a broken build fails here rather than at
# container start.
RUN make

EXPOSE 9002

# Behind Docker's bridge network, forwarded requests arrive from the gateway
# IP (not 127.0.0.1), so the IP allowlist would otherwise reject them. Allow
# all by default; override with a comma-separated list to restrict.
ENV ALLOWED_HOSTS=*

CMD ["./server"]
