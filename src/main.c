#include "../include/server.h"
#include "../include/database.h"
#include "../include/threadpool.h"

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

int main(void)
{
    pool = thread_pool_create(8, 8);
    setvbuf(stdout, NULL, _IONBF, 0);

    // Install SIGINT without SA_RESTART so a blocked accept() returns EINTR
    // and the loop can notice server_running == 0.
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0; // no SA_RESTART
    sigaction(SIGINT, &sa, NULL);

    create_server("0.0.0.0", 9002, 10, pool);
    return 0;
}
