#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include "pdjson.h"

#define JSON_FLAG_ERROR      (1u << 0)

struct json_stack {
    enum json_type type;
    long count;
};

/* Since errmsg is filled with sprintf(), it must never be filled with
 * unbounded input data (e.g. %s).
 */
static void
json_error(struct json_stream *json, const char *fmt, ...)
{
    if (!(json->flags & JSON_FLAG_ERROR)) {
        va_list ap;
        char *p = json->errmsg;
        json->flags |= JSON_FLAG_ERROR;
        p += sprintf(p, "error: %lu: ", (unsigned long)json->lineno);
        va_start(ap, fmt);
        vsprintf(p, fmt, ap);
        va_end(ap);
    }
}

static enum json_type
push(struct json_stream *json, enum json_type type)
{
    json->stack_top++;

    if (json->stack_top >= json->stack_size) {
        struct json_stack *stack;
        size_t size = json->stack_size ? json->stack_size * 2 : 8;
        stack = json->alloc(json->stack,
                            size * sizeof(*json->stack),
                            json->alloc_arg);
        if (!stack) {
            json_error(json, "out of memory");
            return JSON_ERROR;
        }
        json->stack_size = size;
        json->stack = stack;
    }

    json->stack[json->stack_top].type = type;
    json->stack[json->stack_top].count = 0;

    return type;
}

static enum json_type
pop(struct json_stream *json, int c, enum json_type expected)
{
    if (json->stack == NULL || json->stack[json->stack_top].type != expected) {
        json_error(json, "unexpected byte, '%c'", c);
        return JSON_ERROR;
    }
    json->stack_top--;
    return expected == JSON_ARRAY ? JSON_ARRAY_END : JSON_OBJECT_END;
}

static int
json_io_get(struct json_stream *json)
{
    int c;
    if (json->peek >= -1) {
        c = json->peek;
        json->peek = -2;
    } else {
        c = json->fgetc(json->fgetc_arg);
        if (c < 0)
            c = -1;
        else
            json->position++; /* FIXME */
    }
    return c;
}

static int
json_io_peek(struct json_stream *json)
{
    if (json->peek >= -1)
        return json->peek;
    return (json->peek = json_io_get(json));
}

static enum json_type
is_match(struct json_stream *json, const char *pattern, enum json_type type)
{
    const char *p;
    for (p = pattern; *p; p++)
        if (*p != json_io_get(json))
            return JSON_ERROR;
    return type;
}

static int
pushchar(struct json_stream *json, int c)
{
    if (json->data.string_fill == json->data.string_size) {
        size_t size = json->data.string_size * 2;
        char *buffer = json->alloc(json->data.string,
                                   size,
                                   json->alloc_arg);
        if (buffer == NULL) {
            json_error(json, "out of memory");
            return -1;
        } else {
            json->data.string_size = size;
            json->data.string = buffer;
        }
    }
    json->data.string[json->data.string_fill++] = c;
    return 0;
}

static int
init_string(struct json_stream *json)
{
    json->data.string_fill = 0;
    if (json->data.string == NULL) {
        json->data.string_size = 1024;
        json->data.string = json->alloc(0,
                                        json->data.string_size,
                                        json->alloc_arg);
        if (json->data.string == NULL) {
            json_error(json, "out of memory");
            return -1;
        }
    }
    json->data.string[0] = '\0';
    return 0;
}

static int
encode_utf8(struct json_stream *json, unsigned long c)
{
    if (c < 0x80UL) {
        return pushchar(json, c);
    } else if (c < 0x0800UL) {
        return !((pushchar(json, (c >> 6 & 0x1F) | 0xC0) == 0) &&
                 (pushchar(json, (c >> 0 & 0x3F) | 0x80) == 0));
    } else if (c < 0x010000UL) {
        if (c >= 0xd800 && c <= 0xdfff) {
            json_error(json, "invalid codepoint %06lx", c);
            return -1;
        }
        return !((pushchar(json, (c >> 12 & 0x0F) | 0xE0) == 0) &&
                 (pushchar(json, (c >>  6 & 0x3F) | 0x80) == 0) &&
                 (pushchar(json, (c >>  0 & 0x3F) | 0x80) == 0));
    } else if (c < 0x110000UL) {
        return !((pushchar(json, (c >> 18 & 0x07) | 0xF0) == 0) &&
                (pushchar(json, (c >> 12 & 0x3F) | 0x80) == 0) &&
                (pushchar(json, (c >> 6  & 0x3F) | 0x80) == 0) &&
                (pushchar(json, (c >> 0  & 0x3F) | 0x80) == 0));
    } else {
        json_error(json, "can't encode UTF-8 for %06lx", c);
        return -1;
    }
}

