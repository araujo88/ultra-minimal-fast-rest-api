#ifndef _MODELS_H
#define _MODELS_H 1

#define NUM_COLS 4

#define STR_LEN 256

#define TABLE_NAME "Users"

static char *TABLE_COLS[NUM_COLS][2] = {
	{"name", "TEXT"},
	{"surname", "TEXT"},
	{"age", "INT"},
	{"height", "REAL"},
};

#endif
