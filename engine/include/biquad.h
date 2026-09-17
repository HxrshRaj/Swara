#pragma once
#include <vector>
#include "effect.h"

namespace swara {

// A single second-order IIR section (Direct Form I), coefficients per the
// Audio EQ Cookbook (Robert Bristow-Johnson). a0 is normalized to 1.
struct BiquadCoeffs {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
};

class BiquadStage {
public:
    void setCoeffs(const BiquadCoeffs& c) { c_ = c; }
    inline float processSample(float x) {
        double y = c_.b0 * x + c_.b1 * x1_ + c_.b2 * x2_ - c_.a1 * y1_ - c_.a2 * y2_;
        x2_ = x1_; x1_ = x;
        y2_ = y1_; y1_ = y;
        return (float)y;
    }
    void reset() { x1_ = x2_ = y1_ = y2_ = 0.0; }
private:
    BiquadCoeffs c_;
    double x1_ = 0, x2_ = 0, y1_ = 0, y2_ = 0;
};

enum class FilterType { LowPass, HighPass };

// N-th order Butterworth filter built from a cascade of ceil(N/2) biquad
// sections. Each section's Q is chosen from the classic Butterworth pole
// layout Qk = 1 / (2*cos((2k-1)*pi / (2*N))), which is what makes the
// cascade a maximally-flat Butterworth response rather than an arbitrary
// stack of resonant filters. RBJ cookbook biquad coefficients are derived
// per-section from that Q.
class ButterworthFilter : public Effect {
public:
    ButterworthFilter(FilterType type, double cutoffHz, double sampleRateHz, int order);

    void process(const float* in, float* out, int numSamples) override;
    void reset() override;
    const char* name() const override { return name_.c_str(); }

private:
    void designStages();

    FilterType type_;
    double cutoffHz_;
    double sampleRateHz_;
    int order_;
    std::vector<BiquadStage> stages_;
    std::string name_;
};

} // namespace swara
