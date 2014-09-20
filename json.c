#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "json.h"

#define json_error(json, format, ...)                             \
    if (!json->error) {                                           \
        json->error = 1;                                          \
        snprintf(json->errmsg, sizeof(json->errmsg),              \
                 "error: %lu: " format,  json->lineno,            \
                 __VA_ARGS__);                                    \
    }                                                             \

struct nesting {
    enum json_type type;
    struct nesting *next;
};

static enum json_type
push(json_stream_t *json, enum json_type type)
{
    struct nesting *nesting = malloc(sizeof(struct nesting));
    if (nesting == NULL) {
        json_error(json, "%s", strerror(errno));
        return JSON_ERROR;
    }
    nesting->type = type;
    nesting->next = json->nesting;
    json->nesting = nesting;
    return type;
}

static enum json_type
pop(json_stream_t *json, int c, enum json_type expected)
{
    struct nesting *nesting = json->nesting;
    if (nesting == NULL || nesting->type != expected) {
        json_error(json, "unexpected character, %c", c);
        free(nesting);
        return JSON_ERROR;
    }
    json->nesting = json->nesting->next;
    free(nesting);
    return expected == JSON_ARRAY ? JSON_ARRAY_END : JSON_OBJECT_END;
}

static int buffer_peek(struct json_source *source)
{
    if (source->position < source->source.buffer.length)
        return source->source.buffer.buffer[source->position];
    else
        return EOF;
}

static int buffer_get(struct json_source *source)
{
    int c = source->peek(source);
    source->position++;
    return c;
}

static int stream_get(struct json_source *source)
{
    source->position++;
    return fgetc(source->source.stream.stream);
}

static int stream_peek(struct json_source *source)
{
    int c = fgetc(source->source.stream.stream);
    ungetc(c, source->source.stream.stream);
    return c;
}

static void init(json_stream_t *json)
{
    json->lineno = 1;
    json->error = 0;
    json->errmsg[0] = '\0';
    json->ntokens = 0;
    json->nesting = NULL;
    json->data.string = NULL;
    json->data.string_size = 0;
    json->data.string_fill = 0;
    json->source.position = 0;
}

static enum json_type
is_match(json_stream_t *json, const char *pattern, enum json_type type)
{
    for (const char *p = pattern; *p; p++)
        if (*p != json->source.get(&json->source))
            return JSON_ERROR;
    return type;
}

static int pushchar(json_stream_t *json, int c)
{
    if (json->data.string_fill == json->data.string_size) {
        size_t size = json->data.string_size * 2;
        char *buffer = realloc(json->data.string, size);
        if (buffer == NULL) {
            json_error(json, "%s", strerror(errno));
            return -1;
        } else {
            json->data.string_size = size;
            json->data.string = buffer;
        }
    }
    json->data.string[json->data.string_fill++] = c;
    return 0;
}

static int init_string(json_stream_t *json)
{
    json->data.string_fill = 0;
    if (json->data.string == NULL) {
        json->data.string_size = 1024;
        json->data.string = malloc(json->data.string_size);
        if (json->data.string == NULL) {
            json_error(json, "%s", strerror(errno));
            return -1;
        }
    }
    return 0;
}

int read_unicode(json_stream_t *json)
{
    char code[5];
    for (size_t i = 0; i < sizeof(code) - 1; i++) {
        int c = json->source.get(&json->source);
        if (c == EOF) {
            json_error(json, "%s", "unterminated string literal in unicode");
            return -1;
        } else if (strchr("0123456789abcdefABCDEF", c) == NULL) {
            json_error(json, "bad escape unicode character, %c", c);
            return -1;
        }
        code[i] = c;
    }
    code[sizeof(code) - 1] = '\0';
    int value = strtol(code, NULL, 16);
    // TODO (UTF-8 encode)
    (void) value;
    return 0;
}

int read_escaped(json_stream_t *json)
{
    int c = json->source.get(&json->source);
    if (c == EOF) {
        json_error(json, "%s", "unterminated string literal in escape");
        return -1;
    } else if (c == 'u') {
        if (read_unicode(json) != 0)
            return -1;
    } else {
        const char *codes = "\\bfnrt/";
        char *p = strchr(codes, c);
        if (p != NULL) {
            if (pushchar(json, "\\\b\f\n\r\t/"[p - codes]) != 0)
                return -1;
        } else {
            json_error(json, "bad escaped character, %c", c);
            return -1;
        }
    }
    return 0;
}

