#include "spectral_gate.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

namespace swara {

static constexpr double PI = 3.14159265358979323846;

SpectralNoiseGate::SpectralNoiseGate(int frameSize, double sampleRateHz,
                                      double thresholdAboveFloorDb,
                                      double floorGainDb,
                                      double kneeDb)
    : frameSize_(frameSize), hopSize_(frameSize / 2), numBins_(frameSize / 2 + 1),
      sampleRateHz_(sampleRateHz), thresholdAboveFloorDb_(thresholdAboveFloorDb),
      floorGainLinear_(std::pow(10.0, floorGainDb / 20.0)), kneeDb_(kneeDb),
      window_(frameSize, 0.0f), history_(frameSize, 0.0f), olaBuffer_(frameSize, 0.0f),
      noiseFloor_(numBins_, 1e-6f), smoothedMag_(numBins_, 1e-6f) {
    if (frameSize_ < 8 || (frameSize_ % 2) != 0)
        throw std::runtime_error("Spectral gate frame size must be even and >= 8");

    // Periodic (DFT-even) Hann window: with 50% hop, two shifted copies sum
    // to exactly 1 everywhere, so analysis-only windowing plus plain
    // overlap-add reconstructs an unmodified spectrum exactly (no synthesis
    // window or extra normalization needed).
    for (int n = 0; n < frameSize_; ++n)
        window_[n] = 0.5f * (1.0f - std::cos(2.0f * (float)PI * n / frameSize_));

    fwdCfg_ = kiss_fftr_alloc(frameSize_, 0, nullptr, nullptr);
    invCfg_ = kiss_fftr_alloc(frameSize_, 1, nullptr, nullptr);
    spectrum_.resize(numBins_);
    timeBuf_.resize(frameSize_);
}

SpectralNoiseGate::~SpectralNoiseGate() {
    kiss_fft_free(fwdCfg_);
    kiss_fft_free(invCfg_);
}

void SpectralNoiseGate::processFrame() {
    // Slide the analysis window forward by one hop.
    std::rotate(history_.begin(), history_.begin() + hopSize_, history_.end());
    for (int i = 0; i < hopSize_; ++i)
        history_[frameSize_ - hopSize_ + i] = pending_[i];
    pending_.erase(pending_.begin(), pending_.begin() + hopSize_);

    std::vector<float> windowed(frameSize_);
    for (int n = 0; n < frameSize_; ++n) windowed[n] = history_[n] * window_[n];

    kiss_fftr(fwdCfg_, windowed.data(), spectrum_.data());

    const double eps = 1e-9;
    for (int b = 0; b < numBins_; ++b) {
        double rawMag = std::sqrt((double)spectrum_[b].r * spectrum_[b].r +
                                   (double)spectrum_[b].i * spectrum_[b].i);

        // Smooth the raw per-frame magnitude first: a single STFT frame of
        // broadband noise has high frame-to-frame variance (the magnitude
        // of a stationary random process is itself a random variable with
        // significant spread), so gating on the raw instantaneous value
        // makes the gate flicker open on quiet noise just by chance. A
        // short envelope follower knocks that variance down before the
        // gate decision, at the cost of a few milliseconds of response time.
        if (!floorInitialized_) {
            smoothedMag_[b] = (float)rawMag;
        } else {
            smoothedMag_[b] += (float)((rawMag - smoothedMag_[b]) * 0.3);
        }
        double mag = smoothedMag_[b];

        // Per-bin noise floor: a damped minimum-statistics tracker, bootstrapped
        // from the first analyzed frame so it starts near the real ambient level
        // instead of ramping up from zero over hundreds of frames. After that it
        // decays toward low magnitudes faster than it rises toward high ones, so
        // it settles near the *typical* quiet level rather than the absolute
        // minimum ever observed (an instant snap-to-minimum ratchets down to
        // near zero over many frames of random noise, since some bin/frame
        // combination is always near-silent by chance, which then never lets
        // the gate close on real noise) and resists being dragged up by short
        // loud transients.
        if (!floorInitialized_) {
            noiseFloor_[b] = (float)mag;
        } else if (mag < noiseFloor_[b]) {
            noiseFloor_[b] += (float)((mag - noiseFloor_[b]) * 0.15);
        } else {
            noiseFloor_[b] += (float)((mag - noiseFloor_[b]) * 0.01);
        }

        double magDb = 20.0 * std::log10(mag + eps);
        double floorDb = 20.0 * std::log10((double)noiseFloor_[b] + eps);
        double aboveFloor = magDb - floorDb;

        double gain;
        if (aboveFloor >= thresholdAboveFloorDb_ + kneeDb_) {
            gain = 1.0;
        } else if (aboveFloor <= thresholdAboveFloorDb_) {
            gain = floorGainLinear_;
        } else {
            double t = (aboveFloor - thresholdAboveFloorDb_) / kneeDb_;
            gain = floorGainLinear_ + t * (1.0 - floorGainLinear_);
        }

        spectrum_[b].r = (float)(spectrum_[b].r * gain);
        spectrum_[b].i = (float)(spectrum_[b].i * gain);
    }
    floorInitialized_ = true;

    kiss_fftri(invCfg_, spectrum_.data(), timeBuf_.data());

    // kiss_fftr/kiss_fftri round-trip is unnormalized (scales by nfft).
    for (int n = 0; n < frameSize_; ++n)
        olaBuffer_[n] += timeBuf_[n] / (float)frameSize_;

    for (int i = 0; i < hopSize_; ++i) outputQueue_.push_back(olaBuffer_[i]);
    std::rotate(olaBuffer_.begin(), olaBuffer_.begin() + hopSize_, olaBuffer_.end());
    std::fill(olaBuffer_.end() - hopSize_, olaBuffer_.end(), 0.0f);
}

void SpectralNoiseGate::process(const float* in, float* out, int numSamples) {
    for (int i = 0; i < numSamples; ++i) pending_.push_back(in[i]);
    while ((int)pending_.size() >= hopSize_) processFrame();

    for (int i = 0; i < numSamples; ++i) {
        if (!outputQueue_.empty()) {
            out[i] = outputQueue_.front();
            outputQueue_.pop_front();
        } else {
            out[i] = 0.0f;
        }
    }
}

void SpectralNoiseGate::reset() {
    std::fill(history_.begin(), history_.end(), 0.0f);
    std::fill(olaBuffer_.begin(), olaBuffer_.end(), 0.0f);
    std::fill(noiseFloor_.begin(), noiseFloor_.end(), 1e-6f);
    std::fill(smoothedMag_.begin(), smoothedMag_.end(), 1e-6f);
    floorInitialized_ = false;
    pending_.clear();
    outputQueue_.clear();
}

} // namespace swara
