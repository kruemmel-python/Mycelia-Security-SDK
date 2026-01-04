#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

// --- SHA-256 / HMAC / HKDF ---
struct Sha256Ctx {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t buffer[64];
    size_t buffer_len;
};

static const uint32_t kSha256Init[8] = {
    0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
    0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
};

static const uint32_t kSha256K[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
    0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
    0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
    0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
    0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
    0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
};

static uint32_t rotr(uint32_t v, uint32_t r) {
    return (v >> r) | (v << (32u - r));
}

static void sha256_init(struct Sha256Ctx *ctx) {
    memcpy(ctx->state, kSha256Init, sizeof(kSha256Init));
    ctx->bitlen = 0;
    ctx->buffer_len = 0;
}

static void sha256_transform(struct Sha256Ctx *ctx, const uint8_t data[64]) {
    uint32_t m[64];
    for (int i = 0; i < 16; ++i) {
        m[i] = ((uint32_t)data[i * 4] << 24) |
               ((uint32_t)data[i * 4 + 1] << 16) |
               ((uint32_t)data[i * 4 + 2] << 8) |
               (uint32_t)data[i * 4 + 3];
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(m[i - 15], 7) ^ rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
        uint32_t s1 = rotr(m[i - 2], 17) ^ rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + kSha256K[i] + m[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_update(struct Sha256Ctx *ctx, const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        ctx->buffer[ctx->buffer_len++] = data[i];
        if (ctx->buffer_len == 64) {
            sha256_transform(ctx, ctx->buffer);
            ctx->bitlen += 512;
            ctx->buffer_len = 0;
        }
    }
}

static void sha256_final(struct Sha256Ctx *ctx, uint8_t out[32]) {
    uint64_t bitlen = ctx->bitlen + (uint64_t)ctx->buffer_len * 8;

    ctx->buffer[ctx->buffer_len++] = 0x80;
    if (ctx->buffer_len > 56) {
        while (ctx->buffer_len < 64) {
            ctx->buffer[ctx->buffer_len++] = 0x00;
        }
        sha256_transform(ctx, ctx->buffer);
        ctx->buffer_len = 0;
    }
    while (ctx->buffer_len < 56) {
        ctx->buffer[ctx->buffer_len++] = 0x00;
    }

    for (int i = 7; i >= 0; --i) {
        ctx->buffer[ctx->buffer_len++] = (uint8_t)((bitlen >> (i * 8)) & 0xFFu);
    }

    sha256_transform(ctx, ctx->buffer);

    for (int i = 0; i < 8; ++i) {
        out[i * 4] = (uint8_t)((ctx->state[i] >> 24) & 0xFFu);
        out[i * 4 + 1] = (uint8_t)((ctx->state[i] >> 16) & 0xFFu);
        out[i * 4 + 2] = (uint8_t)((ctx->state[i] >> 8) & 0xFFu);
        out[i * 4 + 3] = (uint8_t)(ctx->state[i] & 0xFFu);
    }
}

static void hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t data_len, uint8_t out[32]) {
    uint8_t key_block[64];
    if (key_len > 64) {
        struct Sha256Ctx ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, key, key_len);
        sha256_final(&ctx, key_block);
        memset(key_block + 32, 0, 32);
    } else {
        memcpy(key_block, key, key_len);
        if (key_len < 64) {
            memset(key_block + key_len, 0, 64 - key_len);
        }
    }

    uint8_t o_key_pad[64];
    uint8_t i_key_pad[64];
    for (int i = 0; i < 64; ++i) {
        o_key_pad[i] = key_block[i] ^ 0x5cu;
        i_key_pad[i] = key_block[i] ^ 0x36u;
    }

    struct Sha256Ctx inner;
    sha256_init(&inner);
    sha256_update(&inner, i_key_pad, 64);
    sha256_update(&inner, data, data_len);
    uint8_t inner_hash[32];
    sha256_final(&inner, inner_hash);

    struct Sha256Ctx outer;
    sha256_init(&outer);
    sha256_update(&outer, o_key_pad, 64);
    sha256_update(&outer, inner_hash, 32);
    sha256_final(&outer, out);
}

static void hkdf_sha256(const uint8_t *ikm, size_t ikm_len,
                        const uint8_t *salt, size_t salt_len,
                        const uint8_t *info, size_t info_len,
                        uint8_t *out, size_t out_len) {
    uint8_t prk[32];
    hmac_sha256(salt, salt_len, ikm, ikm_len, prk);

    uint8_t t[32];
    size_t t_len = 0;
    uint8_t counter = 1;
    size_t offset = 0;

    while (offset < out_len) {
        size_t data_len = t_len + info_len + 1;
        uint8_t *data = (uint8_t *)malloc(data_len);
        if (!data) {
            return;
        }
        if (t_len > 0) {
            memcpy(data, t, t_len);
        }
        if (info_len > 0) {
            memcpy(data + t_len, info, info_len);
        }
        data[data_len - 1] = counter;

        hmac_sha256(prk, 32, data, data_len, t);
        free(data);
        t_len = 32;

        size_t to_copy = (out_len - offset < t_len) ? (out_len - offset) : t_len;
        memcpy(out + offset, t, to_copy);
        offset += to_copy;
        ++counter;
    }
}

