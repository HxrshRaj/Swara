/* Tests for the swara_rt C11 module. Tiny hand-written runner, no dependencies. */
#include "swara_ring.h"
#include "swara_limiter.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static int g_run = 0, g_failed = 0;
#define CHECK(cond) do { g_run++; if (!(cond)) { g_failed++; \
    printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

/* ---- ring buffer ---- */
static void test_ring_init_validation(void) {
    float buf[8]; swara_ring r;
    CHECK(swara_ring_init(NULL, buf, 8) == SWARA_ERR_ARG);
    CHECK(swara_ring_init(&r, NULL, 8) == SWARA_ERR_ARG);
    CHECK(swara_ring_init(&r, buf, 0) == SWARA_ERR_ARG);
    CHECK(swara_ring_init(&r, buf, 1) == SWARA_ERR_ARG);
    CHECK(swara_ring_init(&r, buf, 6) == SWARA_ERR_ARG);
    CHECK(swara_ring_init(&r, buf, 8) == SWARA_OK);
    CHECK(swara_ring_available(&r) == 0);
    CHECK(swara_ring_space(&r) == 7);
}
static void test_ring_size_helpers(void) {
    CHECK(swara_ring_sizeof() == sizeof(swara_ring));
    CHECK(swara_ring_alignof() >= 1U && (swara_ring_alignof() & (swara_ring_alignof() - 1U)) == 0U);
}
static void test_ring_basic_roundtrip(void) {
    float buf[8], out[4]; swara_ring r;
    const float in[4] = {1.f, 2.f, 3.f, 4.f};
    CHECK(swara_ring_init(&r, buf, 8) == SWARA_OK);
    CHECK(swara_ring_write(&r, in, 4) == SWARA_OK);
    CHECK(swara_ring_available(&r) == 4);
    CHECK(swara_ring_read(&r, out, 4) == SWARA_OK);
    CHECK(memcmp(in, out, sizeof in) == 0);
    CHECK(swara_ring_available(&r) == 0);
}
static void test_ring_full_and_empty(void) {
    float buf[4], tmp[8] = {0}; swara_ring r;
    CHECK(swara_ring_init(&r, buf, 4) == SWARA_OK);
    CHECK(swara_ring_read(&r, tmp, 1) == SWARA_ERR_EMPTY);
    CHECK(swara_ring_write(&r, tmp, 4) == SWARA_ERR_FULL);   /* usable size is 3 */
    CHECK(swara_ring_write(&r, tmp, 3) == SWARA_OK);
    CHECK(swara_ring_space(&r) == 0);
    CHECK(swara_ring_write(&r, tmp, 1) == SWARA_ERR_FULL);
    CHECK(swara_ring_available(&r) == 3);                    /* failed write changed nothing */
    CHECK(swara_ring_read(&r, tmp, 4) == SWARA_ERR_EMPTY);
    CHECK(swara_ring_available(&r) == 3);                    /* failed read changed nothing */
}
static void test_ring_wraparound(void) {
    float buf[8], in[5], out[5]; swara_ring r;
    CHECK(swara_ring_init(&r, buf, 8) == SWARA_OK);
    float v = 0.f; int ok = 1;
    for (int round = 0; round < 50; ++round) {           /* many wraps */
        for (int i = 0; i < 5; ++i) in[i] = v + (float)i;
        ok &= (swara_ring_write(&r, in, 5) == SWARA_OK);
        ok &= (swara_ring_read(&r, out, 5) == SWARA_OK);
        for (int i = 0; i < 5; ++i) ok &= (out[i] == in[i]);
        v += 5.f;
    }
    CHECK(ok);
}
static void test_ring_null_args(void) {
    float buf[4], x = 1.f; swara_ring r;
    CHECK(swara_ring_init(&r, buf, 4) == SWARA_OK);
    CHECK(swara_ring_write(NULL, &x, 1) == SWARA_ERR_ARG);
    CHECK(swara_ring_write(&r, NULL, 1) == SWARA_ERR_ARG);
    CHECK(swara_ring_read(NULL, &x, 1) == SWARA_ERR_ARG);
    CHECK(swara_ring_read(&r, NULL, 1) == SWARA_ERR_ARG);
    CHECK(swara_ring_write(&r, NULL, 0) == SWARA_OK);     /* zero-length is fine */
    CHECK(swara_ring_available(NULL) == 0);
    CHECK(swara_ring_space(NULL) == 0);
}

/* producer/consumer stress: order and completeness across two threads */
#define STRESS_TOTAL 2000000u
static float g_stress_storage[1024];
static swara_ring g_stress_ring;
static int g_stress_ok = 1;

