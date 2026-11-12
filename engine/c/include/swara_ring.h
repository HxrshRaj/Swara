/* swara_ring: single-producer / single-consumer lock-free ring buffer of floats (C11 atomics).
 * C only: this header is not meant to be included from C++ (it uses C11 _Atomic).
 * No malloc: the caller provides the storage. Not certified code. */
#ifndef SWARA_RING_H
#define SWARA_RING_H
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include "swara_status.h"

typedef struct {
    float *data;               /* caller-owned storage */
    size_t capacity;           /* power of two */
    size_t mask;               /* capacity - 1 */
    _Atomic size_t head;       /* written only by the producer */
    _Atomic size_t tail;       /* written only by the consumer */
} swara_ring;

/* capacity must be a power of two and >= 2. Usable size is capacity - 1. */
swara_status swara_ring_init(swara_ring *r, float *storage, size_t capacity);
size_t swara_ring_available(const swara_ring *r);  /* readable samples */
size_t swara_ring_space(const swara_ring *r);      /* writable samples */
/* All-or-nothing: either all n samples are transferred or none (and an error is returned). */
swara_status swara_ring_write(swara_ring *r, const float *src, size_t n);
swara_status swara_ring_read(swara_ring *r, float *dst, size_t n);
#endif
