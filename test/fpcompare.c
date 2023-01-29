#include <chromaprint.h>
#include <sox.h>
#include <stdint.h>
#include <stdio.h>

const int kNumSamples = 2048;

int decode_audio_file(ChromaprintContext* ctx, const char* file_name) {
    sox_format_t* in_fmt = sox_open_read(file_name, NULL, NULL, NULL);
    if (!in_fmt) {
        (void)fprintf(stderr, "ERROR: couldn't open file for read\n");
        return 0;
    }

    chromaprint_start(ctx, (int)in_fmt->signal.rate,
                      (int)in_fmt->signal.channels);

    sox_sample_t samples[kNumSamples];
    int16_t conv_samples[kNumSamples];

    size_t number_read;
    size_t c = 0;  // unused
    while ((number_read = sox_read(in_fmt, samples, kNumSamples)) > 0) {
        for (size_t i = 0; i < number_read; i++) {
            SOX_SAMPLE_LOCALS;
            conv_samples[i] = SOX_SAMPLE_TO_SIGNED_16BIT(samples[i], c);
        }

        if (!chromaprint_feed(ctx, conv_samples, (int)number_read)) {
            (void)fprintf(stderr, "ERROR: fingerprint data ingestion failed\n");
            goto error;
        }
    }

    if (!chromaprint_finish(ctx)) {
        (void)fprintf(stderr, "ERROR: fingerprint calculation failed\n");
        goto error;
    }

    return 1;

error:
    sox_close(in_fmt);
    return 0;
}

int main(int argc, char** argv) {
    int raw_fingerprint_size[2];
    uint32_t* raw_fingerprints[2] = {0};
    ChromaprintContext* chromaprint_ctx;
    int num_failed = 0;

    if (argc != 3) {
        printf("usage: %s FILE1 FILE2\n\n", argv[0]);
        return 2;
    }

    sox_format_init();

    chromaprint_ctx = chromaprint_new(CHROMAPRINT_ALGORITHM_DEFAULT);

    for (int i = 0; i < 2; i++) {
        char* file_name = argv[i + 1];
        if (!decode_audio_file(chromaprint_ctx, file_name)) {
            (void)fprintf(stderr,
                          "ERROR: unable to calculate fingerprint for file %s, "
                          "skipping\n",
                          file_name);
            num_failed++;
            continue;
        }
        if (!chromaprint_get_raw_fingerprint(chromaprint_ctx,
                                             &raw_fingerprints[i],
                                             &raw_fingerprint_size[i])) {
            (void)fprintf(stderr,
                          "ERROR: unable to calculate fingerprint for file %s, "
                          "skipping\n",
                          file_name);
            num_failed++;
            continue;
        }
    }

    if (num_failed == 0) {
        int size_sum = raw_fingerprint_size[0] + raw_fingerprint_size[1];
        int setbits = 0;

        int i;
        for (i = 0; i < raw_fingerprint_size[0] && i < raw_fingerprint_size[1];
             i++) {
            uint32_t thisdiff = raw_fingerprints[0][i] ^ raw_fingerprints[1][i];
            setbits += __builtin_popcount(thisdiff);
        }
        const int int32_bits = 32;
        setbits += (size_sum - 2 * i) * int32_bits;
        printf("%f\n", setbits / ((size_sum - i) * int32_bits * 1.0));
    } else {
        (void)fprintf(
            stderr,
            "ERROR: Couldn't calculate both fingerprints; can't compare.\n");
        printf("1.0\n");
    }

    chromaprint_dealloc(raw_fingerprints[0]);
    chromaprint_dealloc(raw_fingerprints[1]);

    chromaprint_free(chromaprint_ctx);
    sox_format_quit();

    return num_failed ? 1 : 0;
}
