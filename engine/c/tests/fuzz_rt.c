/* libFuzzer target for swara_ring and swara_limiter.
 * Build: clang -std=c11 -g -O1 -fsanitize=fuzzer,address,undefined -Ic/include \
 *        c/tests/fuzz_rt.c c/src/swara_ring.c c/src/swara_limiter.c -lm
 * The fuzzer drives random operation sequences and checks invariants with abort(). */
#include "swara_limiter.h"
#include "swara_ring.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include <stdio.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "invariant violated at line %d: %s\n", __LINE__, #c); abort(); } } while (0)

int LLVMFuzzerTestOneInput(const uint8_t *d, size_t size) {
    if (size < 8) return 0;

    /* ---- ring: random writes and reads, checked against a model counter ---- */
    float storage[16];
    swara_ring r;
    CHECK(swara_ring_init(&r, storage, 16) == SWARA_OK);
    unsigned next_write = 0, next_read = 0;
    size_t used = 0;
    for (size_t i = 0; i + 1 < size / 2; ++i) {
        size_t n = (size_t)(d[2 * i + 1] % 20);
        float buf[20];
        if (d[2 * i] & 1) {
            for (size_t k = 0; k < n; ++k) buf[k] = (float)(next_write + (unsigned)k);
            swara_status s = swara_ring_write(&r, buf, n);
            if (n <= 15U - used) { CHECK(s == SWARA_OK); used += n; next_write += (unsigned)n; }
            else CHECK(s == SWARA_ERR_FULL);
        } else {
            swara_status s = swara_ring_read(&r, buf, n);
            if (n <= used) {
                CHECK(s == SWARA_OK);
                for (size_t k = 0; k < n; ++k) CHECK(buf[k] == (float)(next_read + (unsigned)k));
                used -= n; next_read += (unsigned)n;
            } else CHECK(s == SWARA_ERR_EMPTY);
        }
        CHECK(swara_ring_available(&r) == used);
        CHECK(swara_ring_space(&r) == 15U - used);
    }

    /* ---- limiter: random parameters and random float samples (including NaN/inf) ---- */
    float p[4];
    memcpy(p, d, sizeof p < size ? sizeof p : size);
    swara_limiter l;
    if (swara_limiter_init(&l, p[0], p[1], p[2], p[3]) == SWARA_OK) {
        size_t n = (size - 8) / 4;
        if (n > 256) n = 256;
        float in[256], out[256];
        if (n > 0) memcpy(in, d + 8, n * 4);
        CHECK(swara_limiter_process(&l, in, out, n) == SWARA_OK);
        for (size_t i = 0; i < n; ++i) {
            CHECK(isfinite(out[i]));
            CHECK(fabsf(out[i]) <= l.threshold);
        }
        CHECK(l.gain >= 0.0f && l.gain <= 1.0f); /* NaN fails both comparisons */
    }
    return 0;
}

#ifdef FUZZ_STANDALONE
/* Fallback driver when libFuzzer is unavailable: random inputs from a fixed-seed generator. */
#include <stdio.h>
int main(int argc, char **argv) {
    unsigned long iters = (argc > 1) ? strtoul(argv[1], NULL, 10) : 200000UL;
    uint64_t s = 0x9E3779B97F4A7C15ULL;
    uint8_t buf[1100];
    for (unsigned long it = 0; it < iters; ++it) {
        s ^= s << 13; s ^= s >> 7; s ^= s << 17;
        size_t size = 8 + (size_t)(s % 1090);
        for (size_t i = 0; i < size; ++i) { s ^= s << 13; s ^= s >> 7; s ^= s << 17; buf[i] = (uint8_t)(s >> 24); }
        /* every few inputs, make the limiter parameters and samples valid and interesting */
        if (it % 3 == 0) {
            float thr = 0.1f + (float)(s % 90) / 100.0f, at = 1.0f, rl = 50.0f, sr = 48000.0f;
            memcpy(buf, &thr, 4); memcpy(buf + 4, &at, 4);
            if (size >= 16) { memcpy(buf + 8, &rl, 4); memcpy(buf + 12, &sr, 4); }
        }
        LLVMFuzzerTestOneInput(buf, size);
    }
    printf("%lu inputs, no invariant violated\n", iters);
    return 0;
}
#endif
