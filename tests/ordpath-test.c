#include <getopt.h>
#include <err.h>
#include <inttypes.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ordpath.h"

/*
 * Configuration
 */

#define UTILITY_VERSION        "0.0.1"

#define SETUP_LEN_MAX          1024
#define LABEL_LEN_MAX          8193
#define ELABEL_BITLEN_MAX      (LABEL_LEN_MAX * 64)
#define BENCHMARK_LOOP_COUNT   16384

/*
 * Utility macros
 */

#define SZ_FROM_BITLEN(l)      (((l)+CHAR_BIT-1)/CHAR_BIT)

#define BENCHMARK_LOOP_DO_NOT_OPTIMIZE() \
    do { asm volatile ("":::"memory"); } while (0)

#define TS2D(ts) \
    ((double)ts.tv_sec + (double)ts.tv_nsec/1000000000.0)

/*
 * Structures
 */

struct range {
    int64_t                    min;
    int64_t                    max;
};

struct label {
    size_t                     len;
    int64_t                    data[ELABEL_BITLEN_MAX];
};

struct elabel {
    size_t                     bitlen;
    char                       padding [ORDPATH_BUF_ALIGNMENT];
    char                       reserved [SZ_FROM_BITLEN(ELABEL_BITLEN_MAX)];
};

#define ELABEL_BUF(e) \
    (char *)((uintptr_t)(e)->reserved & ~(uintptr_t)(ORDPATH_BUF_ALIGNMENT-1))

/*
 * Helper functions
 */

static void read_setup(const char *name, char setup[SETUP_LEN_MAX])
{
    FILE *file;
    size_t size;
    if (!(file = fopen(name, "rt"))) {
        err(EXIT_FAILURE, "Error opening \"%s\"", name);
    }
    size = fread(setup, 1, SETUP_LEN_MAX - 1, file);
    setup[size] = 0;
    if (strlen(setup) != size || !feof(file)) {
        errx(EXIT_FAILURE, "Bad setup \"%s\"", name);
    }
    fclose(file);
}

static int eq_labels(const struct label *l1, const struct label *l2)
{
    return l1->len == l2->len
        && memcmp(l1->data, l2->data, l1->len * sizeof l1->data[0]) == 0;
}

static void read_label(struct label *label, FILE *file, const struct range *r)
{
    int64_t c;
    for (size_t i=0; i<LABEL_LEN_MAX; i++) {
        if (1 != fscanf(file, "%"SCNd64" ", &c)) {
            if (!feof(file)) {
                errx(EXIT_FAILURE, "Bad label");
            }
            label->len = i;
            return;
        }
        if (c < r->min || c > r->max) {
            errx(EXIT_FAILURE,
                "Label component %"PRId64" "
                "not in valid range [%"PRId64",%"PRId64"]",
                c, r->min, r->max);
        }
        label->data[i] = c;
    }
    errx(EXIT_FAILURE, "Label too long");
}

static void write_label(const struct label *label, FILE *file)
{
    for (size_t i=0; i < label->len; i++) {
        fprintf(file, "%"PRId64"\n", label->data[i]);
    }
}

static int eq_elabels(const struct elabel *el1, const struct elabel *el2)
{
    return el1->bitlen == el2->bitlen
        && memcmp(ELABEL_BUF(el1), ELABEL_BUF(el2), SZ_FROM_BITLEN(el1->bitlen)) == 0;
}

static void read_elabel(struct elabel *elabel, FILE *file)
{
    char buf[64];
    size_t size, bitlen;
    if (!fgets(buf, sizeof buf, file) || !strchr(buf, '\n')) {
        goto badlabel;
    }
    if (1 != sscanf(buf, "%zu", &bitlen)) {
        goto badlabel;
    }
    if (bitlen > ELABEL_BITLEN_MAX) {
        errx(EXIT_FAILURE, "Encoded label too long");
    }
    size = SZ_FROM_BITLEN(bitlen);
    if (size != fread(ELABEL_BUF(elabel), 1, size, file)) {
        goto badlabel;
    }
    elabel->bitlen = bitlen;
    return;
badlabel:
    errx(EXIT_FAILURE, "Bad encoded label");
}

