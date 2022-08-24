#include "../include/routes.h"
#include "../include/models.h"
#include <stdio.h>

extern char url_get_root[URL_MAX_LEN];
extern char url_get_entries[URL_MAX_LEN];
extern char url_get_entry[URL_MAX_LEN];
extern char url_post_entry[URL_MAX_LEN];
extern char url_put_entry[URL_MAX_LEN];
extern char url_delete_entry[URL_MAX_LEN];

extern char *routes[NUM_ROUTES];

void generate_routes()
{
    printf("Generating routes...\n");
    sprintf(url_get_root, "GET / ");
    sprintf(url_get_entries, "GET /%s", TABLE_NAME);
    sprintf(url_post_entry, "POST /%s", TABLE_NAME);
    sprintf(url_get_entry, "GET /%s/", TABLE_NAME);
    sprintf(url_put_entry, "PUT /%s/", TABLE_NAME);
    sprintf(url_delete_entry, "DELETE /%s/", TABLE_NAME);
    printf("Routes generated!\n");
}