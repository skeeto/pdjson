#ifndef NULLPROGRAM_JSON_PARSER_PRIVATE_H
#define NULLPROGRAM_JSON_PARSER_PRIVATE_H

#include <stdio.h>

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

struct json_stream {
    size_t lineno;
    int error;
    char errmsg[128];
    enum json_type next;
    struct nesting *nesting;
    struct {
        char *string;
        size_t string_fill, string_size;
    } data;
    size_t ntokens;
    struct json_source source;
};

#endif
