// Tests for the Objective-C wrappers. Plain tool with a tiny check macro (XCTest is not available on Linux).
#import <Foundation/Foundation.h>
#import <pthread.h>
#import "SWRingBuffer.h"
#import "SWLimiter.h"
#include <math.h>

static int g_run = 0, g_failed = 0;
#define CHECK(cond) do { g_run++; if (!(cond)) { g_failed++; printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)

static NSData *Floats(const float *v, NSUInteger n) { return [NSData dataWithBytes:v length:n * sizeof(float)]; }

static void testRingInitValidation(void) {
    SWRingBuffer *bad1 = [[SWRingBuffer alloc] initWithCapacity:0];
    SWRingBuffer *bad2 = [[SWRingBuffer alloc] initWithCapacity:6];
    SWRingBuffer *bad3 = [[SWRingBuffer alloc] initWithCapacity:1];
    SWRingBuffer *ok = [[SWRingBuffer alloc] initWithCapacity:8];
    CHECK(bad1 == nil); CHECK(bad2 == nil); CHECK(bad3 == nil);
    CHECK(ok != nil);
    CHECK([ok availableSamples] == 0);
    CHECK([ok freeSamples] == 7);
    [ok release];
}

static void testRingRoundTrip(void) {
    SWRingBuffer *r = [[SWRingBuffer alloc] initWithCapacity:8];
    const float in[4] = {1.f, 2.f, 3.f, 4.f};
    NSError *err = nil;
    CHECK([r writeSamples:Floats(in, 4) error:&err]);
    CHECK(err == nil);
    CHECK([r availableSamples] == 4);
    NSData *out = [r readSamples:4 error:&err];
    CHECK(out != nil);
    CHECK([out length] == 4 * sizeof(float));
    CHECK(memcmp([out bytes], in, sizeof in) == 0);
    CHECK([r availableSamples] == 0);
    [r release];
}

static void testRingFullAndEmptyErrors(void) {
    SWRingBuffer *r = [[SWRingBuffer alloc] initWithCapacity:4];
    float z[4] = {0, 0, 0, 0};
    NSError *err = nil;
    CHECK([r readSamples:1 error:&err] == nil);
    CHECK(err != nil && [err code] == SWErrorEmpty && [[err domain] isEqualToString:SWErrorDomain]);
    err = nil;
    CHECK(![r writeSamples:Floats(z, 4) error:&err]);          // usable size is 3
    CHECK(err != nil && [err code] == SWErrorFull);
    CHECK([r writeSamples:Floats(z, 3) error:NULL]);            // NULL error pointer is allowed
    CHECK([r freeSamples] == 0);
    CHECK([r availableSamples] == 3);                           // the failed write changed nothing
    [r release];
}

static void testRingRejectsBadData(void) {
    SWRingBuffer *r = [[SWRingBuffer alloc] initWithCapacity:8];
    NSError *err = nil;
    uint8_t odd[5] = {1, 2, 3, 4, 5};
    CHECK(![r writeSamples:[NSData dataWithBytes:odd length:5] error:&err]);
    CHECK(err != nil && [err code] == SWErrorInvalidArgument);
    CHECK(![r writeSamples:nil error:NULL]);
    CHECK([r availableSamples] == 0);
    [r release];
}

static void testRingWraparound(void) {
    SWRingBuffer *r = [[SWRingBuffer alloc] initWithCapacity:8];
    BOOL ok = YES;
    float v = 0.f;
    for (int round = 0; round < 40; ++round) {
        float in[5];
        for (int i = 0; i < 5; ++i) in[i] = v + (float)i;
        ok = ok && [r writeSamples:Floats(in, 5) error:NULL];
        NSData *out = [r readSamples:5 error:NULL];
        ok = ok && out != nil && memcmp([out bytes], in, sizeof in) == 0;
        v += 5.f;
    }
    CHECK(ok);
    [r release];
}

