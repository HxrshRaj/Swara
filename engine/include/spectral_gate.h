#pragma once
#include <vector>
#include <deque>
#include "effect.h"
#include "../third_party/kissfft/kiss_fftr.h"

namespace swara {

// FFT-based spectral noise gate. Performs a genuine short-time Fourier
// transform (STFT) via KissFFT: analysis with a 50%-overlap Hann window,
// per-bin adaptive noise-floor tracking, a soft-knee gain applied in the
// frequency domain, then inverse FFT with overlap-add reconstruction.
//
// This is a real block-processing pipeline with real algorithmic latency:
// output for sample n only becomes available once a full analysis frame
// (frameSize samples) covering n has been accumulated, so the first
// (frameSize - hopSize) output samples of a stream are necessarily zero.
// That startup delay is a genuine property of STFT-based real-time
// processing, not an implementation shortcut.
class SpectralNoiseGate : public Effect {
public:
    SpectralNoiseGate(int frameSize, double sampleRateHz,
                       double thresholdAboveFloorDb = 10.0,
                       double floorGainDb = -26.0,
                       double kneeDb = 6.0);
    ~SpectralNoiseGate() override;

    void process(const float* in, float* out, int numSamples) override;
    void reset() override;
    const char* name() const override { return "spectral_noise_gate"; }

    int algorithmicLatencySamples() const { return frameSize_ - hopSize_; }

private:
    void processFrame();

    int frameSize_;
    int hopSize_;
    int numBins_;
    double sampleRateHz_;
    double thresholdAboveFloorDb_;
    double floorGainLinear_;
    double kneeDb_;

    std::vector<float> window_;
    std::vector<float> history_;      // last frameSize_ input samples
    std::vector<float> olaBuffer_;    // overlap-add accumulator, frameSize_ long
    std::deque<float> pending_;       // raw input awaiting a full hop
    std::deque<float> outputQueue_;   // finalized samples ready to emit
    std::vector<float> noiseFloor_;   // per-bin adaptive noise floor (magnitude)
    std::vector<float> smoothedMag_;  // per-bin smoothed magnitude envelope (reduces single-frame variance)
    bool floorInitialized_ = false;

    kiss_fftr_cfg fwdCfg_;
    kiss_fftr_cfg invCfg_;
    std::vector<kiss_fft_cpx> spectrum_;
    std::vector<float> timeBuf_;
};

} // namespace swara
