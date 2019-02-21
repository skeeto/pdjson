# Public Domain JSON Parser for C

A public domain JSON parser focused on correctness, ANSI C99
compliance, full Unicode (UTF-8) support, minimal memory footprint,
and a simple API. As a streaming API, arbitrary large JSON could be
processed with a small amount of memory (the size of the largest
string in the JSON). It seems most C JSON libraries suck in some
significant way: broken string support (what if the string contains
`\u0000`?), broken/missing Unicode support, or crappy software license
(GPL or "do no evil"). This library intends to avoid these flaws.

The parser is intended to support *exactly* the JSON standard, no
more, no less, so that even slightly non-conforming JSON is rejected.
The input is assumed to be UTF-8, and all strings returned by the
library are UTF-8 with possible nul characters in the middle, which is
why the size output parameter is important. Encoded characters
(`\uxxxx`) are decoded and re-encoded into UTF-8. UTF-16 surrogate
pairs expressed as adjacent encoded characters are supported.

One exception to this rule is made to support a "streaming" mode. When
a JSON "stream" contains multiple JSON objects (optionally separated
by JSON whitespace), the default behavior of the parser is to allow
the stream to be "reset," and to continue parsing the stream.

The library is usable and nearly complete, but needs polish.

## API Overview

All parser state is attached to a `json_stream` struct. Its fields
should not be accessed directly. To initialize, it's "opened" on a
user-defined function that returns the input a byte at a time. Return a
negative value from this function to indicate EOF.

```c
typedef int (*json_fgetc)(void *);
void json_open(struct json_stream *, json_fgetc f, void *arg, int flags);
void json_close(struct json_stream *);
```

After opening a stream, custom allocator callbacks can be specified,
in case allocations should not come from a system-supplied malloc.
(When no custom allocator is specified, the system allocator is used.)

```c
typedef void *(*json_realloc)(void *, size_t, void *ctx);
typedef void  (*json_free)(void *, void *ctx);
void json_set_allocator(struct json_stream *,
                        json_realloc,
                        json_free,
                        void *ctx);
```

By default only one value is read from the stream. The parser can be
reset to read more objects. The overall line number and position are
preserved.

```c
void json_reset(struct json_stream *);
```

By default the parser enforced strict conformance to the JSON standard.
To relax this and allow for a stream of independent JSON objects, use
the `JSON_FLAG_STREAMING` flag when opening the JSON stream.

The JSON is parsed as a stream of events (`enum json_type`). The
stream is in the indicated state, during which data can be queried and
retrieved.

```c
enum json_type json_next(struct json_stream *);
enum json_type json_peek(struct json_stream *);

const char *json_get_string(struct json_stream *, size_t *length);
double json_get_number(json_stream *);
```

Numbers can also be retrieved by `json_get_string()`, which will
return the raw text number as it appeared in the JSON. This is useful
if better precision is required.

In the case of a parse error, the event will be `JSON_ERROR`. The
stream cannot be used again until it is reset. In the event of an
error, a human-friendly, English error message is available, as well
as the line number and byte position. (The line number and byte
position are always available.)

```c
const char *json_get_error(json_stream *);
unsigned long json_get_lineno(json_stream *);
unsigned long json_get_position(json_stream *);
```

Outside of errors, a `JSON_OBJECT` event will always be followed by
zero or more pairs of `JSON_STRING` (property name) events and their
associated value events. That is, the stream of events will always be
logical and consistent.

## Stream Open Example

Suppose you want to consume a JSON object on standard input. The source
function will wrap `fgetc()`.

```c
static int
my_fgetc(void *arg)
{
    return fgetc(arg);
}

/* ... */

    struct json_stream json;
    json_open(&json, my_fgetc, stdin, 0);

    /* ... consume all tokens ... */

    /* check for input errors */
    if (ferror(stdin)) {
        /* ... */
    }
```