// two-thread order check through the Objective-C API
#define TOTAL 200000u
static SWRingBuffer *g_ring;
static int g_ok = 1;
static void *producer(void *arg) {
    (void)arg;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    unsigned sent = 0;
    while (sent < TOTAL) {
        float chunk[7];
        unsigned n = (TOTAL - sent < 7u) ? (TOTAL - sent) : 7u;
        for (unsigned i = 0; i < n; ++i) chunk[i] = (float)(sent + i);
        if ([g_ring writeSamples:Floats(chunk, n) error:NULL]) sent += n;
    }
    [pool release];
    return NULL;
}
static void *consumer(void *arg) {
    (void)arg;
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    unsigned got = 0;
    while (got < TOTAL) {
        unsigned n = (TOTAL - got < 5u) ? (TOTAL - got) : 5u;
        NSData *d = [g_ring readSamples:n error:NULL];
        if (d != nil) {
            const float *f = (const float *)[d bytes];
            for (unsigned i = 0; i < n; ++i) if (f[i] != (float)(got + i)) g_ok = 0;
            got += n;
        }
    }
    [pool release];
    return NULL;
}
static void testRingTwoThreads(void) {
    g_ring = [[SWRingBuffer alloc] initWithCapacity:1024];
    pthread_t p, c;
    CHECK(pthread_create(&p, NULL, producer, NULL) == 0);
    CHECK(pthread_create(&c, NULL, consumer, NULL) == 0);
    pthread_join(p, NULL); pthread_join(c, NULL);
    CHECK(g_ok);
    CHECK([g_ring availableSamples] == 0);
    [g_ring release];
}

static void testLimiterInitValidation(void) {
    CHECK([[SWLimiter alloc] initWithThreshold:0.f attackMs:1 releaseMs:10 sampleRate:48000] == nil);
    CHECK([[SWLimiter alloc] initWithThreshold:1.5f attackMs:1 releaseMs:10 sampleRate:48000] == nil);
    CHECK([[SWLimiter alloc] initWithThreshold:0.5f attackMs:0 releaseMs:10 sampleRate:48000] == nil);
    CHECK([[SWLimiter alloc] initWithThreshold:0.5f attackMs:1 releaseMs:10 sampleRate:INFINITY] == nil);
    SWLimiter *ok = [[SWLimiter alloc] initWithThreshold:0.5f attackMs:1 releaseMs:10 sampleRate:48000];
    CHECK(ok != nil);
    CHECK([ok threshold] == 0.5f);
    CHECK([ok currentGain] == 1.0f);
    [ok release];
}

static void testLimiterCeilingAndPassThrough(void) {
    SWLimiter *l = [[SWLimiter alloc] initWithThreshold:0.5f attackMs:1 releaseMs:50 sampleRate:48000];
    float loud[4800];
    for (int i = 0; i < 4800; ++i) loud[i] = 3.0f * sinf(2.0f * 3.14159265f * 440.0f * (float)i / 48000.0f);
    NSData *out = [l processSamples:Floats(loud, 4800)];
    CHECK(out != nil && [out length] == 4800 * sizeof(float));
    const float *o = (const float *)[out bytes];
    float peak = 0.f;
    for (int i = 0; i < 4800; ++i) if (fabsf(o[i]) > peak) peak = fabsf(o[i]);
    CHECK(peak <= 0.5f && peak > 0.45f);
    CHECK([l currentGain] < 1.0f);
    [l reset];
    CHECK([l currentGain] == 1.0f);
    float quiet[100];
    for (int i = 0; i < 100; ++i) quiet[i] = 0.1f * sinf((float)i * 0.1f);
    NSData *q = [l processSamples:Floats(quiet, 100)];
    CHECK(memcmp([q bytes], quiet, sizeof quiet) == 0);
    [l release];
}

static void testLimiterBadAndSpecialInput(void) {
    SWLimiter *l = [[SWLimiter alloc] initWithThreshold:0.5f attackMs:1 releaseMs:10 sampleRate:48000];
    CHECK([l processSamples:nil] == nil);
    uint8_t odd[3] = {1, 2, 3};
    CHECK([l processSamples:[NSData dataWithBytes:odd length:3]] == nil);
    CHECK([[l processSamples:[NSData data]] length] == 0);
    float weird[3] = {NAN, INFINITY, -INFINITY};
    NSData *out = [l processSamples:Floats(weird, 3)];
    const float *o = (const float *)[out bytes];
    CHECK(out != nil && isfinite(o[0]) && isfinite(o[1]) && isfinite(o[2]));
    CHECK(fabsf(o[1]) <= 0.5f && fabsf(o[2]) <= 0.5f);
    [l release];
}

int main(void) {
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    testRingInitValidation();
    testRingRoundTrip();
    testRingFullAndEmptyErrors();
    testRingRejectsBadData();
    testRingWraparound();
    testRingTwoThreads();
    testLimiterInitValidation();
    testLimiterCeilingAndPassThrough();
    testLimiterBadAndSpecialInput();
    printf("%d checks, %d failed\n", g_run, g_failed);
    [pool release];
    return g_failed == 0 ? 0 : 1;
}
