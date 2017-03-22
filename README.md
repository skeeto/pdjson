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
(`\uxxxx`) are decoded and re-encoded into UTF-8.

The library is usable and nearly complete, but needs polish.

## API Overview

All parser state is attached to a `json_stream` struct. Its fields
should not be accessed directly. To initialize, it can be "opened" on
an input `FILE *` stream or memory buffer. It's disposed of by being
"closed."

~~~c
void json_open_stream(json_stream *json, FILE * stream);
void json_open_string(json_stream *json, const char *string);
void json_open_buffer(json_stream *json, const void *buffer, size_t size);
void json_close(json_stream *json);
~~~

By default only one value is read from the stream. The parser can be
reset to read more objects. The overall line number and position are
preserved.

~~~c
void json_reset(json_stream *json);
~~~

The JSON is parsed as a stream of events (`enum json_type`). The
stream is in the indicated state, during which data can be queried and
retrieved.

~~~c
enum json_type json_next(json_stream *json);
enum json_type json_peek(json_stream *json);

const char *json_get_string(json_stream *json, size_t *length);
double json_get_number(json_stream *json);
~~~

Numbers can also be retrieved by `json_get_string()`, which will
return the raw text number as it appeared in the JSON. This is useful
if better precision is required.

In the case of a parse error, the event will be `JSON_ERROR`. The
stream cannot be used again until it is reset. In the event of an
error, a human-friendly, English error message is available, as well
as the line number and byte position. (The line number and byte
position are always available.)

~~~c
const char *json_get_error(json_stream *json);
size_t json_get_lineno(json_stream *json);
size_t json_get_position(json_stream *json);
~~~

Outside of errors, a `JSON_OBJECT` event will always be followed by
zero or more pairs of `JSON_STRING` (property name) events and their
associated value events. That is, the stream of events will always be
logical and consistent.
