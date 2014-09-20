#include <stdio.h>
#include "json.h"

int main()
{
    //const char *data = "{\"a\": \"b\"}";
    json_stream_t json;
    json_open_string(&json, "10.4e10");
    printf("%d == %d\n", json_next(&json), JSON_NULL);
    printf("%s\n", json_get_string(&json, NULL));
    printf("%f\n", json_get_number(&json));
    printf("%d == %d\n", json_next(&json), JSON_DONE);
    const char *err = json_get_error(&json);
    printf("%s, lines %ld\n", err ? err : "Success", json_get_lineno(&json));
    json_close(&json);
    return 0;
}
