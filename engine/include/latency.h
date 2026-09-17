#pragma once
#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <chrono>

namespace swara {

// Accumulates real wall-clock durations (in microseconds) for a sequence of
// buffer-processing calls, so we can report actual measured latency rather
// than a theoretical estimate.
class LatencyStats {
public:
    void record(double microseconds) { samples_.push_back(microseconds); }

    size_t count() const { return samples_.size(); }

    double min() const { return samples_.empty() ? 0.0 : *std::min_element(samples_.begin(), samples_.end()); }
    double max() const { return samples_.empty() ? 0.0 : *std::max_element(samples_.begin(), samples_.end()); }

    double mean() const {
        if (samples_.empty()) return 0.0;
        double sum = 0.0;
        for (double s : samples_) sum += s;
        return sum / samples_.size();
    }

    double percentile(double p) const {
        if (samples_.empty()) return 0.0;
        std::vector<double> sorted = samples_;
        std::sort(sorted.begin(), sorted.end());
        double idx = p / 100.0 * (sorted.size() - 1);
        size_t lo = (size_t)std::floor(idx);
        size_t hi = (size_t)std::ceil(idx);
        if (lo == hi) return sorted[lo];
        double frac = idx - lo;
        return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
    }

    const std::vector<double>& samples() const { return samples_; }

private:
    std::vector<double> samples_;
};

// RAII stopwatch using a monotonic high-resolution clock.
class Stopwatch {
public:
    Stopwatch() : start_(std::chrono::high_resolution_clock::now()) {}
    double elapsedMicroseconds() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::micro>(now - start_).count();
    }
private:
    std::chrono::high_resolution_clock::time_point start_;
};

} // namespace swara
