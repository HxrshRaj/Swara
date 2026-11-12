#pragma once
#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <vector>
#include "swara_ring.h"

namespace swara {

// C++ owner of a C11 swara_ring plus its float storage. Single producer thread
// calls write(), single consumer thread calls read().
class SpscRing {
public:
    explicit SpscRing(size_t capacityPow2)
        : storage_(capacityPow2),
          raw_(::operator new(swara_ring_sizeof(), std::align_val_t(swara_ring_alignof()))),
          align_(swara_ring_alignof()) {
        ring_ = static_cast<swara_ring*>(raw_);
        if (swara_ring_init(ring_, storage_.data(), capacityPow2) != SWARA_OK) {
            release();
            throw std::runtime_error("SpscRing: capacity must be a power of two >= 2");
        }
    }
    ~SpscRing() { release(); }
    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    bool write(const float* src, size_t n) { return swara_ring_write(ring_, src, n) == SWARA_OK; }
    bool read(float* dst, size_t n) { return swara_ring_read(ring_, dst, n) == SWARA_OK; }
    size_t available() const { return swara_ring_available(ring_); }

private:
    void release() { if (raw_) { ::operator delete(raw_, std::align_val_t(align_)); raw_ = nullptr; } }
    std::vector<float> storage_;
    void* raw_;
    size_t align_;
    swara_ring* ring_ = nullptr;
};

} // namespace swara
