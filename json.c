#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "json.h"

#define json_error(json, format, ...)                             \
    if (!json->error) {                                           \
        json->error = 1;                                          \
        snprintf(json->errmsg, sizeof(json->errmsg),              \
                 "error: %lu: " format,                           \
                 (unsigned long) json->lineno,                    \
                 __VA_ARGS__);                                    \
    }                                                             \

struct nesting {
    enum json_type type;
    long count;
    char meta;
    struct nesting *next;
};

static enum json_type
push(json_stream *json, enum json_type type)
{
    struct nesting *nesting = malloc(sizeof(struct nesting));
    if (nesting == NULL) {
        json_error(json, "%s", strerror(errno));
        return JSON_ERROR;
    }
    nesting->type = type;
    nesting->count = 0;
    nesting->meta = 0;
    nesting->next = json->nesting;
    json->nesting = nesting;
    return type;
}

static enum json_type
pop(json_stream *json, int c, enum json_type expected)
{
    struct nesting *nesting = json->nesting;
    if (nesting == NULL || nesting->type != expected) {
        json_error(json, "unexpected byte, '%c'", c);
        free(nesting);
        return JSON_ERROR;
    }
    json->nesting = json->nesting->next;
    free(nesting);
    return expected == JSON_ARRAY ? JSON_ARRAY_END : JSON_OBJECT_END;
}

static void pop_all(json_stream *json)
{
    struct nesting *n = json->nesting;
    while (n) {
        struct nesting *current = n;
        n = n->next;
        free(current);
    }
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

static void init(json_stream *json)
{
    json->lineno = 1;
    json->error = 0;
    json->errmsg[0] = '\0';
    json->ntokens = 0;
    json->next = 0;
    json->nesting = NULL;
    json->data.string = NULL;
    json->data.string_size = 0;
    json->data.string_fill = 0;
    json->source.position = 0;
}

static enum json_type
is_match(json_stream *json, const char *pattern, enum json_type type)
{
    for (const char *p = pattern; *p; p++)
        if (*p != json->source.get(&json->source))
            return JSON_ERROR;
    return type;
}

static int pushchar(json_stream *json, int c)
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

static int init_string(json_stream *json)
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
    json->data.string[0] = '\0';
    return 0;
}

static int encode_utf8(json_stream *json, uint_least32_t c)
{
    if (c < 0x80) {
        return pushchar(json, c);
    } else if (c < 0x0800) {
        return !((pushchar(json, (c >> 6 & 0x1F) | 0xC0) == 0) &&
                 (pushchar(json, (c >> 0 & 0x3F) | 0x80) == 0));
    } else if (c < 0x010000) {
        if (c >= 0xd800 && c <= 0xdfff) {
            json_error(json, "invalid codepoint %06x", c);
            return -1;
        }
        return !((pushchar(json, (c >> 12 & 0x0F) | 0xE0) == 0) &&
                 (pushchar(json, (c >>  6 & 0x3F) | 0x80) == 0) &&
                 (pushchar(json, (c >>  0 & 0x3F) | 0x80) == 0));
    } else if (c < 0x110000) {
        return !((pushchar(json, (c >> 18 & 0x07) | 0xF0) == 0) &&
                (pushchar(json, (c >> 12 & 0x3F) | 0x80) == 0) &&
                (pushchar(json, (c >> 6  & 0x3F) | 0x80) == 0) &&
                (pushchar(json, (c >> 0  & 0x3F) | 0x80) == 0));
    } else {
        json_error(json, "can't encode UTF-8 for %06x", c);
        return -1;
    }
}

static int hexchar(int c)
{
    switch (c) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a':
    case 'A': return 10;
    case 'b':
    case 'B': return 11;
    case 'c':
    case 'C': return 12;
    case 'd':
    case 'D': return 13;
    case 'e':
    case 'E': return 14;
    case 'f':
    case 'F': return 15;
    default:
        return -1;
    }
}

