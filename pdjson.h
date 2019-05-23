#ifndef PDJSON_H
#define PDJSON_H

#include <stddef.h>

#define JSON_FLAG_STREAMING  (1u << 1)

enum json_type {
    JSON_ERROR = 1, JSON_DONE,
    JSON_OBJECT, JSON_OBJECT_END, JSON_ARRAY, JSON_ARRAY_END,
    JSON_STRING, JSON_NUMBER, JSON_TRUE, JSON_FALSE, JSON_NULL
};

typedef int   (*json_fgetc)(void *);
typedef void *(*json_alloc)(void *, size_t, void *);

struct json_stream;

void json_open(struct json_stream *, json_fgetc, void *, int flags);
void json_close(struct json_stream *);

void json_set_allocator(struct json_stream *, json_alloc, void *);

enum json_type json_next(struct json_stream *);
enum json_type json_peek(struct json_stream *);
void json_reset(struct json_stream *);
const char *json_get_string(struct json_stream *, size_t *);
double json_get_number(struct json_stream *);

unsigned long json_get_lineno(struct json_stream *);
unsigned long json_get_position(struct json_stream *);
size_t json_get_depth(struct json_stream *);
const char *json_get_error(struct json_stream *);

/* internal */

struct json_stream {
    unsigned long lineno;

    struct json_stack *stack;
    size_t stack_top;
    size_t stack_size;
    enum json_type next;
    unsigned flags;

    struct {
        char *string;
        size_t string_fill;
        size_t string_size;
    } data;

    unsigned long ntokens;

    json_fgetc fgetc;
    void *fgetc_arg;
    unsigned long position;
    int peek;

    json_alloc alloc;
    void *alloc_arg;

    char errmsg[128];
};

#endif
