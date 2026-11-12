#include "limiter.h"
#include <stdexcept>

namespace swara {

LimiterEffect::LimiterEffect(float threshold, double attackMs, double releaseMs, double sampleRateHz) {
    if (swara_limiter_init(&lim_, threshold, (float)attackMs, (float)releaseMs, (float)sampleRateHz) != SWARA_OK)
        throw std::runtime_error("Invalid limiter parameters");
}

void LimiterEffect::process(const float* in, float* out, int numSamples) {
    if (numSamples <= 0) return;
    swara_limiter_process(&lim_, in, out, (size_t)numSamples);
}

void LimiterEffect::reset() { swara_limiter_reset(&lim_); }

} // namespace swara
