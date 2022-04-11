#ifndef PDJSON_H
#define PDJSON_H

#ifdef __cplusplus
extern "C" {
#else
#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
    #include <stdbool.h>
#else
    #ifndef bool
        #define bool int
        #define true 1
        #define false 0
    #endif /* bool */
#endif /* __STDC_VERSION__ */
#endif /* __cplusplus */

#include <stdio.h>
#include <pdjson_base.h>

PDJSON_SYMEXPORT void json_open_stream(json_stream *json, FILE *stream);

#ifdef __cplusplus
} /* extern "C" */
#endif /* __cplusplus */

#endif
