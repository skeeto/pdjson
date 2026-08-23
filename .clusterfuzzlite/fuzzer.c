#include <stdint.h>
#include "pdjson.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    json_stream stream;
    json_open_buffer(&stream, (const char *)data, size);
    json_skip(&stream);
    json_close(&stream);
    
    return 0;
}
