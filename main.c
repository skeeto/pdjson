#include <stdio.h>
#include "json.h"

int main()
{
    json_stream_t json;
    json_open_string(&json, "{\"a\": 1, \"b\": null}");
    json_next(&json);
    while (json_next(&json) != JSON_OBJECT_END && !json_get_error(&json)) {
        printf("%s = ", json_get_string(&json, NULL));
        json_next(&json);
        printf("%s\n", json_get_string(&json, NULL));
    }
    printf("%d == %d\n", json_next(&json), JSON_DONE);
    const char *err = json_get_error(&json);
    printf("%s, lines %ld\n", err ? err : "Success", json_get_lineno(&json));
    json_close(&json);
    return 0;
}
