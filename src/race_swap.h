#pragma once
#include <cstdint>

namespace wdc::render {
// On the calling game thread, service only the swap routine's VI waits for
// this image. A null pacing callback suppresses the draw's swap.
class RaceSwap {
public:
    RaceSwap(const uint8_t* image, void (*before_present)());
    ~RaceSwap();
    RaceSwap(const RaceSwap&) = delete;
    RaceSwap& operator=(const RaceSwap&) = delete;
    static const RaceSwap* for_image(const uint8_t* image);
    void (*const before_present)();
private:
    const uint8_t* image_;
    const RaceSwap* previous_;
};
}
