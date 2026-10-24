#pragma once

#include <cstdint>

namespace wdc::input {
// Per-device state, owned by the SDL thread. The caller supplies the device
// write so transition/error handling can be checked without driver caching.
class RumbleOutput {
    bool active{};
    bool failed{};
public:
    void apply(uint16_t strength, auto&& send) {
        if (strength == 0) {
            if (!active) return;
            active = false;
            send(0); // Stop once; a transient stop failure must not blacklist the pad.
        } else if (!failed) {
            active = true;
            if (send(strength) != 0) failed = true;
        }
    }
};
}