static void write_elabel(const struct elabel *elabel, FILE *file)
{
    fprintf(file, "%-15zu\n", elabel->bitlen);
    fwrite(ELABEL_BUF(elabel), 1, SZ_FROM_BITLEN(elabel->bitlen), file);
}

static void memcpy_benchmark(int n, const struct label *l)
{
    static struct label t;
    int i;
    for (i=0; i<n; i++) {
        memcpy(t.data, l->data, l->len * sizeof l->data[0]);

        BENCHMARK_LOOP_DO_NOT_OPTIMIZE();
    }
}

static void encoding_benchmark(int n, const struct label *l,
    ordpath_codec_t *codec)
{
    static struct elabel et;
    int i;
    for (i=0; i<n; i++) {
        ordpath_encode(codec, l->data, l->len, ELABEL_BUF(&et), &et.bitlen);

        BENCHMARK_LOOP_DO_NOT_OPTIMIZE();
    }
}

static void decoding_benchmark(int n, const struct elabel *el,
    ordpath_codec_t *codec)
{
    static struct label t;
    int i;
    for (i=0; i<n; i++) {
        ordpath_decode(codec, ELABEL_BUF(el), el->bitlen, t.data, &t.len);

        BENCHMARK_LOOP_DO_NOT_OPTIMIZE();
    }
}

/*
 * Here it goes
 */

