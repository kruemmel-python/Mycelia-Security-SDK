#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "../../android/app/src/main/cpp/mycelia_vulkan_compute.h"

static int hex_to_bytes(const char *hex, std::vector<uint8_t> &out) {
    out.clear();
    size_t n = strlen(hex);
    if ((n % 2) != 0) return 0;
    out.reserve(n / 2);
    for (size_t i = 0; i < n; i += 2) {
        char a = hex[i];
        char b = hex[i + 1];
        auto v = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
            if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
            return -1;
        };
        int hi = v(a);
        int lo = v(b);
        if (hi < 0 || lo < 0) return 0;
        out.push_back(static_cast<uint8_t>((hi << 4) | lo));
    }
    return 1;
}

static void bytes_to_hex(const std::vector<uint8_t> &in) {
    static const char *hx = "0123456789abcdef";
    for (uint8_t b : in) {
        putchar(hx[(b >> 4) & 0xF]);
        putchar(hx[b & 0xF]);
    }
    putchar('\n');
}

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr,
                "Usage: %s <shader_dir> <seed_uint64> <stream_offset> <hex_plaintext> <mode>\n"
                "mode: enc|dec\n",
                argv[0]);
        return 1;
    }

    std::string shader_dir = argv[1];
    uint64_t seed_u64 = strtoull(argv[2], nullptr, 10);
    uint64_t stream_offset = strtoull(argv[3], nullptr, 10);
    const char *hex_plain = argv[4];
    const char *mode = argv[5];

    std::vector<uint8_t> input;
    if (!hex_to_bytes(hex_plain, input)) {
        fprintf(stderr, "Invalid hex_plaintext\n");
        return 2;
    }

    std::vector<uint8_t> seed_bytes(8, 0);
    for (int i = 0; i < 8; ++i) {
        seed_bytes[i] = static_cast<uint8_t>((seed_u64 >> (8u * i)) & 0xFFu);
    }

    MyceliaVulkanCompute compute(shader_dir);
    if (!compute.initialize()) {
        fprintf(stderr, "Failed to init Vulkan compute\n");
        return 3;
    }

    std::vector<uint8_t> output;
    bool ok = false;
    if (strcmp(mode, "enc") == 0) {
        ok = compute.encrypt(input, output, seed_bytes, stream_offset);
    } else if (strcmp(mode, "dec") == 0) {
        ok = compute.decrypt(input, output, seed_bytes, stream_offset);
    } else {
        fprintf(stderr, "Invalid mode: %s\n", mode);
        return 4;
    }

    if (!ok) {
        fprintf(stderr, "Compute failed\n");
        return 5;
    }

    bytes_to_hex(output);
    compute.shutdown();
    return 0;
}
