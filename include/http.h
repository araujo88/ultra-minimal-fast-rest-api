#ifndef _HTTP_H
#define _HTTP_H 1

#include <stddef.h>    // size_t
#include <sys/types.h> // ssize_t
#include "models.h"    // NUM_COLS, STR_LEN, TABLE_COLS

// Pure HTTP request parsing, decoupled from transport and routing so each piece
// can be unit-tested / fuzzed in isolation. The only function that performs I/O
// is recv_request(); the rest operate on caller-owned buffers.

// Read one complete request into the front of buf, preserving any trailing
// bytes that belong to a following (pipelined) request so the connection can be
// reused (keep-alive). *len is in/out: bytes already buffered on entry, bytes
// buffered on exit. Returns the byte length of the single request at buf[0]
// (>0), 0 on a clean peer close with no pending request, -1 on error or a
// request that does not fit in cap. Never assumes one recv() == one request.
ssize_t recv_request(int fd, char *buf, size_t cap, size_t *len);

// Decide whether the connection should be kept alive after this request, from
// its HTTP version (1.1 defaults to keep-alive, 1.0/unknown to close) and an
// explicit Connection header if present. req must be NUL-terminated at the end
// of the request. Returns 1 to keep alive, 0 to close.
int request_keep_alive(const char *req);

// Return a pointer to the start of the body (just past the header terminator),
// or NULL if no blank line is present in buf.
char *find_body(char *buf);

// Split the request line into method and target (both NUL-terminated, bounded
// by msz/tsz). Returns 1 on success, 0 if malformed (no target).
int parse_request_line(const char *buf, char *method, size_t msz,
                       char *target, size_t tsz);

// Parse an application/x-www-form-urlencoded body into model_string, matching
// keys against the model columns. Missing fields are left as empty strings;
// values are URL-decoded and length-bounded. NULL-safe.
void parse_form(const char *body, char model_string[NUM_COLS][STR_LEN]);

// Parse a strictly-numeric unsigned id. Returns 1 and sets *out on success,
// 0 if s is empty, non-numeric, has trailing characters, or overflows.
int parse_id(const char *s, unsigned int *out);

#endif
