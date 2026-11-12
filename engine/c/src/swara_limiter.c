#include "swara_limiter.h"
#include <float.h>
#include <math.h>

static float coeff_from_ms(float ms, float sr) {
    /* One-pole smoothing coefficient for a time constant of `ms` milliseconds.
       Computed in double: with float arithmetic a tiny time constant times a huge sample rate
       could give 0 * inf = NaN (found by libFuzzer). A time constant of zero means "instant". */
    double samples = 0.001 * (double)ms * (double)sr;
    if (!(samples > 0.0)) return 0.0f;
    return (float)exp(-1.0 / samples);
}

swara_status swara_limiter_init(swara_limiter *l, float threshold, float attack_ms,
                                float release_ms, float sample_rate_hz) {
    if (l == NULL) return SWARA_ERR_ARG;
    if (!(threshold > 0.0f && threshold <= 1.0f)) return SWARA_ERR_ARG;
    if (!(attack_ms > 0.0f && release_ms > 0.0f && sample_rate_hz > 0.0f)) return SWARA_ERR_ARG;
    /* reject infinities too: they are positive, so the checks above let them through */
    if (!(isfinite(threshold) && isfinite(attack_ms) && isfinite(release_ms) && isfinite(sample_rate_hz)))
        return SWARA_ERR_ARG;
    l->threshold = threshold;
    l->attack_coeff = coeff_from_ms(attack_ms, sample_rate_hz);
    l->release_coeff = coeff_from_ms(release_ms, sample_rate_hz);
    l->gain = 1.0f;
    return SWARA_OK;
}

void swara_limiter_reset(swara_limiter *l) {
    if (l != NULL) l->gain = 1.0f;
}

swara_status swara_limiter_process(swara_limiter *l, const float *in, float *out, size_t n) {
    if (l == NULL || (n > 0U && (in == NULL || out == NULL))) return SWARA_ERR_ARG;
    for (size_t i = 0; i < n; ++i) {
        float x = in[i];
        /* found by fuzzing: NaN would propagate to the output, and infinity can underflow the
           gain to zero and produce NaN. NaN becomes silence; infinities become the largest finite value. */
        if (x != x) x = 0.0f;
        else if (x > FLT_MAX) x = FLT_MAX;
        else if (x < -FLT_MAX) x = -FLT_MAX;
        float peak = fabsf(x);
        /* gain needed so this sample sits at or below the threshold */
        float target = (peak > l->threshold) ? (l->threshold / peak) : 1.0f;
        /* move gain toward target: fast when reducing, slow when recovering */
        float c = (target < l->gain) ? l->attack_coeff : l->release_coeff;
        l->gain = c * l->gain + (1.0f - c) * target;
        float y = x * l->gain;
        /* hard guarantee: smoothing can lag, so clamp the final sample to the ceiling */
        if (y > l->threshold) y = l->threshold;
        if (y < -l->threshold) y = -l->threshold;
        out[i] = y;
    }
    return SWARA_OK;
}
