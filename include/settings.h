#ifndef _SETTINGS_H
#define _SETTINGS_H 1

// Compile-time default client allowlist. Overridden at runtime by the
// ALLOWED_HOSTS environment variable (comma-separated IPv4 list, or "*" to
// allow all) when it is set -- see init_allowlist() in server.c.
#define NUM_ALLOWED_HOSTS 2

char *ALLOWED_HOSTS[NUM_ALLOWED_HOSTS] = {
    "0.0.0.0",
    "127.0.0.1"};

// Optional HTTP Basic authentication. Empty ("") disables it -- the default.
// Set to "user:password" to require auth on every request. Overridden at
// runtime by the BASIC_AUTH environment variable, which is preferred for real
// secrets (it keeps credentials out of source control) -- see init_basic_auth()
// in server.c. Over plain HTTP this only base64-encodes credentials; put TLS in
// front for anything real (see SECURITY.md).
#define BASIC_AUTH_DEFAULT ""

#endif