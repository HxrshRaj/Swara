#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace swara {

// Minimal PCM WAV reader/writer. Supports 16-bit and 24-bit integer PCM,
// mono or multi-channel, interleaved. Samples are exposed as planar
// float buffers normalized to [-1.0, 1.0].
struct WavAudio {
    uint32_t sampleRate = 0;
    uint16_t numChannels = 0;
    uint16_t bitsPerSample = 16;
    // channels[c][n] = sample for channel c, frame n
    std::vector<std::vector<float>> channels;

    size_t numFrames() const { return channels.empty() ? 0 : channels[0].size(); }
};

// Throws std::runtime_error on malformed files.
WavAudio readWav(const std::string& path);

// Always writes 16-bit PCM WAV.
void writeWav(const std::string& path, const WavAudio& audio);

} // namespace swara
