/* Measures SPSC ring buffer throughput across two threads. Prints what it measured. */
#include "swara_ring.h"
#include <pthread.h>
#include <stdio.h>
#include <time.h>

#define TOTAL 50000000u
static float storage[4096];
static swara_ring ring;

static void *producer(void *a) {
    (void)a; float chunk[64] = {0}; unsigned sent = 0;
    while (sent < TOTAL) if (swara_ring_write(&ring, chunk, 64) == SWARA_OK) sent += 64u;
    return NULL;
}
static void *consumer(void *a) {
    (void)a; float chunk[64]; unsigned got = 0;
    while (got < TOTAL) if (swara_ring_read(&ring, chunk, 64) == SWARA_OK) got += 64u;
    return NULL;
}
int main(void) {
    swara_ring_init(&ring, storage, 4096);
    struct timespec t0, t1; pthread_t p, c;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    pthread_create(&p, NULL, producer, NULL); pthread_create(&c, NULL, consumer, NULL);
    pthread_join(p, NULL); pthread_join(c, NULL);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double s = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("%u samples in %.3f s = %.1f Msamples/s (%.1f MB/s of float32)\n",
           TOTAL, s, TOTAL / s / 1e6, TOTAL * 4.0 / s / 1e6);
    return 0;
}
