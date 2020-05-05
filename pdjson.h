#ifndef PDJSON_H
#define PDJSON_H

#ifndef PDJSON_SYMEXPORT
#   define PDJSON_SYMEXPORT
#endif

#ifdef __cplusplus
extern "C" {
#else
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
    #include <stdbool.h>
#else
    #ifndef bool
        #define bool int
        #define true 1
        #define false 0
    #endif /* bool */
#endif /* __STDC_VERSION__ */
#endif /* __cplusplus */

#include <stdio.h>

enum json_type {
    JSON_ERROR = 1, JSON_DONE,
    JSON_OBJECT, JSON_OBJECT_END, JSON_ARRAY, JSON_ARRAY_END,
    JSON_STRING, JSON_NUMBER, JSON_TRUE, JSON_FALSE, JSON_NULL
};

struct json_allocator {
    void *(*malloc)(size_t);
    void *(*realloc)(void *, size_t);
    void (*free)(void *);
};

typedef int (*json_user_io)(void *user);

typedef struct json_stream json_stream;
typedef struct json_allocator json_allocator;

PDJSON_SYMEXPORT void json_open_buffer(json_stream *json, const void *buffer, size_t size);
PDJSON_SYMEXPORT void json_open_string(json_stream *json, const char *string);
PDJSON_SYMEXPORT void json_open_stream(json_stream *json, FILE *stream);
PDJSON_SYMEXPORT void json_open_user(json_stream *json, json_user_io get, json_user_io peek, void *user);
PDJSON_SYMEXPORT void json_close(json_stream *json);

PDJSON_SYMEXPORT void json_set_allocator(json_stream *json, json_allocator *a);
PDJSON_SYMEXPORT void json_set_streaming(json_stream *json, bool mode);

PDJSON_SYMEXPORT enum json_type json_next(json_stream *json);
PDJSON_SYMEXPORT enum json_type json_peek(json_stream *json);
PDJSON_SYMEXPORT void json_reset(json_stream *json);
PDJSON_SYMEXPORT const char *json_get_string(json_stream *json, size_t *length);
PDJSON_SYMEXPORT double json_get_number(json_stream *json);

PDJSON_SYMEXPORT enum json_type json_skip(json_stream *json);
PDJSON_SYMEXPORT enum json_type json_skip_until(json_stream *json, enum json_type type);

PDJSON_SYMEXPORT size_t json_get_lineno(json_stream *json);
PDJSON_SYMEXPORT size_t json_get_position(json_stream *json);
PDJSON_SYMEXPORT size_t json_get_column(json_stream *json);
PDJSON_SYMEXPORT size_t json_get_depth(json_stream *json);
PDJSON_SYMEXPORT enum json_type json_get_context(json_stream *json, size_t *count);
PDJSON_SYMEXPORT const char *json_get_error(json_stream *json);

PDJSON_SYMEXPORT int json_source_get(json_stream *json);
PDJSON_SYMEXPORT int json_source_peek(json_stream *json);
PDJSON_SYMEXPORT bool json_isspace(int c);

/* internal */

struct json_source {
    int (*get)(struct json_source *);
    int (*peek)(struct json_source *);
    size_t position;
    union {
        struct {
            FILE *stream;
        } stream;
        struct {
            const char *buffer;
            size_t length;
        } buffer;
        struct {
            void *ptr;
            json_user_io get;
            json_user_io peek;
        } user;
    } source;
};

struct json_stream {
    size_t lineno;

    /* While counting lines is straightforward, columns are tricky because we
     * have to count codepoints, not bytes. We could have peppered the code
     * with increments in all the relevant places but that seems inelegant.
     * So instead we calculate the column dynamically, based on the current
     * position.
     *
     * Specifically, we will remember the position at the beginning of each
     * line (linepos) and, assuming only the ASCII characters on the line, the
     * column will be the difference between the current position and linepos.
     * Of course there could also be multi-byte UTF-8 sequences which we will
     * handle by keeping an adjustment (lineadj) -- the number of continuation
     * bytes encountered on this line so far. Finally, for json_source_get()
     * we also have to keep the number of remaining continuation bytes in the
     * current multi-byte UTF-8 sequence (linecon).
     *
     * This is not the end of the story, however: with only the just described
     * approach we will always end up with the column of the latest character
     * read which is not what we want when returning potentially multi-
     * character value events (string, number, etc); in these cases we want to
     * return the column of the first character (note that if the value itself
     * is invalid and we are returning JSON_ERROR, we still want the current
     * column). So to handle this we will cache the start column (colno) for
     * such events.
     */
    size_t linepos; /* Position at the beginning of the current line. */
    size_t lineadj; /* Adjustment for multi-byte UTF-8 sequences. */
    size_t linecon; /* Number of remaining UTF-8 continuation bytes. */
    size_t colno;   /* Start column for value events or 0. */

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

    size_t ntokens;

    struct json_source source;
    struct json_allocator alloc;
    char errmsg[128];
};

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif
