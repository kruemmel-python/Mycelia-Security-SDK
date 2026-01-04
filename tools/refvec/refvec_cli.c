#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../include/mycelia.h"

static int hex_to_bytes(const char *hex, uint8_t **out, size_t *out_len) {
    size_t len = strlen(hex);
    if (len % 2 != 0) {
        return 0;
    }
    size_t bytes = len / 2;
    uint8_t *buf = (uint8_t *)malloc(bytes);
    if (!buf) {
        return 0;
    }
    for (size_t i = 0; i < bytes; ++i) {
        unsigned int value = 0;
        if (sscanf(hex + i * 2, "%2x", &value) != 1) {
            free(buf);
            return 0;
        }
        buf[i] = (uint8_t)value;
    }
    *out = buf;
    *out_len = bytes;
    return 1;
}

static void bytes_to_hex(const uint8_t *data, size_t len, FILE *out) {
    for (size_t i = 0; i < len; ++i) {
        fprintf(out, "%02x", data[i]);
    }
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <seed_uint64> <stream_offset> <hex_plaintext> <gpu_index>\n", argv[0]);
        return 1;
    }

    uint64_t seed = strtoull(argv[1], NULL, 10);
    uint64_t stream_offset = strtoull(argv[2], NULL, 10);
    const char *hex = argv[3];
    int gpu_index = atoi(argv[4]);

    uint8_t *data = NULL;
    size_t data_len = 0;
    if (!hex_to_bytes(hex, &data, &data_len)) {
        fprintf(stderr, "Invalid hex input\n");
        return 1;
    }

    if (myc_init() != MYC_SUCCESS) {
        fprintf(stderr, "myc_init failed: %s\n", myc_get_last_error());
        free(data);
        return 1;
    }

    myc_context_t ctx = NULL;
    if (myc_create_context(gpu_index, &ctx) != MYC_SUCCESS) {
        fprintf(stderr, "myc_create_context failed: %s\n", myc_get_last_error());
        free(data);
        return 1;
    }

    if (myc_set_seed(ctx, seed) != MYC_SUCCESS) {
        fprintf(stderr, "myc_set_seed failed: %s\n", myc_get_last_error());
        myc_destroy_context(ctx);
        free(data);
        return 1;
    }

    if (myc_process_buffer(ctx, data, data_len, (size_t)stream_offset) != MYC_SUCCESS) {
        fprintf(stderr, "myc_process_buffer failed: %s\n", myc_get_last_error());
        myc_destroy_context(ctx);
        free(data);
        return 1;
    }

    bytes_to_hex(data, data_len, stdout);
    fprintf(stdout, "\n");

    myc_destroy_context(ctx);
    free(data);
    return 0;
}
