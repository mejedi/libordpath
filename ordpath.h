#ifndef _ORDPATH_H
#define _ORDPATH_H

#include <stdint.h>

extern const char * const ordpath_compile_options;

typedef
enum ordpath_status {
    ORDPATH_SUCCESS = 0,
    ORDPATH_INTERNALERROR = 1,
    ORDPATH_OUTOFMEM = 2,
    ORDPATH_INVAL = 3,
    ORDPATH_SETUPPARSE = 10,
    ORDPATH_SETUPINVAL = 11,
    ORDPATH_SETUPLIMIT = 12,
    ORDPATH_CORRUPTDATA = 20
}
ordpath_status_t;

void
ordpath_strerror(
    ordpath_status_t status,
    char buf[],
    size_t bufsize);

typedef struct ordpath_codec ordpath_codec_t;

ordpath_status_t
ordpath_create(
    ordpath_codec_t **pcodec,
    const char setupstr[],
    int64_t range[]);

void
ordpath_destroy(
    ordpath_codec_t *codec);

#define ORDPATH_BUF_ALIGNMENT               8

ordpath_status_t
ordpath_encode(
    const ordpath_codec_t *codec,
    const int64_t label[],
    size_t lablen,
    char outbuf[],
    size_t *poutbitlen);

ordpath_status_t
ordpath_decode(
    const ordpath_codec_t *codec,
    const char inbuf[],
    size_t inbitlen,
    int64_t label[],
    size_t *plablen);

#endif

