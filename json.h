#ifndef PDJSON_H
#define PDJSON_H

#include <stdio.h>

enum json_type {
    JSON_ERROR = 1, JSON_DONE,
    JSON_OBJECT, JSON_OBJECT_END, JSON_ARRAY, JSON_ARRAY_END,
    JSON_STRING, JSON_NUMBER, JSON_TRUE, JSON_FALSE, JSON_NULL
};

#include "json_private.h"

typedef struct json_stream json_stream_t;

void json_open_buffer(json_stream_t *json, const void *buffer, size_t size);
void json_open_string(json_stream_t *json, const char *string);
void json_open_stream(json_stream_t *json, FILE * stream);
void json_close(json_stream_t *json);

enum json_type json_next(json_stream_t *json);
enum json_type json_peek(json_stream_t *json);
void json_reset(json_stream_t *json);
const char *json_get_string(json_stream_t *json, size_t *length);
double json_get_number(json_stream_t *json);

size_t json_get_lineno(json_stream_t *json);
size_t json_get_position(json_stream_t *json);
size_t json_get_depth(json_stream_t *json);
const char *json_get_error(json_stream_t *json);

#endif
