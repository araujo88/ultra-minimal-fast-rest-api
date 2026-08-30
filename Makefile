CC=gcc
CC_FLAGS=-g -Wall -Wextra -Wpedantic
CC_LIBS=-lpthread -lsqlite3

# Sanitizer flags for the `asan` target (AddressSanitizer + UBSan).
SAN_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -O1

# Formatter binary. CI pins clang-format 15 via pip; override to match locally
# (e.g. `make format-check CLANG_FORMAT=clang-format-15`).
CLANG_FORMAT ?= clang-format
FORMAT_FILES=$(wildcard src/*.c) $(wildcard include/*.h)

SRC_DIR=src
HDR_DIR=include
OBJ_DIR=obj

# source and object files
SRC_FILES=$(wildcard $(SRC_DIR)/*.c)
OBJ_FILES=$(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRC_FILES))

BIN_FILE=server

all: $(OBJ_DIR) $(BIN_FILE)

$(BIN_FILE): $(OBJ_FILES)
	$(CC) $(CC_FLAGS) $^ -I$(HDR_DIR) -o $@ $(CC_LIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CC_FLAGS) -c $^ -I$(HDR_DIR) -o $@ $(LFLAGS)

$(OBJ_DIR):
	mkdir $@

# Instrumented build for catching memory/UB errors during testing.
asan: CC_FLAGS += $(SAN_FLAGS)
asan: clean all

# Build with warnings promoted to errors (used in CI).
strict: CC_FLAGS += -Werror
strict: clean all

# Unit + fuzz harness for the pure HTTP parsers (no sockets/DB). Runs under the
# sanitizers so the fuzz loop actually catches out-of-bounds access.
http-test: $(SRC_DIR)/http.c tests/http_smoke.c
	$(CC) $(CC_FLAGS) $(SAN_FLAGS) -I$(HDR_DIR) $^ -o http_test
	./http_test

# Lint: verify formatting matches .clang-format (CI). Use `make format` to apply.
format-check:
	$(CLANG_FORMAT) --dry-run --Werror $(FORMAT_FILES)

format:
	$(CLANG_FORMAT) -i $(FORMAT_FILES)

# Static analysis.
cppcheck:
	cppcheck --enable=warning,performance,portability --inline-suppr --std=c11 \
		--error-exitcode=1 --suppress=missingIncludeSystem -Iinclude src

# Runtime memory check: drive the running server under Valgrind memcheck.
valgrind: $(OBJ_DIR) $(BIN_FILE)
	./run_valgrind.sh

# Throughput benchmark: seed a small dataset and sweep read/write scenarios.
bench: $(OBJ_DIR) $(BIN_FILE)
	./bench/run_bench.sh

.PHONY: all asan strict http-test format format-check cppcheck valgrind bench clean

clean:
	rm -rf $(BIN_FILE) $(OBJ_DIR) $(TBN_DIR) *.db http_test