static void derive_key_nonce(uint64_t seed, uint32_t key[8], uint32_t nonce[3]) {
    uint8_t ikm[8];
    for (int i = 0; i < 8; ++i) {
        ikm[i] = (uint8_t)((seed >> (8 * i)) & 0xFFu);
    }
    const char salt[] = "mycelia-hkdf-salt";
    const char info[] = "mycelia-chacha20";
    uint8_t out[44];
    hkdf_sha256(ikm, sizeof(ikm), (const uint8_t *)salt, sizeof(salt) - 1,
                (const uint8_t *)info, sizeof(info) - 1, out, sizeof(out));

    for (int i = 0; i < 8; ++i) {
        key[i] = (uint32_t)out[i * 4] |
                 ((uint32_t)out[i * 4 + 1] << 8) |
                 ((uint32_t)out[i * 4 + 2] << 16) |
                 ((uint32_t)out[i * 4 + 3] << 24);
    }
    size_t nonce_offset = 32;
    for (int i = 0; i < 3; ++i) {
        nonce[i] = (uint32_t)out[nonce_offset + i * 4] |
                   ((uint32_t)out[nonce_offset + i * 4 + 1] << 8) |
                   ((uint32_t)out[nonce_offset + i * 4 + 2] << 16) |
                   ((uint32_t)out[nonce_offset + i * 4 + 3] << 24);
    }
}

// --- ChaCha20 ---
static uint32_t rotl32(uint32_t v, uint32_t r) {
    return (v << r) | (v >> (32u - r));
}

static void quarter_round(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    *a += *b; *d ^= *a; *d = rotl32(*d, 16);
    *c += *d; *b ^= *c; *b = rotl32(*b, 12);
    *a += *b; *d ^= *a; *d = rotl32(*d, 8);
    *c += *d; *b ^= *c; *b = rotl32(*b, 7);
}

static void chacha20_block(uint32_t key[8], uint32_t nonce[3], uint32_t counter, uint32_t out[16]) {
    uint32_t state[16] = {
        0x61707865u, 0x3320646eu, 0x79622d32u, 0x6b206574u,
        key[0], key[1], key[2], key[3],
        key[4], key[5], key[6], key[7],
        counter, nonce[0], nonce[1], nonce[2]
    };
    uint32_t working[16];
    memcpy(working, state, sizeof(state));

    for (int i = 0; i < 10; ++i) {
        quarter_round(&working[0], &working[4], &working[8], &working[12]);
        quarter_round(&working[1], &working[5], &working[9], &working[13]);
        quarter_round(&working[2], &working[6], &working[10], &working[14]);
        quarter_round(&working[3], &working[7], &working[11], &working[15]);

        quarter_round(&working[0], &working[5], &working[10], &working[15]);
        quarter_round(&working[1], &working[6], &working[11], &working[12]);
        quarter_round(&working[2], &working[7], &working[8], &working[13]);
        quarter_round(&working[3], &working[4], &working[9], &working[14]);
    }

    for (int i = 0; i < 16; ++i) {
        out[i] = working[i] + state[i];
    }
}

static uint8_t keystream_byte(uint32_t key[8], uint32_t nonce[3], uint32_t counter, uint32_t byte_in_block) {
    uint32_t block[16];
    chacha20_block(key, nonce, counter, block);
    uint32_t word = block[byte_in_block / 4u];
    uint32_t shift = (byte_in_block & 3u) * 8u;
    return (uint8_t)((word >> shift) & 0xFFu);
}

static void chacha20_xor(uint8_t *data, size_t len, uint32_t key[8], uint32_t nonce[3], uint64_t stream_offset) {
    uint64_t block_index = stream_offset / 64u;
    uint32_t counter_base = (uint32_t)(block_index & 0xFFFFFFFFu);
    uint32_t counter_high = (uint32_t)((block_index >> 32) & 0xFFFFFFFFu);
    nonce[2] ^= counter_high;
    uint32_t offset_in_block = (uint32_t)(stream_offset % 64u);

    for (size_t i = 0; i < len; ++i) {
        uint32_t absolute_in_block = offset_in_block + (uint32_t)i;
        uint32_t block_offset = absolute_in_block / 64u;
        uint32_t byte_in_block = absolute_in_block % 64u;
        uint32_t counter = counter_base + block_offset;
        uint8_t ks = keystream_byte(key, nonce, counter, byte_in_block);
        data[i] ^= ks;
    }
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <seed_uint64> <stream_offset> <hex_plaintext>\n", argv[0]);
        return 1;
    }

    uint64_t seed = strtoull(argv[1], NULL, 10);
    uint64_t stream_offset = strtoull(argv[2], NULL, 10);
    const char *hex = argv[3];

    uint8_t *data = NULL;
    size_t data_len = 0;
    if (!hex_to_bytes(hex, &data, &data_len)) {
        fprintf(stderr, "Invalid hex input\n");
        return 1;
    }

    uint32_t key[8];
    uint32_t nonce[3];
    derive_key_nonce(seed, key, nonce);
    chacha20_xor(data, data_len, key, nonce, stream_offset);

    bytes_to_hex(data, data_len, stdout);
    fprintf(stdout, "\n");

    free(data);
    return 0;
}
