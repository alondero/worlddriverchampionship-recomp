// Prototype-only batching of the exact XXH3 input byte stream. No guest access,
// retained pointers, heap allocation, or change to the default seed/secret.
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include "xxHash/xxh3.h"

namespace prototype {
template<size_t Capacity = 8192>
class BufferedHash {
    uint8_t bytes_[Capacity];
    size_t used_ = 0;
    bool streaming_ = false;
    XXH3_state_t state_;
public:
    void append(const void* data, size_t size) {
        if (!streaming_ && size <= Capacity - used_) {
            std::memcpy(bytes_ + used_, data, size);
            used_ += size;
            return;
        }
        if (!streaming_) {
            XXH3_64bits_reset(&state_);
            XXH3_64bits_update(&state_, bytes_, used_);
            streaming_ = true;
        }
        XXH3_64bits_update(&state_, data, size);
    }
    uint64_t digest() const {
        return streaming_ ? XXH3_64bits_digest(&state_) : XXH3_64bits(bytes_, used_);
    }
    bool streamed() const { return streaming_; }
};
}
