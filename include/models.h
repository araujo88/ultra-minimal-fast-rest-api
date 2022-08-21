#ifndef _MODELS_H
#define _MODELS_H 1

#define NUM_MODELS 1

#define TABLE_NAME "Users"

typedef struct _user
{
    char name[256];
    char surname[256];
} user;

#endif