static int_least32_t
read_unicode_cp(json_stream *json)
{
    int_least32_t cp = 0;
    int shift = 12;

    for (size_t i = 0; i < 4; i++) {
        int c = json->source.get(&json->source);
        int hc;

        if (c == EOF) {
            json_error(json, "%s", "unterminated string literal in unicode");
            return -1;
        } else if ((hc = hexchar(c)) == -1) {
            json_error(json, "bad escape unicode byte, '%c'", c);
            return -1;
        }

        cp += hc * (1 << shift);
        shift -= 4;
    }


    return cp;
}

static int read_unicode(json_stream *json)
{
    int_least32_t cp, h, l;

    if ((cp = read_unicode_cp(json)) == -1) {
        return -1;
    }

    if (cp >= 0xd800 && cp <= 0xdbff) {
        /* This is the high portion of a surrogate pair; we need to read the
         * lower portion to get the codepoint
         */
        h = cp;

        int c = json->source.get(&json->source);
        if (c == EOF) {
            json_error(json, "%s", "unterminated string literal in unicode");
            return -1;
        } else if (c != '\\') {
            json_error(json, "invalid continuation for surrogate pair: '%c', "
                             "expected '\\'", c);
            return -1;
        }

        c = json->source.get(&json->source);
        if (c == EOF) {
            json_error(json, "%s", "unterminated string literal in unicode");
            return -1;
        } else if (c != 'u') {
            json_error(json, "invalid continuation for surrogate pair: '%c', "
                             "expected 'u'", c);
            return -1;
        }

        if ((l = read_unicode_cp(json)) == -1) {
            return -1;
        }

        if (l < 0xdc00 || l > 0xdfff) {
            json_error(json, "invalid surrogate pair continuation \\u%04x out "
                             "of range (dc00-dfff)", l);
            return -1;
        }

        cp = ((h - 0xd800) * 0x400) + ((l - 0xdc00) + 0x10000);
    }

    return encode_utf8(json, cp);
}

int read_escaped(json_stream *json)
{
    int c = json->source.get(&json->source);
    if (c == EOF) {
        json_error(json, "%s", "unterminated string literal in escape");
        return -1;
    } else if (c == 'u') {
        if (read_unicode(json) != 0)
            return -1;
    } else {
        const char *codes = "\\bfnrt/\"";
        char *p = strchr(codes, c);
        if (p != NULL) {
            if (pushchar(json, "\\\b\f\n\r\t/\""[p - codes]) != 0)
                return -1;
        } else {
            json_error(json, "bad escaped byte, '%c'", c);
            return -1;
        }
    }
    return 0;
}

static enum json_type
read_string(json_stream *json)
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
read_digits(json_stream *json)
{
    while (isdigit(json->source.peek(&json->source))) {
        if (pushchar(json, json->source.get(&json->source)) != 0)
            return -1;
    }
    return 0;
}

static enum json_type
read_number(json_stream *json, int c)
{
    if (pushchar(json, c) != 0)
        return JSON_ERROR;
    if (c == '-') {
        c = json->source.get(&json->source);
        if (isdigit(c)) {
            return read_number(json, c);
        } else {
            json_error(json, "unexpected byte, '%c'", c);
        }
    } else if (strchr("123456789", c) != NULL) {
        if (read_digits(json) != 0)
            return JSON_ERROR;
    }
    /* Up to decimal or exponent has been read. */
    c = json->source.peek(&json->source);
    if (strchr(".eE", c) == NULL) {
        if (pushchar(json, '\0') != 0)
            return JSON_ERROR;
        else
            return JSON_NUMBER;
    }
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
            json_error(json, "unexpected byte in number, '%c'", c);
            return JSON_ERROR;
        }
    }
    if (pushchar(json, '\0') != 0)
        return JSON_ERROR;
    else
        return JSON_NUMBER;
}

/* Returns the next non-whitespace character in the stream. */
static int next(json_stream *json)
{
   int c;
   while (isspace(c = json->source.get(&json->source)))
       if (c == '\n')
           json->lineno++;
   return c;
}

