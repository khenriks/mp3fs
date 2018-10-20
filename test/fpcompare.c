#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chromaprint.h>
#include <sox.h>

#define NUM_SAMPLES 2048

int decode_audio_file(ChromaprintContext *ctx, const char *file_name) {
    sox_format_t* in_fmt = sox_open_read(file_name, NULL, NULL, NULL);
    if (!in_fmt) {
        fprintf(stderr, "ERROR: couldn't open file for read\n");
        return 0;
    }

    chromaprint_start(ctx, (int)in_fmt->signal.rate, in_fmt->signal.channels);

    sox_sample_t samples[NUM_SAMPLES];
    int16_t conv_samples[NUM_SAMPLES];

    size_t number_read;
    size_t c = 0;  // unused
    while ((number_read = sox_read(in_fmt, samples, NUM_SAMPLES)) > 0) {
        for (size_t i = 0; i < number_read; i++) {
            SOX_SAMPLE_LOCALS;
            conv_samples[i] = SOX_SAMPLE_TO_SIGNED_16BIT(samples[i], c);
        }

        if (!chromaprint_feed(ctx, conv_samples, (int)number_read)) {
            fprintf(stderr, "ERROR: fingerprint data ingestion failed\n");
            goto error;
        }
    }

    if (!chromaprint_finish(ctx)) {
        fprintf(stderr, "ERROR: fingerprint calculation failed\n");
        goto error;
    }

    return 1;

error:
    sox_close(in_fmt);
    return 0;
}

int main(int argc, char **argv)
{
    int num_file_names = 0;
    int raw_fingerprint_size[2];
    uint32_t *raw_fingerprints[2] = {0};
    char* file_names[2];
    ChromaprintContext *chromaprint_ctx;
    int algo = CHROMAPRINT_ALGORITHM_DEFAULT, num_failed = 0;

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (!strcmp(arg, "-algo") && i + 1 < argc) {
            const char *v = argv[++i];
            if (!strcmp(v, "test1")) { algo = CHROMAPRINT_ALGORITHM_TEST1; }
            else if (!strcmp(v, "test2")) { algo = CHROMAPRINT_ALGORITHM_TEST2; }
            else if (!strcmp(v, "test3")) { algo = CHROMAPRINT_ALGORITHM_TEST3; }
            else if (!strcmp(v, "test4")) { algo = CHROMAPRINT_ALGORITHM_TEST4; }
            else {
                fprintf(stderr, "WARNING: unknown algorithm, using the default\n");
            }
        }
        else if (!strcmp(arg, "-set") && i + 1 < argc) {
            i += 1;
        }
        else if (num_file_names < 2) {
            file_names[num_file_names++] = argv[i];
        }
    }

    if (num_file_names != 2) {
            printf("usage: %s [OPTIONS] FILE1 FILE2\n\n", argv[0]);
            return 2;
    }

    sox_format_init();

    chromaprint_ctx = chromaprint_new(algo);

    for (int i = 1; i < argc; i++) {
        char *arg = argv[i];
        if (!strcmp(arg, "-set") && i + 1 < argc) {
            char *name = argv[++i];
            char *value = strchr(name, '=');
            if (value) {
                *value++ = '\0';
                chromaprint_set_option(chromaprint_ctx, name, atoi(value));
            }
        }
    }

    for (int i = 0; i < num_file_names; i++) {
        char* file_name = file_names[i];
        if (!decode_audio_file(chromaprint_ctx, file_name)) {
            fprintf(stderr, "ERROR: unable to calculate fingerprint for file %s, skipping\n", file_name);
            num_failed++;
            continue;
        }
        if (!chromaprint_get_raw_fingerprint(chromaprint_ctx, &raw_fingerprints[i], &raw_fingerprint_size[i])) {
            fprintf(stderr, "ERROR: unable to calculate fingerprint for file %s, skipping\n", file_name);
            num_failed++;
            continue;
        }
    }

    if (num_failed == 0) {
        int size_sum = raw_fingerprint_size[0] + raw_fingerprint_size[1];
        int setbits = 0;

        int i;
        for (i = 0;
             i < raw_fingerprint_size[0] && i < raw_fingerprint_size[1];
             i++) {
            int32_t thisdiff = raw_fingerprints[0][i] ^ raw_fingerprints[1][i];
            setbits += __builtin_popcount(thisdiff);
        }
        setbits += (size_sum - 2 * i) * 32;
        printf("%f\n", setbits/((size_sum - i) * 32.0));
    } else {
        fprintf(stderr, "ERROR: Couldn't calculate both fingerprints; can't compare.\n");
        printf("1.0\n");
    }

    if (raw_fingerprints[0]) chromaprint_dealloc(raw_fingerprints[0]);
    if (raw_fingerprints[1]) chromaprint_dealloc(raw_fingerprints[1]);

    chromaprint_free(chromaprint_ctx);
    sox_format_quit();

    return num_failed ? 1 : 0;
}
