/********************************************************************
 *                         INTERNAL LIMITS
 ********************************************************************/

#define PREFIX_LEN_MAX         8
#define INTERVAL_WIDTH_MAX     (63 - PREFIX_LEN_MAX)
#define INTERVAL_NUM_MAX       20
#define VALID_RANGE_MIN        (INT64_MIN/2)
#define VALID_RANGE_MAX        (INT64_MAX/2)

/********************************************************************
 *                          BORING STUFF
 ********************************************************************/

#define STR(arg)               _STR(arg)
#define _STR(arg)              # arg

#define __LIKELY(cond)         __builtin_expect((cond), 1)
#define __UNLIKELY(cond)       __builtin_expect((cond), 0)

#ifdef NDEBUG
#define DEBUG(format, ...)     (void)0
#else
#define DEBUG(format, ...)     \
    fprintf(stderr, "%s(%s:%d): " format "\n", \
        __FUNCTION__, __FILE__, __LINE__, ## __VA_ARGS__)
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/param.h>

#include <stdint.h>
#include <stdio.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>

#if defined(ORDPATH_SSE2_BITBUF) || defined(ORDPATH_ALT_SSE2_BITBUF) \
    || defined(ORDPATH_SSE2_SEARCHTREE)
#include <emmintrin.h>

#define _ex_byteswapl_epi64(x)                                  \
    _mm_shuffle_epi32(                                          \
        _mm_packus_epi16(                                       \
            _mm_shufflelo_epi16(                                \
                _mm_shufflehi_epi16(                            \
                    _mm_unpacklo_epi8((x), _mm_setzero_si128()),\
                    _MM_SHUFFLE(0, 1, 2, 3)),                   \
                _MM_SHUFFLE(0, 1, 2, 3)),                       \
            _mm_setzero_si128()),                               \
        _MM_SHUFFLE(2, 3, 0, 1))

#endif

#include "ordpath.h"

/********************************************************************
 *                            BITBUF
 ********************************************************************/

#if defined(ORDPATH_SSE2_BITBUF)

/*
 * Use SSE2 instructions exclusively.
 */

typedef __m128i bitbuf_t;

#define bb_zero()            _mm_setzero_si128()
#define bb_load(ptr)         _mm_loadl_epi64((const __m128i *)(ptr))
#define bb_store(ptr, x)     _mm_storel_epi64((__m128i *)(ptr), (x))
#define bb_load_be(ptr)      _ex_byteswapl_epi64(bb_load((ptr)))
#define bb_store_be(ptr, x)  bb_store((ptr), _ex_byteswapl_epi64((x)))
#define bb_to_int(x)         _mm_cvtsi128_si32((x))
#define bb_add(x, y)         _mm_add_epi64((x), (y))
#define bb_sub(x, y)         _mm_sub_epi64((x), (y))
#define bb_or(x, y)          _mm_or_si128((x), (y))
#define bb_shl(x, count)     _mm_slli_epi64((x), (count))
#define bb_shr(x, count)     _mm_srli_epi64((x), (count))
#define bb_cleanup()         ((void)0)

/* SLOW! */
#if 0
#define bb_high_byte(x)      ((_mm_extract_epi16((x), 3) & 0xFFFF) >> 8)
#endif

#elif defined(ORDPATH_ALT_SSE2_BITBUF)

/*
 * Use mostly MMX with ocasional SSE2 instruction.
 * Currently bb_load_be(), bb_store_be(), bb_add() and bb_sub() require
 * SSE2.
 * SLOW!
 */

typedef __m64 bitbuf_t;
#define bb_zero()            _mm_setzero_si64()
#define bb_load(ptr)         (*(const __m64 *)(ptr))
#define bb_store(ptr, x)     (*(__m64 *)(ptr) = (x))
#define bb_load_be(ptr)      \
    _mm_movepi64_pi64(       \
        _ex_byteswapl_epi64(_mm_loadl_epi64((const __m128i *)(ptr))))
#define bb_store_be(ptr, x)  \
    _mm_storel_epi64(        \
        (__m128i *)(ptr), _ex_byteswapl_epi64(_mm_movpi64_epi64((x))))
#define bb_to_int(x)         _mm_cvtsi64_si32((x))
#define bb_add(x, y)         _mm_add_si64((x),(y))
#define bb_sub(x, y)         _mm_sub_si64((x),(y))
#define bb_or(x, y)          _mm_or_si64((x),(y))
#define bb_shl(x, count)     _mm_slli_si64((x), (count))
#define bb_shr(x, count)     _mm_srli_si64((x), (count))
#define bb_cleanup()         _mm_empty()

#else

/*
 * Portable bitbuf implementation.
 */

typedef int64_t bitbuf_t;
#define bb_zero()            INT64_C(0)
#define bb_load(ptr)         (*(ptr))
#define bb_store(ptr, x)     (*(ptr) = (x))
#if (__BYTE_ORDER == __BIG_ENDIAN)
#define bb_load_be(ptr)      bb_load(ptr)
#define bb_store_be(ptr, x)  bb_store(ptr, x)
#elif (__BYTE_ORDER == __LITTLE_ENDIAN)
#define bb_load_be(ptr)      __builtin_bswap64(bb_load(ptr))
#define bb_store_be(ptr, x)  bb_store(ptr, __builtin_bswap64((x)))
#else
#error unknown endian
#endif
#define bb_to_int(x)         ((int)(x))
#define bb_add(x, y)         ((x) + (y))
#define bb_sub(x, y)         ((x) - (y))
#define bb_or(x, y)          ((x) | (y))
#define bb_shl(x, count)     ((x) << (count))
#define bb_shr(x, count)     (int64_t)((uint64_t)(x) >> (count))
#define bb_cleanup()         (void)0

#endif

/********************************************************************
 *                          HERE IT GOES
 ********************************************************************/

const char * const ordpath_compile_options = 2 +
#if defined(ORDPATH_SSE2_BITBUF)
    ", sse2-bitbuf"
#elif defined (ORDPATH_ALT_SSE2_BITBUF)
    ", alt-sse2-bitbuf"
#endif
#if defined(ORDPATH_SSE2_SEARCHTREE)
    ", sse2-search-tree"
#endif
    "\0\0(none)";

typedef ordpath_status_t status_t;
typedef ordpath_codec_t codec_t;

void
ordpath_strerror(
    status_t status,
    char buf[],
    size_t bufsize)
{
    const char *m;
    switch (status) {
    default:
        snprintf(buf, bufsize, "Unknown error %d", status);
        return;

#define STRERROR_ITEM(status, message) \
    case status: m = message; break;

    STRERROR_ITEM (ORDPATH_SUCCESS,       "Success")
    STRERROR_ITEM (ORDPATH_INTERNALERROR, "Internal error")
    STRERROR_ITEM (ORDPATH_OUTOFMEM,      "Out of memory")
    STRERROR_ITEM (ORDPATH_INVAL,         "Invalid parameter")
    STRERROR_ITEM (ORDPATH_SETUPPARSE,    "Unable to parse setup")
    STRERROR_ITEM (ORDPATH_SETUPINVAL,    "Invalid setup")
    STRERROR_ITEM (
            ORDPATH_SETUPLIMIT, "Setup rejected due to internal limits")
    STRERROR_ITEM (ORDPATH_CORRUPTDATA,   "Data corruption detected")
    }

    snprintf(buf, bufsize, "%s", m);
    return;
}

struct setup {
    int64_t             origin; /* first interval origin */
    int                 intervalnum;
    struct intervalsetup {
        int             index;
        int             prefix;
        int             prefixlen;
        int             width;
    }                   intervals [INTERVAL_NUM_MAX];
};

static status_t parse_setup(
    struct setup *setup,
    const char setupstr[])
{
    status_t status = ORDPATH_SUCCESS;
    int strpos = 0;
    int curinterval = 0;
    int isoriginset = 0;
    int64_t rangesize = 0;

#define PREFIX_LEN_PARSE_MAX   64
    char prefixstr[PREFIX_LEN_PARSE_MAX+1];
    size_t prefixlen;
    int prefix;

    unsigned intervalwidth;
    int64_t intervalorigin, intervalsize;

    memset(setup, 0, sizeof *setup);
    while (setupstr[strpos]) {
        int n;

        if (curinterval >= INTERVAL_NUM_MAX) {
            DEBUG("The number of intervals exceeds %d", INTERVAL_NUM_MAX);
            status = ORDPATH_SETUPLIMIT;
            goto out;
        }

        if (sscanf(setupstr + strpos,
                " %"STR(PREFIX_LEN_PARSE_MAX)"[01] : %u %n",
                prefixstr, &intervalwidth, &n) >= 2) {
            strpos += n;
            prefixlen = strlen(prefixstr);
            if (prefixlen > PREFIX_LEN_MAX) {
                DEBUG("Prefix length exceeds %d bit(s)", PREFIX_LEN_MAX);
                status = ORDPATH_SETUPLIMIT;
                goto out;
            }
            prefix = strtol(prefixstr, NULL, 2);

            /* zero-width interval is permited */
            if (intervalwidth > INTERVAL_WIDTH_MAX) {
                DEBUG("Interval width exceeds %d", INTERVAL_WIDTH_MAX);
                status = ORDPATH_SETUPLIMIT;
                goto out;
            }
            intervalsize = INT64_C(1) << intervalwidth;

            if (sscanf(setupstr + strpos,
                    " : %"SCNd64" %n",
                    &intervalorigin, &n) >= 1) {
                strpos += n;
                if (isoriginset) {
                    DEBUG("Origin already set");
                    status = ORDPATH_SETUPINVAL;
                    goto out;
                }
                if (intervalorigin < VALID_RANGE_MIN
                        || intervalorigin > VALID_RANGE_MAX) {
                    DEBUG("Interval origin too big or too small");
                    status = ORDPATH_SETUPLIMIT;
                    goto out;
                }
                /* doesn't overflow due to INTERVAL_WIDTH_MAX /
                 * INTERVAL_NUM_MAX / VALID_RANGE_MIN / VALID_RANGE_MAX
                 * limits */
                setup->origin = intervalorigin - rangesize;
                isoriginset = 1;
            }

            /* doesn't overflow due to
                * INTERVAL_WIDTH_MAX / INTERVAL_NUM_MAX limits */
            rangesize += intervalsize;

            setup->intervals[curinterval].prefix = prefix;
            setup->intervals[curinterval].prefixlen = prefixlen;
            setup->intervals[curinterval].width = intervalwidth;
            setup->intervals[curinterval].index = curinterval;
            curinterval ++;

            continue;
        }
        DEBUG("Parse error");
        status = ORDPATH_SETUPPARSE;
        goto out;
    }
    setup->intervalnum = curinterval;
    if (!isoriginset) {
        DEBUG("Origin not set");
        status = ORDPATH_SETUPINVAL;
        goto out;
    }
    if (setup->origin < VALID_RANGE_MIN || setup->origin > VALID_RANGE_MAX
            || setup->origin + rangesize > VALID_RANGE_MAX) {
        DEBUG("The resulting range exceeds internal limits");
        status = ORDPATH_SETUPLIMIT;
        goto out;
    }
out:
#ifndef NDEBUG
    if (status == ORDPATH_SETUPPARSE) {
        char repbuf[25], *c;
        snprintf(repbuf, sizeof repbuf, "%s", setupstr + strpos);
        if ((c = strchr(repbuf, '\n'))) {
            *c = 0;
        }
        DEBUG("Parse error in setup string at pos %d: %s\"%s\"",
            strpos, strpos ? "..." : "", repbuf);
    }
#endif
    return status;
}

struct ordpath_codec {
    struct interval {
        int64_t                bias;
        int                    bitlen;
    }                          intervals [1 + INTERVAL_NUM_MAX];

#ifdef ORDPATH_SSE2_SEARCHTREE
    __m128i                    intbounds5tree [12];
#if INTERVAL_NUM_MAX > 25
#error INTERVAL_NUM_MAX too high
#endif
#else
#define BOUNDS_HEAP_PRESENT    1
    int                        intboundsnum;
    int64_t                    intboundsheap [INTERVAL_NUM_MAX];
#endif

    uint8_t                    intlookuptab [256];
#if PREFIX_LEN_MAX > 8
#error adjust ordpath_codec.intlookuptab size
#endif

    void                      *mem;
};

#define CODEC_ALIGNMENT        16

#ifdef BOUNDS_HEAP_PRESENT
static void init_bounds_heap(
    codec_t *codec,
    struct setup *setup,
    const int64_t intervalmin[],
    int heappos,
    int *pindex)
{
    /* did we descent too far? */
    if (heappos <= codec->intboundsnum) {
        /* not yet - explore left and right branches recursively */
        init_bounds_heap(codec, setup, intervalmin, heappos*2, pindex);
        codec->intboundsheap[heappos] = intervalmin[++*pindex];
        init_bounds_heap(codec, setup, intervalmin, heappos*2+1, pindex);
    } else {
        /* we did it! */
        setup->intervals[*pindex].index = heappos - codec->intboundsnum;
    }
}
#endif

#ifdef ORDPATH_SSE2_SEARCHTREE
static void init_search_tree(
    codec_t *codec,
    struct setup *setup,
    const int64_t intervalmin[],
    int intervalnum)
{
    int64_t bounds[24], t[4];
    int i, n = intervalnum;
    for (i=0; i<24; i++) {
        bounds[i] = INT64_MIN/2;
    }
    for (i=0; i < n - 1; i++) {
        bounds[24 - (n - 1) + i] = intervalmin[i + 1];
    }
    t[3] = bounds[4];
    t[2] = bounds[9];
    t[1] = bounds[14];
    t[0] = bounds[19];
    memcpy(codec->intbounds5tree, t, sizeof t);
    for (i=0; i<5; i++) {
        t[3] = bounds[i*5 + 0];
        t[2] = bounds[i*5 + 1];
        t[1] = bounds[i*5 + 2];
        t[0] = bounds[i*5 + 3];
        memcpy(codec->intbounds5tree + 10 - 2*i, t, sizeof t);
    }
    for (i=0; i < n; i++) {
        setup->intervals[i].index = n - i;
    }
}
#endif

status_t
ordpath_create(
    codec_t **pcodec,
    const char setupstr[],
    int64_t range[])
{
    status_t status;
    struct setup setup;
    void *mem;
    codec_t *codec = NULL;
    int64_t t;
    int64_t intervalmin [INTERVAL_NUM_MAX + 1] = {0};
    int i, n;

    if (ORDPATH_SUCCESS != (status = parse_setup(&setup, setupstr))) {
        goto out;
    }
    n = setup.intervalnum;
    for (i = 0, t = setup.origin; i < n; i++) {
        intervalmin[i] = t;
        t += (INT64_C(1) << setup.intervals[i].width);
    }
    intervalmin[n] = t;
    if (range) {
        range[0] = intervalmin[0];
        range[1] = intervalmin[n];
    }
    if (!(mem = malloc(sizeof(*codec) + CODEC_ALIGNMENT))) {
        status = ORDPATH_OUTOFMEM;
        goto out;
    }
    codec = (void *)(((uintptr_t)mem + CODEC_ALIGNMENT - 1)
            & ~(uintptr_t)(CODEC_ALIGNMENT - 1));
    memset(codec, 0, sizeof *codec);
    codec->mem = mem;

#ifdef BOUNDS_HEAP_PRESENT
    /*
     * setup codec->intboundsheap, permutes setup->intervals[].index
     */
    codec->intboundsnum = n - 1;
    i = 0;
    init_bounds_heap(codec, &setup, intervalmin, 1, &i);
#endif

#ifdef ORDPATH_SSE2_SEARCHTREE
    /*
     * setup codec->intbounds5tree, permutes setup->intervals[].index
     */
    init_search_tree(codec, &setup, intervalmin, n);
#endif

    /*
     * setup codec->intervals
     */
    codec->intervals[0].bitlen = 10000;
    for (i=0; i<n; i++) {
        struct intervalsetup *is = setup.intervals + i;
        struct interval *in = codec->intervals + is->index;
        in->bias = ((int64_t)is->prefix << is->width) - intervalmin[i];
        in->bitlen = is->prefixlen + is->width;
    }

    /*
     * setup codec->intlookuptab
     */
    for (i=0; i<n; i++) {
        struct intervalsetup *is = setup.intervals + i;
        int freebits, ltind, ltend;
        freebits = PREFIX_LEN_MAX - is->prefixlen;
        ltind = is->prefix << freebits;
        ltend = ltind + (1 << freebits);
        while (ltind < ltend) {
            if (codec->intlookuptab[ltind] != 0) {
                DEBUG("Encoding is not prefix-free");
                status = ORDPATH_SETUPINVAL;
                goto out;
            }
            codec->intlookuptab[ltind++] = is->index;
        }
    }

out:
    if (status != ORDPATH_SUCCESS) {
        ordpath_destroy(codec);
        codec = NULL;
    }
    *pcodec = codec;
    return status;
}

void
ordpath_destroy(
    codec_t *codec)
{
    if (codec) {
        free(codec->mem);
    }
}

status_t
ordpath_encode(
    const codec_t *restrict codec,
    const int64_t *restrict label,
    size_t lablen,
    char *restrict outbuf,
    size_t *restrict poutbitlen)
{
    const int64_t *endlabel = label + lablen;
    int64_t *out = (int64_t *)outbuf;
    bitbuf_t acc;
    int accused = 0;

#ifndef NDEBUG
    /*
     * rejecting unaligned buffer
     */
    if ((uintptr_t)outbuf & (ORDPATH_BUF_ALIGNMENT - 1)) {
        DEBUG("Unaligned buffer, expected alignment %d",
            ORDPATH_BUF_ALIGNMENT);
        return ORDPATH_INVAL;
    }
#endif

    acc = bb_zero();
    while (label < endlabel) {
        bitbuf_t c;
        int intind, bitlen;

#ifdef BOUNDS_HEAP_PRESENT
        /*
         * lookup component in interval heap
         */
        int64_t v = *label;
        int heapind = 1;
        int n = codec->intboundsnum;
        while (__LIKELY(heapind <= n)) {
            heapind = heapind*2 + (int)(v >= codec->intboundsheap[heapind]);
        }
        intind = heapind - n;
#endif

#ifdef ORDPATH_SSE2_SEARCHTREE
        /*
         * lookup component in interval search tree
         */
        __m128i v, deltalo, deltahi, pkdelta, m;
        const __m128i *p;
        int indlo, indhi;

        v = _mm_shuffle_epi32(
                        _mm_loadl_epi64((const __m128i *)label),
                        _MM_SHUFFLE(1, 0, 1, 0));

        p = codec->intbounds5tree;
        deltalo = _mm_sub_epi64(v, p[0]);
        deltahi = _mm_sub_epi64(v, p[1]);
        pkdelta = _mm_packs_epi32(deltalo, deltahi);
        m = _mm_srai_epi32(pkdelta, 31);
        indhi = __builtin_ctz(~_mm_movemask_epi8(m));

        p = (const void *)((uintptr_t)p + 32 + indhi*8);
        deltalo = _mm_sub_epi64(v, p[0]);
        deltahi = _mm_sub_epi64(v, p[1]);
        pkdelta = _mm_packs_epi32(deltalo, deltahi);
        m = _mm_srai_epi32(pkdelta, 31);
        indlo = __builtin_ctz(~_mm_movemask_epi8(m));

        intind = (indhi*5 + indlo + 4) >> 2;
#endif

        /*
         * load label component (again)
         */
        c = bb_load(label++);

        /*
         * apply encoding
         */
        bitlen = codec->intervals[intind].bitlen;
        c = bb_shl(bb_add(c, bb_load(&codec->intervals[intind].bias)),
                64 - bitlen);

        /*
         * combine resulting bitstrings
         */
        acc = bb_or(acc, bb_shr(c, accused));
        accused += bitlen;
        if (__UNLIKELY(accused >= 64)) {
            bb_store_be(out++, acc);
            accused -= 64;
            acc = bb_shl(c, bitlen - accused);
        }
    }
    bb_store_be(out, acc);
    *poutbitlen = CHAR_BIT * ((char *)out - outbuf) + accused;
    bb_cleanup();
    return ORDPATH_SUCCESS;
}

status_t
ordpath_decode(
    const codec_t *restrict codec,
    const char *restrict inbuf,
    size_t inbitlen,
    int64_t *restrict label,
    size_t *restrict plablen)
{
#if defined(bb_high_byte) && PREFIX_LEN_MAX == 8
#define make_tab_ind(x)   bb_high_byte((x))
#else
#define make_tab_ind(x)   bb_to_int(bb_shr((x), 64 - PREFIX_LEN_MAX))
#endif

    int64_t *out = label;
    const int64_t *in;
    bitbuf_t acc;
    int accused, bitlen;

#ifndef NDEBUG
    /*
     * rejecting unaligned buffer
     */
    if ((uintptr_t)inbuf & (ORDPATH_BUF_ALIGNMENT - 1)) {
        DEBUG("Unaligned buffer, expected alignment %d",
            ORDPATH_BUF_ALIGNMENT);
        return ORDPATH_INVAL;
    }
#endif

    in = (const int64_t *)inbuf;
    acc = bb_zero();
    accused = 0;

    while (1) {
        int tabind, intind;
        tabind = make_tab_ind(acc);
        intind = codec->intlookuptab[tabind];
        bitlen = codec->intervals[intind].bitlen;
        if (__LIKELY(accused > bitlen)) {
            bb_store(out++, bb_sub(
                    bb_shr(acc, 64 - bitlen),
                    bb_load(&codec->intervals[intind].bias)));
            accused -= bitlen;
            acc = bb_shl(acc, bitlen);
        } else {
            bitbuf_t c = acc;
            int accused_prev = accused;

            /* fill acc */
            accused = 0;
            if (__LIKELY(inbitlen != 0)) {
                accused = (__LIKELY(inbitlen > 64)) ? 64 : inbitlen;
                acc = bb_load_be(in++);
                inbitlen -= accused;
            }

            /* determine enclosing interval again since more bits are
             * now availible hence results may change */
            c = bb_or(c, bb_shr(acc, accused_prev));
            tabind = make_tab_ind(c);
            intind = codec->intlookuptab[tabind];
            bitlen = codec->intervals[intind].bitlen;

            /* not enough bits? */
            if (__UNLIKELY(bitlen > accused_prev + accused)) {
                *plablen = out - label;
                bb_cleanup();
                /* do we have trailing junk? */
                return (accused + accused_prev == 0) ? ORDPATH_SUCCESS
                        : ORDPATH_CORRUPTDATA;
            }

            bb_store(out++, bb_sub(
                    bb_shr(c, 64 - bitlen),
                    bb_load(&codec->intervals[intind].bias)));
            accused -= bitlen - accused_prev;
            acc = bb_shl(acc, bitlen - accused_prev);
        }
    }

    /* unreached */
}