int main(int argc, char **argv)
{
    enum options {
        OPT_VERSION = 1000,
        OPT_ENCODE,
        OPT_DECODE,
        OPT_BENCHMARK,
        OPT_SETUP,
        OPT_REFDATA
    };

    static const struct option options[] = {
        {"version", 0, NULL, OPT_VERSION},
        {"encode", 0, NULL, OPT_ENCODE},
        {"decode", 0, NULL, OPT_DECODE},
        {"benchmark", 0, NULL, OPT_BENCHMARK},
        {"setup", 1, NULL, OPT_SETUP},
        {"reference-data", 1, NULL, OPT_REFDATA},
        {NULL, 0, NULL, 0}
    };

    int benchmark = 0;
    const char *refdata = NULL;
    enum {MODE_ENCODE = 1, MODE_DECODE} mode = 0;
    ordpath_codec_t *codec = NULL;
    const char *setupname = "<builtin-setup>";
    char setup[SETUP_LEN_MAX] = "\
        0000001 : 48     \
        0000010 : 32     \
        0000011 : 16     \
        000010  : 12     \
        000011  : 8      \
        00010   : 6      \
        00011   : 4      \
        001     : 3      \
        01      : 3 : 0  \
        100     : 4      \
        101     : 6      \
        1100    : 8      \
        1101    : 12     \
        11100   : 16     \
        11101   : 32     \
        11110   : 48";
    struct range r;
    ordpath_status_t status;
    static struct label label;
    static struct elabel elabel;
    char errorbuf[96];
    int opt;

    while (-1 != (opt = getopt_long(argc, argv, "", options, NULL))) {
        switch (opt) {
        case OPT_VERSION:
            printf(
                "ordpath testing utility %s\n"
                "\n"
                "ordpath library compile options: %s\n",
                UTILITY_VERSION,
                ordpath_compile_options);
            return EXIT_SUCCESS;
        case OPT_ENCODE:
            mode = MODE_ENCODE;
            break;
        case OPT_DECODE:
            mode = MODE_DECODE;
            break;
        case OPT_BENCHMARK:
            benchmark = 1;
            break;
        case OPT_SETUP:
            setupname = optarg;
            read_setup(setupname, setup);
            break;
        case OPT_REFDATA:
            refdata = optarg;
            break;
        }
    }
    argc -= optind;
    argv += optind;

    if (mode != MODE_ENCODE && mode != MODE_DECODE) {
        errx(EXIT_FAILURE,
            "Please select mode (pass either --encode or --decode option)");
    }

    switch (argc) {
    default:
        errx(EXIT_FAILURE, "Too many arguments");
    case 2:
        if (strcmp(argv[0], "-") != 0 &&  !freopen(argv[1], "wb", stdout)) {
            err(EXIT_FAILURE, "Unable to open \"%s\" for writing", argv[1]);
        }
        /* fallthrough */
    case 1:
        if (strcmp(argv[0], "-") != 0 && !freopen(argv[0], "rb", stdin)) {
            err(EXIT_FAILURE, "Unable to open \"%s\" for reading", argv[0]);
        }
        /* fallthrough */
    case 0:
        break;
    }

    if (ORDPATH_SUCCESS !=
            (status = ordpath_create(&codec, setup, &r.min))) {
        ordpath_strerror(status, errorbuf, sizeof errorbuf);
        errx(EXIT_FAILURE, "Failed to initialize codec from \"%s\": %s",
            setupname, errorbuf);
    }

    if (mode == MODE_ENCODE) {
        read_label(&label, stdin, &r);
        if (ORDPATH_SUCCESS !=
                (status = ordpath_encode(
                        codec,
                        label.data, label.len,
                        ELABEL_BUF(&elabel), &elabel.bitlen))) {

            ordpath_strerror(status, errorbuf, sizeof errorbuf);
            errx(EXIT_FAILURE, "Encoding failed: %s", errorbuf);
        }
    } else {
        read_elabel(&elabel, stdin);
        if (ORDPATH_SUCCESS !=
                (status = ordpath_decode(
                        codec,
                        ELABEL_BUF(&elabel), elabel.bitlen,
                        label.data, &label.len))) {

            ordpath_strerror(status, errorbuf, sizeof errorbuf);
            errx(EXIT_FAILURE, "Decoding failed: %s", errorbuf);
        }
    }

    if (benchmark) {
        double times[4];
        for (int i = 1; i<4; i++) {
            const char *title;
            struct timespec ts_before = {0}, ts_after = {0};
            clock_gettime(CLOCK_MONOTONIC, &ts_before);
            switch (i) {
            case 1:
                title = "memcpy (16x)";
                memcpy_benchmark(BENCHMARK_LOOP_COUNT * 16, &label);
                break;
            case 2:
                title = "ordpath_encode";
                encoding_benchmark(BENCHMARK_LOOP_COUNT, &label, codec);
                break;
            case 3:
                title = "ordpath_decode";
                decoding_benchmark(BENCHMARK_LOOP_COUNT, &elabel, codec);
                break;
            }
            clock_gettime(CLOCK_MONOTONIC, &ts_after);
            times[i] = TS2D(ts_after) - TS2D(ts_before);
            printf("%-14s    %8.3lf    %8.1lf\n",
                title, times[i], times[i]/times[1]*16.0);
        }
    } else if (refdata) {
        FILE *file = fopen(refdata, "rb");
        if (!file) {
            err(EXIT_FAILURE, "Unable to open \"%s\" for reading", refdata);
        }
        if (mode == MODE_ENCODE) {
            static struct elabel ref_elabel;
            read_elabel(&ref_elabel, file);
            if (!eq_elabels(&elabel, &ref_elabel)) {
                errx(EXIT_FAILURE, "Result doesn't match reference data");
            }
        } else {
            static struct label ref_label;
            read_label(&ref_label, file, &r);
            if (!eq_labels(&label, &ref_label)) {
                errx(EXIT_FAILURE, "Result doesn't match reference data");
            }
        }
    } else {
        if (mode == MODE_ENCODE) {
            write_elabel(&elabel, stdout);
        } else {
            write_label(&label, stdout);
        }
    }

    ordpath_destroy(codec);
    codec = NULL;
    return EXIT_SUCCESS;
}

