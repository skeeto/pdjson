#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../pdjson.h"

void
indent(int n)
{
    int i;
    for (i = 0; i < n * 2; i++)
        putchar(' ');
}

void pretty(struct json_stream *json);

void
pretty_array(struct json_stream *json)
{
    int first = 1;
    printf("[\n");
    while (json_peek(json) != JSON_ARRAY_END && !json_get_error(json)) {
        if (!first)
            printf(",\n");
        else
            first = 0;
        indent(json_get_depth(json));
        pretty(json);
    }
    json_next(json);
    printf("\n");
    indent(json_get_depth(json));
    printf("]");
}

void
pretty_object(struct json_stream *json)
{
    int first = 1;
    printf("{\n");
    while (json_peek(json) != JSON_OBJECT_END && !json_get_error(json)) {
        if (!first)
            printf(",\n");
        else
            first = 0;
        indent(json_get_depth(json));
        json_next(json);
        printf("\"%s\": ", json_get_string(json, NULL));
        pretty(json);
    }
    json_next(json);
    printf("\n");
    indent(json_get_depth(json));
    printf("}");
}

void
pretty(struct json_stream *json)
{
    enum json_type type = json_next(json);
    switch (type) {
        case JSON_DONE:
            return;
        case JSON_NULL:
            printf("null");
            break;
        case JSON_TRUE:
            printf("true");
            break;
        case JSON_FALSE:
            printf("false");
            break;
        case JSON_NUMBER:
            printf("%s", json_get_string(json, NULL));
            break;
        case JSON_STRING:
            printf("\"%s\"", json_get_string(json, NULL));
            break;
        case JSON_ARRAY:
            pretty_array(json);
            break;
        case JSON_OBJECT:
            pretty_object(json);
            break;
        case JSON_OBJECT_END:
        case JSON_ARRAY_END:
            return;
        case JSON_ERROR:
            printf("exiting %d\n", type);
            fprintf(stderr, "%s\n", json_get_error(json));
            exit(EXIT_FAILURE);
    }
}

long
read_file(const char* fname, char** content)
{
    FILE *f;
    long fsize;

    f = fopen(fname, "rb");
    if (!f) goto err;
    if (0 != fseek(f, 0, SEEK_END)) goto err;
    fsize = ftell(f);
    if (fsize == -1L) goto err;
    if (0 != fseek(f, 0, SEEK_SET)) goto err;
    *content = malloc(fsize + 1);
    fread(*content, fsize, 1, f);
    if (ferror(f)) goto err;
    fclose(f);
    (*content)[fsize] = 0;
    return fsize;

err:
    perror(strerror(errno));
    return -1;
}

static int
file_fgetc(void *arg)
{
    return fgetc(arg);
}

struct buffer {
    const void *buf;
    size_t len;
    size_t pos;
};

static int
buffer_fgetc(void *arg)
{
    const unsigned char *buf;
    struct buffer *b = arg;
    if (b->pos == b->len)
        return -1;
    buf = b->buf;
    return buf[b->pos++];
}

int
main(int argc, char **argv)
{
    struct json_stream json;
    char *jstr = NULL;

    if (argc < 2) {
        json_open(&json, file_fgetc, stdin, JSON_FLAG_STREAMING);
    } else {
        struct buffer buffer;
        if (-1 == read_file(argv[1], &jstr)) {
            free(jstr);
            exit(EXIT_FAILURE);
        }
        buffer.buf = jstr;
        buffer.len = strlen(jstr);
        buffer.pos = 0;
        json_open(&json, buffer_fgetc, &buffer, JSON_FLAG_STREAMING);
    }

    pretty(&json);
    if (json_get_error(&json)) {
        fprintf(stderr, "%s\n", json_get_error(&json));
        exit(EXIT_FAILURE);
    } else {
        printf("\n");
    }
    json_close(&json);
    free(jstr);
    return 0;
}
