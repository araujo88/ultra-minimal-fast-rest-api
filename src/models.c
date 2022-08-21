#include "../include/models.h"
#include <string.h>

void string_array_to_struct(model *Model, char struct_string[NUM_COLS][STR_LEN])
{
    memcpy(Model, struct_string, STR_LEN * NUM_COLS);
}

void struct_to_string_array(model *Model, char struct_string[NUM_COLS][STR_LEN])
{
    memcpy(struct_string, Model, STR_LEN * NUM_COLS);
}