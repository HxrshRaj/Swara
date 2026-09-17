#pragma once
#include <string>

namespace swara {

// Streaming, block-based audio effect. Implementations must process
// fixed-size buffers with internal state carried across calls, exactly as
// a real-time processor would (no whole-file lookahead).
class Effect {
public:
    virtual ~Effect() = default;
    virtual void process(const float* in, float* out, int numSamples) = 0;
    virtual void reset() = 0;
    virtual const char* name() const = 0;
};

} // namespace swara