static enum json_type
read_value(json_stream *json, int c)
{
    json->ntokens++;
    switch (c) {
    case EOF:
        json_error(json, "%s", "unexpected end of data");
        return JSON_ERROR;
    case '{':
        return push(json, JSON_OBJECT);
    case '[':
        return push(json, JSON_ARRAY);
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
        json_error(json, "unexpected byte, '%c'", c);
        return JSON_ERROR;
    }
}

enum json_type json_peek(json_stream *json)
{
    enum json_type next = json_next(json);
    json->next = next;
    return next;
}

enum json_type json_next(json_stream *json)
{
    if (json->error)
        return JSON_ERROR;
    if (json->next != 0) {
        enum json_type next = json->next;
        json->next = 0;
        return next;
    }
    if (json->ntokens > 0 && json->nesting == NULL)
        return JSON_DONE;
    int c = next(json);
    if (json->nesting == NULL)
        return read_value(json, c);
    if (json->nesting->type == JSON_ARRAY) {
        if (json->nesting->count == 0) {
            json->nesting->count++;
            return read_value(json, c);
        } else if (c == ',') {
            json->nesting->count++;
            return read_value(json, next(json));
        } else if (c == ']') {
            return pop(json, c, JSON_ARRAY);
        } else {
            json_error(json, "unexpected byte, '%c'", c);
            return JSON_ERROR;
        }
    } else if (json->nesting->type == JSON_OBJECT) {
        if (json->nesting->count == 0) {
            /* No property value pairs yet. */
            enum json_type value = read_value(json, c);
            if (value != JSON_STRING) {
                json_error(json, "%s", "expected property name or '}'");
                return JSON_ERROR;
            } else {
                json->nesting->count++;
                return value;
            }
        } else if ((json->nesting->count % 2) == 0) {
            /* Expecting comma followed by property name. */
            if (c != ',' && c != '}') {
                json_error(json, "%s", "expected ',' or '}'");
                return JSON_ERROR;
            } else if (c == '}') {
                return pop(json, c, JSON_OBJECT);
            } else {
                enum json_type value = read_value(json, next(json));
                if (value != JSON_STRING) {
                    json_error(json, "%s", "expected property name");
                    return JSON_ERROR;
                } else {
                    json->nesting->count++;
                    return value;
                }
            }
        } else if ((json->nesting->count % 2) == 1) {
            /* Expecting colon followed by value. */
            if (c != ':') {
                json_error(json, "%s", "expected ':' after property name");
                return JSON_ERROR;
            } else {
                json->nesting->count++;
                return read_value(json, next(json));
            }
        }
    }
    json_error(json, "%s", "invalid parser state");
    return JSON_ERROR;
}

void json_reset(json_stream *json)
{
    pop_all(json);
    json->ntokens = 0;
    json->error = 0;
    json->errmsg[0] = '\0';
}

const char *json_get_string(json_stream *json, size_t *length)
{
    if (length != NULL)
        *length = json->data.string_fill;
    if (json->data.string == NULL)
        return "";
    else
        return json->data.string;
}

double json_get_number(json_stream *json)
{
    char *p = json->data.string;
    return p == NULL ? 0 : strtod(p, NULL);
}

const char *json_get_error(json_stream *json)
{
    return json->error ? json->errmsg : NULL;
}

size_t json_get_lineno(json_stream *json)
{
    return json->lineno;
}

size_t json_get_position(json_stream *json)
{
    return json->source.position;
}

size_t json_get_depth(json_stream *json)
{
    size_t depth = 0;
    for (struct nesting *n = json->nesting; n; n = n->next)
        depth++;
    return depth;
}

void json_open_buffer(json_stream *json, const void *buffer, size_t size)
{
    init(json);
    json->source.get = buffer_get;
    json->source.peek = buffer_peek;
    json->source.source.buffer.buffer = buffer;
    json->source.source.buffer.length = size;
}

void json_open_string(json_stream *json, const char *string)
{
    json_open_buffer(json, string, strlen(string));
}

void json_open_stream(json_stream *json, FILE * stream)
{
    init(json);
    json->source.get = stream_get;
    json->source.peek = stream_peek;
    json->source.source.stream.stream = stream;
}

void json_close(json_stream *json)
{
    pop_all(json);
    free(json->data.string);
}
