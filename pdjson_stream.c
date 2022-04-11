#include <stdlib.h>
#include <stdio.h>

#ifndef PDJSON_H
#  include "pdjson.h"
#endif

static int stream_get(void *fd)
{
    return fgetc(fd);
}

static int stream_peek(void *fd)
{
    int c = fgetc(fd);
    ungetc(c, fd);
    return c;
}

void json_open_stream(json_stream *json, FILE * stream)
{
    json_open_user(json, stream_get, stream_peek, stream);
}
