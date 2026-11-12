#include "swara_ring.h"

static int is_pow2(size_t v) { return v >= 2U && (v & (v - 1U)) == 0U; }

swara_status swara_ring_init(swara_ring *r, float *storage, size_t capacity) {
    if (r == NULL || storage == NULL || !is_pow2(capacity)) return SWARA_ERR_ARG;
    r->data = storage;
    r->capacity = capacity;
    r->mask = capacity - 1U;
    atomic_init(&r->head, 0U);
    atomic_init(&r->tail, 0U);
    return SWARA_OK;
}

size_t swara_ring_available(const swara_ring *r) {
    if (r == NULL) return 0U;
    size_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    size_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    return h - t; /* unsigned wrap-around is well defined */
}

size_t swara_ring_space(const swara_ring *r) {
    if (r == NULL) return 0U;
    return (r->capacity - 1U) - swara_ring_available(r);
}

swara_status swara_ring_write(swara_ring *r, const float *src, size_t n) {
    if (r == NULL || (src == NULL && n > 0U)) return SWARA_ERR_ARG;
    size_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    size_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    if (n > (r->capacity - 1U) - (h - t)) return SWARA_ERR_FULL;
    for (size_t i = 0; i < n; ++i) r->data[(h + i) & r->mask] = src[i];
    atomic_store_explicit(&r->head, h + n, memory_order_release);
    return SWARA_OK;
}

swara_status swara_ring_read(swara_ring *r, float *dst, size_t n) {
    if (r == NULL || (dst == NULL && n > 0U)) return SWARA_ERR_ARG;
    size_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
    size_t h = atomic_load_explicit(&r->head, memory_order_acquire);
    if (n > h - t) return SWARA_ERR_EMPTY;
    for (size_t i = 0; i < n; ++i) dst[i] = r->data[(t + i) & r->mask];
    atomic_store_explicit(&r->tail, t + n, memory_order_release);
    return SWARA_OK;
}
