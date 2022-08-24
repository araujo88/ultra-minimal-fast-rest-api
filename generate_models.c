#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#define BUFFER_SIZE 1024

char *remove_substring(char *str, const char *sub)
{
    size_t len = strlen(sub);
    if (len > 0)
    {
        char *p = str;
        while ((p = strstr(p, sub)) != NULL)
        {
            memmove(p, p + len, strlen(p + len) + 1);
        }
    }
    return str;
}

void process_file(FILE *input_file, FILE *output_file, char *buffer1)
{
    ssize_t read;
    size_t len = 0;
    char *buffer2;
    char c;
    unsigned int num_cols = 0;

    // Extract characters from file and store in character c
    for (c = getc(input_file); c != EOF; c = getc(input_file))
        if (c == '\n') // Increment count if this character is newline
            num_cols = num_cols + 1;

    num_cols = num_cols - 1;
    fclose(input_file);
    input_file = fopen("models.xml", "r");

    if (num_cols == 0)
    {
        printf("Error - model must contain at least one column.");
        exit(EXIT_FAILURE);
    }

    fprintf(output_file, "\n#define NUM_COLS %u\n\n", num_cols);
    fprintf(output_file, "#define STR_LEN 256\n");

    if (input_file)
    {
        while ((read = getline(&buffer1, &len, input_file)) != -1)
        {
            if (strstr(buffer1, "<model name=") != NULL)
            {
                buffer2 = strstr(buffer1, "<model name=");
                buffer2 += strlen("<model name=");
                remove_substring(buffer2, ">");
                fprintf(output_file, "\n#define TABLE_NAME %s\n", buffer2);
                fprintf(output_file, "static const char *TABLE_COLS[NUM_COLS][2] = {\n");
                memset(buffer2, 0, strlen(buffer2));
            }
            else if (strstr(buffer1, "<col name=") != NULL)
            {
                int i = 0;
                char field_name[BUFFER_SIZE];
                char field_type[BUFFER_SIZE];

                buffer2 = strstr(buffer1, "<col name=");
                buffer2 += strlen("<col name=");
                while (buffer2[i] != '>')
                {
                    field_name[i] = buffer2[i];
                    i++;
                }
                field_name[i] = '\0';

                buffer2 += strlen(field_name) + 1;

                i = 0;
                while (buffer2[i] != '<')
                {
                    field_type[i] = buffer2[i];
                    i++;
                }
                field_type[i] = '\0';

                fprintf(output_file, "\t{%s, \"%s\"},\n", field_name, field_type);
                memset(buffer2, 0, strlen(buffer2));
            }
        }
    }
    fprintf(output_file, "};\n");
}

int main(void)
{
    FILE *input_file;
    FILE *output_file;
    char *buffer = 0;

    printf("Generating database model ...\n");

    input_file = fopen("models.xml", "r");

    if (input_file == NULL)
    {
        printf("Error opening file.\n");
        exit(EXIT_FAILURE);
    }

    output_file = fopen("include/models.h", "w");
    if (output_file == NULL)
    {
        printf("Error creating output file\n");
        exit(EXIT_FAILURE);
    }

    fprintf(output_file, "#ifndef _MODELS_H\n");
    fprintf(output_file, "#define _MODELS_H 1\n");
    process_file(input_file, output_file, buffer);
    fprintf(output_file, "\n#endif\n");

    fclose(input_file);
    fclose(output_file);

    printf("Database model generated!\n");

    return 0;
}