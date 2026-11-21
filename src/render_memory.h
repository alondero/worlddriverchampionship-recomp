#pragma once

#include <cstdint>
#include <mutex>

namespace wdc::render {
// Optional callbacks installed at renderer creation and valid until shutdown.
// The runtime carries task memory with each graphics action. Tasks and screen
// updates hold the snapshot writer's mutex throughout their access.
struct FrameCallbacks {
    std::mutex& mutex;
    bool (*disable_readback)(uint8_t* task_memory);
    void (*task_finished)();
    // Optional observer for associating a screen update with RT64's swap ID.
    // Called under mutex after updateScreen returns on the graphics worker.
    void (*screen_updated)(uint64_t previous_id, uint64_t current_id);
    // Graphics worker, under mutex. True for a requested swap whose GPU image
    // must be presented even when RT64 sees identical VI registers and RAM.
    bool (*force_present)() = nullptr;
};
}

extern "C" const wdc::render::FrameCallbacks* wdc_render_frame_callbacks();
