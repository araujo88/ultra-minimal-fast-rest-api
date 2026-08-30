// Standalone unit + smoke/fuzz harness for the pure HTTP parsers in http.c.
// No sockets, no database -- this is exactly what the http.c extraction buys.
// Build & run:  make http-test   (or under ASan in CI for the fuzz loop).
#include "../include/http.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond)                                                       \
    do                                                                    \
    {                                                                     \
        if (!(cond))                                                      \
        {                                                                 \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static void test_request_line(void)
{
    char m[16], t[64];
    CHECK(parse_request_line("GET /users/1 HTTP/1.1\r\n", m, sizeof(m), t, sizeof(t)) == 1);
    CHECK(strcmp(m, "GET") == 0);
    CHECK(strcmp(t, "/users/1") == 0);

    // No space after method -> malformed.
    CHECK(parse_request_line("GET", m, sizeof(m), t, sizeof(t)) == 0);
    CHECK(parse_request_line("GARBAGE-NO-SPACE\r\n", m, sizeof(m), t, sizeof(t)) == 0);

    // Oversized tokens must stay bounded (no overflow; ASan would catch it).
    char big[5000];
    memset(big, 'A', sizeof(big) - 10);
    memcpy(big + sizeof(big) - 10, " / HTTP\r\n", 9);
    big[sizeof(big) - 1] = '\0';
    parse_request_line(big, m, sizeof(m), t, sizeof(t));
    CHECK(strlen(m) < sizeof(m));
    CHECK(strlen(t) < sizeof(t));
}

static void test_parse_id(void)
{
    unsigned int id = 12345;
    CHECK(parse_id("1", &id) == 1 && id == 1);
    CHECK(parse_id("0", &id) == 1 && id == 0);
    CHECK(parse_id("4294967295", &id) == 1 && id == 4294967295u);
    CHECK(parse_id("", &id) == 0);
    CHECK(parse_id("abc", &id) == 0);
    CHECK(parse_id("1x", &id) == 0);
    CHECK(parse_id("-1", &id) == 0);
    CHECK(parse_id("99999999999999999999", &id) == 0); // overflow
}

static void test_find_body(void)
{
    char a[] = "GET / HTTP/1.1\r\nHost: x\r\n\r\nBODY";
    CHECK(find_body(a) != NULL && strcmp(find_body(a), "BODY") == 0);
    char b[] = "GET / HTTP/1.0\n\nBODY2";
    CHECK(find_body(b) != NULL && strcmp(find_body(b), "BODY2") == 0);
    char c[] = "no terminator here";
    CHECK(find_body(c) == NULL);
}

static void test_parse_form(void)
{
    char m[NUM_COLS][STR_LEN];

    parse_form("name=Al%20ice&surname=Bob&age=3&height=1.5", m);
    CHECK(strcmp(m[0], "Al ice") == 0); // %20 decoded
    CHECK(strcmp(m[1], "Bob") == 0);
    CHECK(strcmp(m[2], "3") == 0);

    // '+' -> space; missing fields stay empty; unknown keys ignored.
    parse_form("name=a+b&unknown=zzz", m);
    CHECK(strcmp(m[0], "a b") == 0);
    CHECK(m[1][0] == '\0');

    // NULL body is safe and clears the fields.
    parse_form(NULL, m);
    CHECK(m[0][0] == '\0');

    // A value longer than STR_LEN must be truncated, never overflow.
    char big[4096];
    memcpy(big, "name=", 5);
    memset(big + 5, 'x', sizeof(big) - 6);
    big[sizeof(big) - 1] = '\0';
    parse_form(big, m);
    CHECK(strlen(m[0]) < STR_LEN);
}

// Feed pseudo-random bytes at the parsers; under ASan this catches any
// out-of-bounds read/write. Invariant: never crash, output always bounded.
static void fuzz(void)
{
    unsigned int seed = 0x1234abcdu;
    char buf[512], m[16], t[64], mm[NUM_COLS][STR_LEN];
    for (int iter = 0; iter < 200000; iter++)
    {
        size_t len = seed % (sizeof(buf) - 1);
        for (size_t i = 0; i < len; i++)
        {
            seed = seed * 1103515245u + 12345u;
            buf[i] = (char)(seed >> 16);
        }
        buf[len] = '\0';

        parse_request_line(buf, m, sizeof(m), t, sizeof(t));
        CHECK(strlen(m) < sizeof(m) && strlen(t) < sizeof(t));
        find_body(buf);
        parse_form(buf, mm);
        for (int c = 0; c < NUM_COLS; c++)
            CHECK(strlen(mm[c]) < STR_LEN);
        unsigned int id;
        parse_id(buf, &id);
    }
}

static void test_keep_alive(void)
{
    // HTTP/1.1 defaults to keep-alive, 1.0 to close.
    CHECK(request_keep_alive("GET / HTTP/1.1\r\nHost: x\r\n\r\n") == 1);
    CHECK(request_keep_alive("GET / HTTP/1.0\r\nHost: x\r\n\r\n") == 0);
    // Explicit Connection header wins over the version default, case-insensitive.
    CHECK(request_keep_alive("GET / HTTP/1.1\r\nConnection: close\r\n\r\n") == 0);
    CHECK(request_keep_alive("GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n") == 1);
    CHECK(request_keep_alive("GET / HTTP/1.1\r\nConnection: Close\r\n\r\n") == 0);
    // No recognizable version -> close.
    CHECK(request_keep_alive("garbage") == 0);
}

int main(void)
{
    test_request_line();
    test_parse_id();
    test_find_body();
    test_parse_form();
    test_keep_alive();
    fuzz();
    if (failures)
    {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    printf("http_smoke: all checks passed\n");
    return 0;
}
