/* swara_ring: single-producer / single-consumer lock-free ring buffer of floats (C11 atomics).
 * Usable from C and C++ (C++ sees an opaque type, see below).
 * No malloc: the caller provides the storage. Not certified code. */
#ifndef SWARA_RING_H
#define SWARA_RING_H
#if !defined(__cplusplus) && !defined(__OBJC__)
#include <stdatomic.h>
#endif
#include <stddef.h>
#include <stdint.h>
#include "swara_status.h"

#if defined(__cplusplus) || defined(__OBJC__)
/* C++ and Objective-C see the ring as an opaque type (GCC's Objective-C front end does not implement _Atomic): allocate swara_ring_sizeof() bytes aligned to
 * swara_ring_alignof() and pass the pointer to swara_ring_init(). */
#ifdef __cplusplus
extern "C" {
#endif
typedef struct swara_ring swara_ring;
#else
struct swara_ring {
    float *data;               /* caller-owned storage */
    size_t capacity;           /* power of two */
    size_t mask;               /* capacity - 1 */
    _Atomic size_t head;       /* written only by the producer */
    _Atomic size_t tail;       /* written only by the consumer */
};
typedef struct swara_ring swara_ring;
#endif

size_t swara_ring_sizeof(void);
size_t swara_ring_alignof(void);

/* capacity must be a power of two and >= 2. Usable size is capacity - 1. */
swara_status swara_ring_init(swara_ring *r, float *storage, size_t capacity);
size_t swara_ring_available(const swara_ring *r);  /* readable samples */
size_t swara_ring_space(const swara_ring *r);      /* writable samples */
/* All-or-nothing: either all n samples are transferred or none (and an error is returned). */
swara_status swara_ring_write(swara_ring *r, const float *src, size_t n);
swara_status swara_ring_read(swara_ring *r, float *dst, size_t n);

#ifdef __cplusplus
}
#endif
#endif
