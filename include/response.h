#ifndef _RESPONSE_H
#define _RESPONSE_H 1

#include <stddef.h> // size_t

// HTTP response construction, shared by the router (server.c) and the views.

// Send the whole buffer, tolerating partial writes and EINTR. A short send()
// would otherwise silently truncate a response.
void response_send_all(int fd, const char *data, size_t len);

// Build and send a complete HTTP/1.1 response: status line, Date, Content-Type,
// Content-Length, then the body, with CRLF terminators. Bounded; the Date is
// formatted with ctime_r so it is safe to call from multiple worker threads.
void response_send(int fd, const char *status, const char *content_type, const char *body);

// Print the "[<date>] - " log prefix (thread-safe timestamp).
void response_log_prefix(void);

// Set, for the current worker thread, whether the next response(s) should
// advertise "Connection: close" (1) or "Connection: keep-alive" (0). The
// transport (server.c) sets this once per request before dispatching; the value
// is thread-local so workers never race. Defaults to close.
void response_set_connection_close(int close_after);

#endif
