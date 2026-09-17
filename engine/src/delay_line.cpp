#include "delay_line.h"
#include <algorithm>
#include <stdexcept>

namespace swara {

DelayEffect::DelayEffect(double delayMs, double sampleRateHz, float feedback, float mix)
    : delaySamples_(std::max<size_t>(1, (size_t)(delayMs * sampleRateHz / 1000.0))),
      feedback_(std::max(0.0f, std::min(feedback, 0.98f))),
      mix_(std::max(0.0f, std::min(mix, 1.0f))),
      buffer_(delaySamples_ + 1) {
    if (delayMs <= 0.0) throw std::runtime_error("Delay time must be > 0");
}

void DelayEffect::process(const float* in, float* out, int numSamples) {
    for (int i = 0; i < numSamples; ++i) {
        float delayed = buffer_.read(delaySamples_);
        float toStore = in[i] + feedback_ * delayed;
        buffer_.write(toStore);
        out[i] = (1.0f - mix_) * in[i] + mix_ * delayed;
    }
}

void DelayEffect::reset() {
    buffer_.clear();
}

} // namespace swara
