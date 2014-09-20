#include <stdio.h>
#include <stdlib.h>
#include "json.h"

void indent(int n)
{
    for (int i = 0; i < n * 2; i++)
        putchar(' ');
}

void pretty(json_stream_t *json);

void pretty_array(json_stream_t *json)
{
    printf("[\n");
    int first = 1;
    while (json_peek(json) != JSON_ARRAY_END && !json_get_error(json)) {
        if (!first)
            printf(",\n");
        else
            first = 0;
        indent(json_get_depth(json));
        pretty(json);
    }
    json_next(json);
    printf("\n");
    indent(json_get_depth(json));
    printf("]");
}

void pretty_object(json_stream_t *json)
{
    printf("{\n");
    int first = 1;
    while (json_peek(json) != JSON_OBJECT_END && !json_get_error(json)) {
        if (!first)
            printf(",\n");
        else
            first = 0;
        indent(json_get_depth(json));
        json_next(json);
        printf("\"%s\": ", json_get_string(json, NULL));
        pretty(json);
    }
    json_next(json);
    printf("\n");
    indent(json_get_depth(json));
    printf("}");
}

void pretty(json_stream_t *json)
{
    enum json_type type = json_next(json);
    switch (type) {
    case JSON_DONE:
        return;
    case JSON_NULL:
        printf("null");
        break;
    case JSON_TRUE:
        printf("true");
        break;
    case JSON_FALSE:
        printf("false");
            break;
    case JSON_NUMBER:
        printf("%s", json_get_string(json, NULL));
        break;
    case JSON_STRING:
        printf("\"%s\"", json_get_string(json, NULL));
        break;
    case JSON_ARRAY:
        pretty_array(json);
        break;
    case JSON_OBJECT:
        pretty_object(json);
        break;
    case JSON_OBJECT_END:
    case JSON_ARRAY_END:
        return;
    case JSON_ERROR:
            printf("exiting %d\n", type);
            fprintf(stderr, "%s\n", json_get_error(json));
            exit(EXIT_FAILURE);
    }
}

int main()
{
    json_stream_t json;
    json_open_stream(&json, stdin);
    pretty(&json);
    if (json_get_error(&json)) {
        fprintf(stderr, "%s\n", json_get_error(&json));
        exit(EXIT_FAILURE);
    } else {
        printf("\n");
    }
    json_close(&json);
    return 0;
}
