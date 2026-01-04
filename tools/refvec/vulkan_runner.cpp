#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "../../android/app/src/main/cpp/mycelia_vulkan_compute.h"

static int hex_to_bytes(const char *hex, std::vector<uint8_t> &out) {
    size_t len = strlen(hex);
    if (len % 2 != 0) {
        return 0;
    }
    out.resize(len / 2);
    for (size_t i = 0; i < out.size(); ++i) {
        unsigned int value = 0;
        if (sscanf(hex + i * 2, "%2x", &value) != 1) {
            return 0;
        }
        out[i] = static_cast<uint8_t>(value);
    }
    return 1;
}

static void bytes_to_hex(const std::vector<uint8_t> &data) {
    for (uint8_t b : data) {
        printf("%02x", b);
    }
    printf("\n");
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "Usage: %s <shader_dir> <seed_uint64> <stream_offset> <hex_plaintext> <mode>\n", argv[0]);
        fprintf(stderr, "mode: enc|dec\n");
        return 1;
    }

    const char *shader_dir = argv[1];
    uint64_t seed = strtoull(argv[2], NULL, 10);
    uint64_t stream_offset = strtoull(argv[3], NULL, 10);
    const char *hex = argv[4];
    const char *mode = argv[5];

    std::vector<uint8_t> input;
    if (!hex_to_bytes(hex, input)) {
        fprintf(stderr, "Invalid hex input\n");
        return 1;
    }

    std::vector<uint8_t> seed_bytes(8, 0);
    for (int i = 0; i < 8; ++i) {
        seed_bytes[i] = static_cast<uint8_t>((seed >> (8 * i)) & 0xFFu);
    }

    MyceliaVulkanCompute compute(shader_dir);
    if (!compute.initialize()) {
        fprintf(stderr, "Failed to init Vulkan compute\n");
        return 1;
    }

    std::vector<uint8_t> output;
    bool ok = false;
    if (strcmp(mode, "enc") == 0) {
        ok = compute.encrypt(input, output, seed_bytes, stream_offset);
    } else if (strcmp(mode, "dec") == 0) {
        ok = compute.decrypt(input, output, seed_bytes, stream_offset);
    } else {
        fprintf(stderr, "Unknown mode\n");
        return 1;
    }

    if (!ok) {
        fprintf(stderr, "Vulkan compute failed\n");
        return 1;
    }

    bytes_to_hex(output);
    return 0;
}
