#pragma once
#include <vector>
#include "effect.h"

namespace swara {

// Fixed-capacity circular (ring) buffer used as the delay line's storage.
// write() advances the head and overwrites the oldest sample; read(delaySamples)
// reads back `delaySamples` behind the current head without disturbing state.
class CircularBuffer {
public:
    explicit CircularBuffer(size_t capacity) : buffer_(capacity, 0.0f), capacity_(capacity) {}

    void write(float sample) {
        buffer_[head_] = sample;
        head_ = (head_ + 1) % capacity_;
    }

    // delaySamples must be in [1, capacity].
    float read(size_t delaySamples) const {
        size_t idx = (head_ + capacity_ - delaySamples) % capacity_;
        return buffer_[idx];
    }

    size_t capacity() const { return capacity_; }

    void clear() { std::fill(buffer_.begin(), buffer_.end(), 0.0f); head_ = 0; }

private:
    std::vector<float> buffer_;
    size_t capacity_;
    size_t head_ = 0;
};

// Classic feedback delay/echo: y[n] = dry*x[n] + wet*delayed, and the value
// fed back into the delay line is x[n] + feedback*delayed, so echoes repeat
// and decay geometrically by `feedback` each pass through the line.
class DelayEffect : public Effect {
public:
    DelayEffect(double delayMs, double sampleRateHz, float feedback, float mix);

    void process(const float* in, float* out, int numSamples) override;
    void reset() override;
    const char* name() const override { return "delay_echo"; }

private:
    size_t delaySamples_;
    float feedback_;
    float mix_; // 0 = fully dry, 1 = fully wet
    CircularBuffer buffer_;
};

} // namespace swara
