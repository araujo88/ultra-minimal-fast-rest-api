#ifndef _SETTINGS_H
#define _SETTINGS_H 1

// Compile-time default client allowlist. Overridden at runtime by the
// ALLOWED_HOSTS environment variable (comma-separated IPv4 list, or "*" to
// allow all) when it is set -- see init_allowlist() in server.c.
#define NUM_ALLOWED_HOSTS 2

char *ALLOWED_HOSTS[NUM_ALLOWED_HOSTS] = {
    "0.0.0.0",
    "127.0.0.1"};

#endif