#ifndef NULLPROGRAM_JSON_PARSER_H
#define NULLPROGRAM_JSON_PARSER_H

#include <stdio.h>

enum json_type {
    JSON_ERROR = -1, JSON_DONE,
    JSON_OBJECT, JSON_OBJECT_END, JSON_ARRAY, JSON_ARRAY_END,
    JSON_STRING, JSON_NUMBER, JSON_TRUE, JSON_FALSE, JSON_NULL
};

struct json_source {
    int (*get) (struct json_source *);
    int (*peek) (struct json_source *);
    size_t position;
    union {
        struct {
            FILE *stream;
        } stream;
        struct {
            const char *buffer;
            size_t length;
        } buffer;
    } source;
};

typedef struct json_stream {
    size_t lineno;
    int error;
    char errmsg[128];
    struct nesting *nesting;
    struct {
        char *string;
        size_t string_fill, string_size;
    } data;
    size_t ntokens;
    struct json_source source;
} json_stream_t;

void json_open_buffer(json_stream_t *json, const void *buffer, size_t size);
void json_open_string(json_stream_t *json, const char *string);
void json_open_stream(json_stream_t *json, FILE * stream);
void json_close(json_stream_t *json);

enum json_type json_next(json_stream_t *json);
void json_reset(json_stream_t *json);
const char *json_get_string(json_stream_t *json, size_t *length);
double json_get_number(json_stream_t *json);

size_t json_get_lineno(json_stream_t *json);
size_t json_get_position(json_stream_t *json);
size_t json_get_depth(json_stream_t *json);
const char *json_get_error(json_stream_t *json);

#endif
