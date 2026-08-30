#include "../include/server.h"
#include "../include/database.h"
#include "../include/threadpool.h"
#include <getopt.h>

int server_socket;
thread_pool_t *pool;

// Cleared by SIGINT; the accept loop in create_server() sees this and performs
// the real shutdown from normal context. See create_server().
volatile sig_atomic_t server_running = 1;

// Async-signal-safe handler: only touch a sig_atomic_t flag. No printf, no
// mutexes, no SQLite, no free/exit here.
static void handle_signal(int sig)
{
    (void)sig;
    server_running = 0;
}

// Parse a bounded integer. Returns 1 and sets *out on success; 0 on any
// malformed value or out-of-range result.
static int parse_int(const char *s, int lo, int hi, int *out)
{
    if (!s || !*s)
        return 0;
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || v < lo || v > hi)
        return 0;
    *out = (int)v;
    return 1;
}

// Apply an integer override from an environment variable, keeping the current
// value (and warning) if it is set but malformed.
static void env_int(const char *name, int lo, int hi, int *value)
{
    // Validated by parse_int below; only someone controlling the process
    // environment can set it.
    // Flawfinder: ignore getenv
    const char *s = getenv(name);
    if (!s || !*s)
        return;
    if (!parse_int(s, lo, hi, value))
        fprintf(stderr, "Ignoring invalid %s=\"%s\" (using %d)\n", name, s, *value);
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [options]\n"
            "  -H, --host ADDR       bind address (default 0.0.0.0)\n"
            "  -p, --port PORT       listen port 1-65535 (default 9002)\n"
            "  -t, --threads N       worker threads 1-1024 (default 8)\n"
            "  -b, --backlog N       listen backlog 1-65535 (default 10)\n"
            "  -h, --help            show this help and exit\n"
            "\n"
            "Each option also has an environment default (a CLI flag overrides it):\n"
            "  BIND_ADDRESS, PORT, THREADS, BACKLOG, and ALLOWED_HOSTS.\n",
            prog);
}

int main(int argc, char **argv)
{
    // Defaults, then environment overrides, then CLI flags (highest priority).
    // BIND_ADDRESS is operator-controlled config, validated by inet_pton later.
    // Flawfinder: ignore getenv
    const char *host = getenv("BIND_ADDRESS");
    if (!host || !*host)
        host = "0.0.0.0";
    int port = 9002, threads = 8, backlog = 10;
    env_int("PORT", 1, 65535, &port);
    env_int("THREADS", 1, 1024, &threads);
    env_int("BACKLOG", 1, 65535, &backlog);

    static const struct option opts[] = {
        {"host", required_argument, 0, 'H'},
        {"port", required_argument, 0, 'p'},
        {"threads", required_argument, 0, 't'},
        {"backlog", required_argument, 0, 'b'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}};
    int c;
    // Standard glibc getopt_long over a fixed optstring and our own option
    // table; the flagged CWE-120 note is about "older implementations".
    // Flawfinder: ignore getopt_long
    while ((c = getopt_long(argc, argv, "H:p:t:b:h", opts, NULL)) != -1)
    {
        switch (c)
        {
        case 'H':
            host = optarg;
            break;
        case 'p':
            if (!parse_int(optarg, 1, 65535, &port))
            {
                fprintf(stderr, "Invalid --port \"%s\"\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        case 't':
            if (!parse_int(optarg, 1, 1024, &threads))
            {
                fprintf(stderr, "Invalid --threads \"%s\"\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        case 'b':
            if (!parse_int(optarg, 1, 65535, &backlog))
            {
                fprintf(stderr, "Invalid --backlog \"%s\"\n", optarg);
                return EXIT_FAILURE;
            }
            break;
        case 'h':
            usage(argv[0]);
            return EXIT_SUCCESS;
        default:
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    pool = thread_pool_create(threads, threads);
    setvbuf(stdout, NULL, _IONBF, 0);

    // Install SIGINT without SA_RESTART so a blocked accept() returns EINTR
    // and the loop can notice server_running == 0.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART
    sigaction(SIGINT, &sa, NULL);

    create_server(host, port, backlog, pool);
    return 0;
}
