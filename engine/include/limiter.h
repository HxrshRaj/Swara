#pragma once
#include "effect.h"
#include "swara_limiter.h"

namespace swara {

// Peak limiter: C++ Effect wrapper around the C11 swara_limiter.
class LimiterEffect : public Effect {
public:
    LimiterEffect(float threshold, double attackMs, double releaseMs, double sampleRateHz);
    void process(const float* in, float* out, int numSamples) override;
    void reset() override;
    const char* name() const override { return "peak_limiter"; }
private:
    swara_limiter lim_;
};

} // namespace swara
