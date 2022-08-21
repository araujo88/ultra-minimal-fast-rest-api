#ifndef _MODELS_H
#define _MODELS_H 1

#define NUM_MODELS 1
#define NUM_COLS 3

#define TABLE_NAME "Users"
#define STR_LEN 256

static char *TABLE_COLS[NUM_COLS][2] = {{"name", "TEXT"}, {"surname", "TEXT"}, {"age", "INTEGER"}};

typedef struct user
{
    char name[STR_LEN];
    char surname[STR_LEN];
    int age;
} model;

void string_array_to_struct(model *Model, char struct_string[NUM_COLS][STR_LEN]);
void struct_to_string_array(model *Model, char struct_string[NUM_COLS][STR_LEN]);

#endif