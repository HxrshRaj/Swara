/* swara_limiter: peak limiter with attack/release gain smoothing, plain C11.
 * Safe to include from C++. No malloc, no recursion, every call validates its arguments.
 * Not certified code; no safety-standard compliance is claimed. */
#ifndef SWARA_LIMITER_H
#define SWARA_LIMITER_H
#include <stddef.h>
#include "swara_status.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct swara_limiter {
    float threshold;       /* linear peak ceiling, (0, 1] */
    float attack_coeff;    /* per-sample smoothing when gain must drop */
    float release_coeff;   /* per-sample smoothing when gain recovers */
    float gain;            /* current gain, (0, 1] */
} swara_limiter;

swara_status swara_limiter_init(swara_limiter *l, float threshold, float attack_ms,
                                float release_ms, float sample_rate_hz);
void swara_limiter_reset(swara_limiter *l);
/* in and out may alias. Output magnitude never exceeds the threshold. */
swara_status swara_limiter_process(swara_limiter *l, const float *in, float *out, size_t n);

#ifdef __cplusplus
}
#endif
#endif