static void *producer(void *arg) {
    (void)arg;
    unsigned sent = 0;
    float chunk[7];
    while (sent < STRESS_TOTAL) {
        size_t n = (STRESS_TOTAL - sent < 7u) ? (STRESS_TOTAL - sent) : 7u;
        for (size_t i = 0; i < n; ++i) chunk[i] = (float)(sent + (unsigned)i);
        if (swara_ring_write(&g_stress_ring, chunk, n) == SWARA_OK) sent += (unsigned)n;
    }
    return NULL;
}
static void *consumer(void *arg) {
    (void)arg;
    unsigned got = 0;
    float chunk[5];
    while (got < STRESS_TOTAL) {
        size_t n = (STRESS_TOTAL - got < 5u) ? (STRESS_TOTAL - got) : 5u;
        if (swara_ring_read(&g_stress_ring, chunk, n) == SWARA_OK) {
            for (size_t i = 0; i < n; ++i)
                if (chunk[i] != (float)(got + (unsigned)i)) g_stress_ok = 0;
            got += (unsigned)n;
        }
    }
    return NULL;
}
static void test_ring_spsc_stress(void) {
    pthread_t p, c;
    CHECK(swara_ring_init(&g_stress_ring, g_stress_storage, 1024) == SWARA_OK);
    CHECK(pthread_create(&p, NULL, producer, NULL) == 0);
    CHECK(pthread_create(&c, NULL, consumer, NULL) == 0);
    pthread_join(p, NULL);
    pthread_join(c, NULL);
    CHECK(g_stress_ok);
    CHECK(swara_ring_available(&g_stress_ring) == 0);
}

/* ---- limiter ---- */
static void test_limiter_init_validation(void) {
    swara_limiter l;
    CHECK(swara_limiter_init(NULL, 0.5f, 1, 10, 48000) == SWARA_ERR_ARG);
    CHECK(swara_limiter_init(&l, 0.0f, 1, 10, 48000) == SWARA_ERR_ARG);
    CHECK(swara_limiter_init(&l, 1.5f, 1, 10, 48000) == SWARA_ERR_ARG);
    CHECK(swara_limiter_init(&l, 0.5f, 0, 10, 48000) == SWARA_ERR_ARG);
    CHECK(swara_limiter_init(&l, 0.5f, 1, -1, 48000) == SWARA_ERR_ARG);
    CHECK(swara_limiter_init(&l, 0.5f, 1, 10, 0) == SWARA_ERR_ARG);
    CHECK(swara_limiter_init(&l, NAN, 1, 10, 48000) == SWARA_ERR_ARG);
    CHECK(swara_limiter_init(&l, 0.5f, 1, 10, 48000) == SWARA_OK);
}
static void test_limiter_never_exceeds_ceiling(void) {
    swara_limiter l; float in[4800], out[4800];
    CHECK(swara_limiter_init(&l, 0.5f, 1.0f, 50.0f, 48000.0f) == SWARA_OK);
    for (int i = 0; i < 4800; ++i) in[i] = 3.0f * sinf(2.0f * 3.14159265f * 440.0f * (float)i / 48000.0f);
    CHECK(swara_limiter_process(&l, in, out, 4800) == SWARA_OK);
    float peak = 0.f;
    for (int i = 0; i < 4800; ++i) if (fabsf(out[i]) > peak) peak = fabsf(out[i]);
    CHECK(peak <= 0.5f);
    CHECK(peak > 0.45f);   /* it limits, it does not mute */
}
static void test_limiter_passes_quiet_signal_unchanged(void) {
    swara_limiter l; float in[1000], out[1000]; int same = 1;
    CHECK(swara_limiter_init(&l, 0.9f, 1.0f, 50.0f, 48000.0f) == SWARA_OK);
    for (int i = 0; i < 1000; ++i) in[i] = 0.2f * sinf((float)i * 0.05f);
    CHECK(swara_limiter_process(&l, in, out, 1000) == SWARA_OK);
    for (int i = 0; i < 1000; ++i) if (out[i] != in[i]) same = 0;
    CHECK(same);
}
static void test_limiter_gain_recovers(void) {
    swara_limiter l; float loud[2000], quiet[48000], out[48000];
    CHECK(swara_limiter_init(&l, 0.5f, 1.0f, 20.0f, 48000.0f) == SWARA_OK);
    for (int i = 0; i < 2000; ++i) loud[i] = 4.0f;
    for (int i = 0; i < 48000; ++i) quiet[i] = 0.1f;
    CHECK(swara_limiter_process(&l, loud, out, 2000) == SWARA_OK);
    float reduced = l.gain;
    CHECK(reduced < 0.2f);
    CHECK(swara_limiter_process(&l, quiet, out, 48000) == SWARA_OK);
    CHECK(l.gain > 0.99f);                      /* recovered after one second */
    CHECK(fabsf(out[47999] - 0.1f) < 1e-3f);
}
static void test_limiter_handles_negative_and_inplace(void) {
    swara_limiter l; float buf[100];
    CHECK(swara_limiter_init(&l, 0.25f, 1.0f, 10.0f, 48000.0f) == SWARA_OK);
    for (int i = 0; i < 100; ++i) buf[i] = (i % 2) ? -2.0f : 2.0f;
    CHECK(swara_limiter_process(&l, buf, buf, 100) == SWARA_OK);   /* in-place */
    int ok = 1;
    for (int i = 0; i < 100; ++i) if (fabsf(buf[i]) > 0.25f) ok = 0;
    CHECK(ok);
    CHECK(buf[1] < 0.f && buf[0] > 0.f);        /* sign preserved */
}
static void test_limiter_null_and_zero_length(void) {
    swara_limiter l; float x = 0.f;
    CHECK(swara_limiter_init(&l, 0.5f, 1.0f, 10.0f, 48000.0f) == SWARA_OK);
    CHECK(swara_limiter_process(NULL, &x, &x, 1) == SWARA_ERR_ARG);
    CHECK(swara_limiter_process(&l, NULL, &x, 1) == SWARA_ERR_ARG);
    CHECK(swara_limiter_process(&l, &x, NULL, 1) == SWARA_ERR_ARG);
    CHECK(swara_limiter_process(&l, NULL, NULL, 0) == SWARA_OK);
    swara_limiter_reset(NULL);                  /* must not crash */
    l.gain = 0.3f; swara_limiter_reset(&l);
    CHECK(l.gain == 1.0f);
}
static void test_limiter_block_size_independence(void) {
    /* processing in one block or in many small blocks must give identical output */
    swara_limiter a, b; float in[3000], out_a[3000], out_b[3000];
    CHECK(swara_limiter_init(&a, 0.4f, 1.0f, 30.0f, 44100.0f) == SWARA_OK);
    CHECK(swara_limiter_init(&b, 0.4f, 1.0f, 30.0f, 44100.0f) == SWARA_OK);
    for (int i = 0; i < 3000; ++i) in[i] = 2.0f * sinf((float)i * 0.03f);
    CHECK(swara_limiter_process(&a, in, out_a, 3000) == SWARA_OK);
    for (int i = 0; i < 3000; i += 64) {
        size_t n = (3000 - i < 64) ? (size_t)(3000 - i) : 64u;
        CHECK(swara_limiter_process(&b, in + i, out_b + i, n) == SWARA_OK);
    }
    CHECK(memcmp(out_a, out_b, sizeof out_a) == 0);
}