static enum json_type
read_string(json_stream_t *json)
{
    if (init_string(json) != 0)
        return JSON_ERROR;
    while (1) {
        int c = json->source.get(&json->source);
        if (c == EOF) {
            json_error(json, "%s", "unterminated string literal");
            return JSON_ERROR;
        } else if (c == '"') {
            if (pushchar(json, '\0') == 0)
                return JSON_STRING;
            else
                return JSON_ERROR;
        } else if (c == '\\') {
            if (read_escaped(json) != 0)
                return JSON_ERROR;
        } else {
            if (pushchar(json, c) != 0)
                return JSON_ERROR;
        }
    }
    return JSON_ERROR;
}

static int
read_digits(json_stream_t *json)
{
    while (isdigit(json->source.peek(&json->source))) {
        if (pushchar(json, json->source.get(&json->source)) != 0)
            return -1;
    }
    return 0;
}

static enum json_type
read_number(json_stream_t *json, int c)
{
    if (pushchar(json, c) != 0)
        return JSON_ERROR;
    if (c == '-') {
        c = json->source.get(&json->source);
        if (isdigit(c)) {
            return read_number(json, c);
        } else {
            json_error(json, "unexpected character, %c", c);
        }
    } else if (strchr("123456789", c) != NULL) {
        if (read_digits(json) != 0)
            return JSON_ERROR;
    }
    /* Up to decimal or exponent has been read. */
    c = json->source.peek(&json->source);
    if (strchr(".eE", c) == NULL)
        return JSON_NUMBER;
    if (c == '.') {
        json->source.get(&json->source); // consume .
        if (pushchar(json, c) != 0)
            return JSON_ERROR;
        if (read_digits(json) != 0)
            return JSON_ERROR;
    }
    /* Check for exponent. */
    c = json->source.peek(&json->source);
    if (c == 'e' || c == 'E') {
        json->source.get(&json->source); // consume e/E
        if (pushchar(json, c) != 0)
            return JSON_ERROR;
        c = json->source.peek(&json->source);
        if (c == '+' || c == '-') {
            json->source.get(&json->source); // consume
            if (pushchar(json, c) != 0)
                return JSON_ERROR;
        } else if (isdigit(c)) {
            if (read_digits(json) != 0)
                return JSON_ERROR;
        } else {
            json_error(json, "unexpected character in number, %c", c);
            return JSON_ERROR;
        }
    }
    return JSON_NUMBER;
}

enum json_type json_next(json_stream_t *json)
{
    if (json->ntokens > 0 && json->nesting == NULL)
        return JSON_DONE;
    int c;
    while (isspace(c = json->source.get(&json->source)))
        if (c == '\n')
            json->lineno++;
    json->ntokens++;
    switch (c) {
    case EOF:
        if (json->nesting != NULL)
            json_error(json, "%s", "unexpected end of data");
        return JSON_ERROR;
    case '{':
        return push(json, JSON_OBJECT);
    case '[':
        return push(json, JSON_ARRAY);
    case '}':
        return pop(json, c, JSON_OBJECT);
    case ']':
        return pop(json, c, JSON_ARRAY);
    case '"':
        return read_string(json);
    case 'n':
        return is_match(json, "ull", JSON_NULL);
    case 'f':
        return is_match(json, "alse", JSON_FALSE);
    case 't':
        return is_match(json, "rue", JSON_TRUE);
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '-':
        if (init_string(json) != 0)
            return JSON_ERROR;
        return read_number(json, c);
    default:
        json_error(json, "unexpected character, %c", c);
        return JSON_ERROR;
    }
}

const char *json_get_string(json_stream_t *json, size_t *length)
{
    if (length != NULL)
        *length = json->data.string_fill;
    return json->data.string;
}

double json_get_number(json_stream_t *json)
{
    return strtod(json->data.string, NULL);
}

const char *json_get_error(json_stream_t *json)
{
    return json->error ? json->errmsg : NULL;
}

long json_get_lineno(json_stream_t *json)
{
    return json->lineno;
}

size_t json_get_position(json_stream_t *json)
{
    return json->source.position;
}

void json_open_buffer(json_stream_t *json, const void *buffer, size_t size)
{
    init(json);
    json->source.get = buffer_get;
    json->source.peek = buffer_peek;
    json->source.source.buffer.buffer = buffer;
    json->source.source.buffer.length = size;
}

void json_open_string(json_stream_t *json, const char *string)
{
    json_open_buffer(json, string, strlen(string));
}

void json_open_stream(json_stream_t *json, FILE * stream)
{
    init(json);
    json->source.get = stream_get;
    json->source.peek = stream_peek;
    json->source.source.stream.stream = stream;
}

void json_close(json_stream_t *json)
{
    struct nesting *n = json->nesting;
    while (n) {
        struct nesting *current = n;
        n = n->next;
        free(current);
    }
    free(json->data.string);
}
