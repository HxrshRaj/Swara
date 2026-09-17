#include "biquad.h"
#include <cmath>
#include <stdexcept>

namespace swara {

static constexpr double PI = 3.14159265358979323846;

ButterworthFilter::ButterworthFilter(FilterType type, double cutoffHz, double sampleRateHz, int order)
    : type_(type), cutoffHz_(cutoffHz), sampleRateHz_(sampleRateHz), order_(order) {
    if (order_ < 1) throw std::runtime_error("Filter order must be >= 1");
    if (cutoffHz_ <= 0 || cutoffHz_ >= sampleRateHz_ / 2.0)
        throw std::runtime_error("Cutoff must be within (0, Nyquist)");
    name_ = (type_ == FilterType::LowPass ? std::string("lowpass") : std::string("highpass")) +
            "_butterworth_o" + std::to_string(order_);
    designStages();
}

void ButterworthFilter::designStages() {
    stages_.clear();
    bool hasOddStage = (order_ % 2) == 1;

    double omega = 2.0 * PI * cutoffHz_ / sampleRateHz_;
    double sinw = std::sin(omega);
    double cosw = std::cos(omega);

    // Odd order: one real pole, realized as a first-order section via the
    // bilinear transform of the analog RC prototype (unity gain at DC for
    // lowpass, unity gain at Nyquist for highpass).
    if (hasOddStage) {
        double t = std::tan(omega / 2.0);
        double a0 = 1.0 + t;
        BiquadCoeffs c;
        if (type_ == FilterType::LowPass) {
            c.b0 = t / a0; c.b1 = t / a0; c.b2 = 0.0;
        } else {
            c.b0 = 1.0 / a0; c.b1 = -1.0 / a0; c.b2 = 0.0;
        }
        c.a1 = (t - 1.0) / a0; c.a2 = 0.0;
        BiquadStage stage;
        stage.setCoeffs(c);
        stages_.push_back(stage);
    }

    // Remaining conjugate pole pairs -> one RBJ-cookbook biquad each.
    // Butterworth poles sit at equal angular spacing on the unit circle;
    // for pair k (k = 1..floor(N/2), odd real pole excluded) that gives
    // Qk = 1 / (2*sin((2k-1)*pi / (2N))), which reproduces the standard
    // Butterworth Q tables (e.g. N=4 -> 0.5412, 1.3066; N=3 -> 1.0).
    int pairCount = order_ / 2;
    for (int k = 1; k <= pairCount; ++k) {
        double Q = 1.0 / (2.0 * std::sin(PI * (2.0 * k - 1.0) / (2.0 * order_)));
        double alpha = sinw / (2.0 * Q);
        BiquadCoeffs c;
        if (type_ == FilterType::LowPass) {
            double b0 = (1.0 - cosw) / 2.0;
            double b1 = 1.0 - cosw;
            double b2 = (1.0 - cosw) / 2.0;
            double a0 = 1.0 + alpha;
            double a1 = -2.0 * cosw;
            double a2 = 1.0 - alpha;
            c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0;
            c.a1 = a1 / a0; c.a2 = a2 / a0;
        } else {
            double b0 = (1.0 + cosw) / 2.0;
            double b1 = -(1.0 + cosw);
            double b2 = (1.0 + cosw) / 2.0;
            double a0 = 1.0 + alpha;
            double a1 = -2.0 * cosw;
            double a2 = 1.0 - alpha;
            c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0;
            c.a1 = a1 / a0; c.a2 = a2 / a0;
        }
        BiquadStage stage;
        stage.setCoeffs(c);
        stages_.push_back(stage);
    }
}

void ButterworthFilter::process(const float* in, float* out, int numSamples) {
    for (int i = 0; i < numSamples; ++i) {
        float s = in[i];
        for (auto& stage : stages_) s = stage.processSample(s);
        out[i] = s;
    }
}

void ButterworthFilter::reset() {
    for (auto& stage : stages_) stage.reset();
}

} // namespace swara