static void test_limiter_nan_and_infinity(void) {
    /* Regression for a bug the fuzzer found: NaN input used to produce NaN output. */
    swara_limiter l; float in[6], out[6];
    CHECK(swara_limiter_init(&l, 0.5f, 1.0f, 10.0f, 48000.0f) == SWARA_OK);
    in[0] = NAN; in[1] = INFINITY; in[2] = -INFINITY; in[3] = 0.2f; in[4] = NAN; in[5] = -0.2f;
    CHECK(swara_limiter_process(&l, in, out, 6) == SWARA_OK);
    int ok = 1;
    for (int i = 0; i < 6; ++i) ok &= (isfinite(out[i]) && fabsf(out[i]) <= 0.5f);
    CHECK(ok);
    CHECK(out[0] == 0.0f);                       /* NaN becomes silence */
    CHECK(out[1] > 0.0f && out[2] < 0.0f);       /* infinities keep their sign */
    CHECK(l.gain > 0.0f && l.gain <= 1.0f);
    /* a long run of infinities must not drive the gain to zero or NaN */
    float big[2000], res[2000];
    for (int i = 0; i < 2000; ++i) big[i] = INFINITY;
    CHECK(swara_limiter_process(&l, big, res, 2000) == SWARA_OK);
    CHECK(l.gain > 0.0f && isfinite(res[1999]));
}

int main(void) {
    test_ring_init_validation();
    test_ring_size_helpers();
    test_ring_basic_roundtrip();
    test_ring_full_and_empty();
    test_ring_wraparound();
    test_ring_null_args();
    test_ring_spsc_stress();
    test_limiter_init_validation();
    test_limiter_never_exceeds_ceiling();
    test_limiter_passes_quiet_signal_unchanged();
    test_limiter_gain_recovers();
    test_limiter_handles_negative_and_inplace();
    test_limiter_null_and_zero_length();
    test_limiter_block_size_independence();
    test_limiter_nan_and_infinity();
    printf("%d checks, %d failed\n", g_run, g_failed);
    return g_failed == 0 ? 0 : 1;
}