static int
hexchar(int c)
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

static long
read_unicode_cp(struct json_stream *json)
{
    size_t i;
    long cp = 0;
    int shift = 12;

    for (i = 0; i < 4; i++) {
        int c = json_io_get(json);
        int hc;

        if (c == -1) {
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

static int
read_unicode(struct json_stream *json)
{
    long cp, h, l;

    if ((cp = read_unicode_cp(json)) == -1) {
        return -1;
    }

    if (cp >= 0xd800 && cp <= 0xdbff) {
        int c;

        /* This is the high portion of a surrogate pair; we need to read the
         * lower portion to get the codepoint
         */
        h = cp;

        c = json_io_get(json);
        if (c == -1) {
            json_error(json, "%s", "unterminated string literal in unicode");
            return -1;
        } else if (c != '\\') {
            json_error(json, "invalid continuation for surrogate pair: '%c', "
                             "expected '\\'", c);
            return -1;
        }

        c = json_io_get(json);
        if (c == -1) {
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
            json_error(json, "invalid surrogate pair continuation \\u%04lx out "
                             "of range (dc00-dfff)", l);
            return -1;
        }

        cp = ((h - 0xd800) * 0x400) + ((l - 0xdc00) + 0x10000);
    } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            json_error(json, "dangling surrogate \\u%04lx", cp);
            return -1;
    }

    return encode_utf8(json, cp);
}

static int
read_escaped(struct json_stream *json)
{
    int c = json_io_get(json);
    if (c == -1) {
        json_error(json, "%s", "unterminated string literal in escape");
        return -1;
    } else if (c == 'u') {
        if (read_unicode(json) != 0)
            return -1;
    } else {
        switch (c) {
            case '\\':
            case 'b':
            case 'f':
            case 'n':
            case 'r':
            case 't':
            case '/':
            case '"': {
                const char *codes = "\\bfnrt/\"";
                const char *p = strchr(codes, c);
                if (pushchar(json, "\\\b\f\n\r\t/\""[p - codes]) != 0)
                    return -1;
            } break;
            default:
                json_error(json, "bad escaped byte, '%c'", c);
                return -1;
        }
    }
    return 0;
}

static int
char_needs_escaping(int c)
{
    if ((c >= 0) && (c < 0x20 || c == 0x22 || c == 0x5c)) {
        return 1;
    }

    return 0;
}

static int
utf8_seq_length(char byte)
{
    unsigned char u = byte;
    if (u < 0x80) return 1;

    if (0x80 <= u && u <= 0xBF) {
        /* second, third or fourth byte of a multi-byte
         * sequence, i.e. a "continuation byte"
         */
        return 0;
    } else if (u == 0xC0 || u == 0xC1) {
        /* overlong encoding of an ASCII byte */
        return 0;
    } else if (0xC2 <= u && u <= 0xDF) {
        /* 2-byte sequence */
        return 2;
    } else if (0xE0 <= u && u <= 0xEF) {
        /* 3-byte sequence */
        return 3;
    } else if (0xF0 <= u && u <= 0xF4) {
        /* 4-byte sequence */
        return 4;
    } else {
        /* u >= 0xF5
         * Restricted (start of 4-, 5- or 6-byte sequence) or invalid UTF-8
         */
        return 0;
    }
}

static int
is_legal_utf8(const unsigned char *bytes, int length)
{
    unsigned char a;
    const unsigned char *srcptr;

    if (0 == bytes || 0 == length)
        return 0;

    srcptr = bytes + length;
    switch (length) {
        default:
            return 0;
            /* Everything else falls through when true. */
        case 4:
            if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
            /* FALLTHROUGH */
        case 3:
            if ((a = (*--srcptr)) < 0x80 || a > 0xBF) return 0;
            /* FALLTHROUGH */
        case 2:
            a = (*--srcptr);
            switch (*bytes) {
                case 0xE0:
                    if (a < 0xA0 || a > 0xBF) return 0;
                    break;
                case 0xED:
                    if (a < 0x80 || a > 0x9F) return 0;
                    break;
                case 0xF0:
                    if (a < 0x90 || a > 0xBF) return 0;
                    break;
                case 0xF4:
                    if (a < 0x80 || a > 0x8F) return 0;
                    break;
                default:
                    if (a < 0x80 || a > 0xBF) return 0;
            }
            /* FALLTHROUGH */
        case 1:
            if (*bytes >= 0x80 && *bytes < 0xC2) return 0;
    }
    return *bytes <= 0xF4;
}

static int
read_utf8(struct json_stream *json, int next_char)
{
    int i;
    unsigned char buffer[4];

    int count = utf8_seq_length(next_char);
    if (!count) {
        json_error(json, "%s", "Bad character.");
        return -1;
    }

    buffer[0] = next_char;
    for (i = 1; i < count; ++i)
        buffer[i] = json_io_get(json);

    if (!is_legal_utf8(buffer, count)) {
        json_error(json, "%s", "No legal UTF8 found");
        return -1;
    }

    for (i = 0; i < count; ++i)
        if (pushchar(json, buffer[i]) != 0)
            return -1;
    return 0;
}

static enum json_type
read_string(struct json_stream *json)
{
    if (init_string(json) != 0)
        return JSON_ERROR;
    for (;;) {
        int c = json_io_get(json);
        if (c == -1) {
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
        } else if ((unsigned) c >= 0x80) {
            if (read_utf8(json, c) != 0)
                return JSON_ERROR;
        } else {
            if (char_needs_escaping(c)) {
                json_error(json, "%s", "unescaped control character in string");
                return JSON_ERROR;
            }

            if (pushchar(json, c) != 0)
                return JSON_ERROR;
        }
    }
    return JSON_ERROR;
}

static int
is_digit(int c)
{
    return c >= 48 /*0*/ && c <= 57 /*9*/;
}

static int
read_digits(struct json_stream *json)
{
    unsigned nread = 0;
    while (is_digit(json_io_peek(json))) {
        if (pushchar(json, json_io_get(json)) != 0)
            return -1;

        nread++;
    }

    if (nread == 0) {
        return -1;
    }

    return 0;
}

static enum json_type
read_number(struct json_stream *json, int c)
{
    if (pushchar(json, c) != 0)
        return JSON_ERROR;
    if (c == '-') {
        c = json_io_get(json);
        if (is_digit(c)) {
            return read_number(json, c);
        } else {
            json_error(json, "unexpected byte, '%c'", c);
        }
    } else if (strchr("123456789", c) != NULL) {
        c = json_io_peek(json);
        if (is_digit(c)) {
            if (read_digits(json) != 0)
                return JSON_ERROR;
        }
    }
    /* Up to decimal or exponent has been read. */
    c = json_io_peek(json);
    if (strchr(".eE", c) == NULL) {
        if (pushchar(json, '\0') != 0)
            return JSON_ERROR;
        else
            return JSON_NUMBER;
    }
    if (c == '.') {
        json_io_get(json); /* consume . */
        if (pushchar(json, c) != 0)
            return JSON_ERROR;
        if (read_digits(json) != 0)
            return JSON_ERROR;
    }
    /* Check for exponent. */
    c = json_io_peek(json);
    if (c == 'e' || c == 'E') {
        json_io_get(json); /* consume e/E */
        if (pushchar(json, c) != 0)
            return JSON_ERROR;
        c = json_io_peek(json);
        if (c == '+' || c == '-') {
            json_io_get(json); /* consume */
            if (pushchar(json, c) != 0)
                return JSON_ERROR;
            if (read_digits(json) != 0)
                return JSON_ERROR;
        } else if (is_digit(c)) {
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

static int
json_isspace(int c)
{
    switch (c) {
        case 0x09:
        case 0x0a:
        case 0x0d:
        case 0x20:
            return 1;
    }

    return 0;
}

/* Returns the next non-whitespace character in the stream. */
static int
next(struct json_stream *json)
{
    int c;
    while (json_isspace((c = json_io_get(json))))
        if (c == '\n')
            json->lineno++;
    return c;
}

static enum json_type
read_value(struct json_stream *json, int c)
{
    json->ntokens++;
    switch (c) {
        case -1:
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

enum json_type
json_peek(struct json_stream *json)
{
    enum json_type next = json_next(json);
    json->next = next;
    return next;
}

enum json_type
json_next(struct json_stream *json)
{
    int c;

    if (json->flags & JSON_FLAG_ERROR)
        return JSON_ERROR;

    if (json->next != 0) {
        enum json_type next = json->next;
        json->next = (enum json_type)0;
        return next;
    }

    if (json->ntokens > 0 && json->stack_top == (size_t)-1) {
        do {
            c = json_io_peek(json);
            if (json_isspace(c)) {
                c = json_io_get(json);
            }
        } while (json_isspace(c));

        if (!(json->flags & JSON_FLAG_STREAMING) && c != -1) {
            return JSON_ERROR;
        }

        return JSON_DONE;
    }

    c = next(json);
    if (json->stack_top == (size_t)-1)
        return read_value(json, c);

    if (json->stack[json->stack_top].type == JSON_ARRAY) {
        if (json->stack[json->stack_top].count == 0) {
            if (c == ']') {
                return pop(json, c, JSON_ARRAY);
            }
            json->stack[json->stack_top].count++;
            return read_value(json, c);
        } else if (c == ',') {
            json->stack[json->stack_top].count++;
            return read_value(json, next(json));
        } else if (c == ']') {
            return pop(json, c, JSON_ARRAY);
        } else {
            json_error(json, "unexpected byte, '%c'", c);
            return JSON_ERROR;
        }
    } else if (json->stack[json->stack_top].type == JSON_OBJECT) {
        if (json->stack[json->stack_top].count == 0) {
            enum json_type value;
            if (c == '}') {
                return pop(json, c, JSON_OBJECT);
            }

            /* No property value pairs yet. */
            value = read_value(json, c);
            if (value != JSON_STRING) {
                json_error(json, "%s", "expected property name or '}'");
                return JSON_ERROR;
            } else {
                json->stack[json->stack_top].count++;
                return value;
            }
        } else if ((json->stack[json->stack_top].count % 2) == 0) {
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
                    json->stack[json->stack_top].count++;
                    return value;
                }
            }
        } else if ((json->stack[json->stack_top].count % 2) == 1) {
            /* Expecting colon followed by value. */
            if (c != ':') {
                json_error(json, "%s", "expected ':' after property name");
                return JSON_ERROR;
            } else {
                json->stack[json->stack_top].count++;
                return read_value(json, next(json));
            }
        }
    }
    json_error(json, "%s", "invalid parser state");
    return JSON_ERROR;
}

void
json_reset(struct json_stream *json)
{
    json->stack_top = -1;
    json->ntokens = 0;
    json->flags &= ~JSON_FLAG_ERROR;
    json->errmsg[0] = '\0';
}

const char *
json_get_string(struct json_stream *json, size_t *length)
{
    if (length != NULL)
        *length = json->data.string_fill;
    if (json->data.string == NULL)
        return "";
    else
        return json->data.string;
}

double
json_get_number(struct json_stream *json)
{
    char *p = json->data.string;
    return p == NULL ? 0 : strtod(p, NULL);
}

const char *
json_get_error(struct json_stream *json)
{
    return json->flags & JSON_FLAG_ERROR ? json->errmsg : NULL;
}

unsigned long
json_get_lineno(struct json_stream *json)
{
    return json->lineno;
}

unsigned long
json_get_position(struct json_stream *json)
{
    return json->position;
}

size_t
json_get_depth(struct json_stream *json)
{
    return json->stack_top + 1;
}

static void *
json_alloc_default(void *buf, size_t len, void *arg)
{
    (void)arg;
    if (!len) {
        free(buf);
        return 0;
    } else {
        return realloc(buf, len);
    }
}

void
json_open(struct json_stream *json, json_fgetc fgetc, void *arg, int flags)
{
    json->lineno = 1;
    json->flags = flags;
    json->errmsg[0] = 0;
    json->ntokens = 0;
    json->next = 0;

    json->stack = NULL;
    json->stack_top = -1;
    json->stack_size = 0;

    json->data.string = NULL;
    json->data.string_size = 0;
    json->data.string_fill = 0;

    json->alloc = json_alloc_default;

    json->fgetc = fgetc;
    json->fgetc_arg = arg;
    json->position = 0;
    json->peek = -2;
}

void
json_set_allocator(struct json_stream *json, json_alloc alloc, void *arg)
{
    json->alloc = alloc;
    json->alloc_arg = arg;
}

void
json_close(struct json_stream *json)
{
    json->alloc(json->stack, 0, json->alloc_arg);
    json->stack = 0;
    json->alloc(json->data.string, 0, json->alloc_arg);
    json->data.string = 0;
}
