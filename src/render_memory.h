#pragma once

#include <cstdint>
#include <mutex>

namespace wdc::render {
struct TaskMemory {
    uint8_t* rdram;
    bool disable_readback;
};

// Optional source installed at renderer creation and valid until shutdown.
// Graphics tasks and screen updates hold the same mutex as the snapshot writer.
// The source retains each bank through its queued task and screen completion.
struct FrameSource {
    std::mutex& mutex;
    // Graphics worker, in task order. Supply a non-null, word-swapped guest
    // memory image for the entire task and its queued framebuffer reads.
    TaskMemory (*next_task)();
    void (*task_finished)();
    // Optional observer for associating a screen update with RT64's swap ID.
    // Called under mutex after updateScreen returns on the graphics worker.
    void (*screen_updated)(uint64_t previous_id, uint64_t current_id);
    // Graphics worker, under mutex. True for a requested swap whose GPU image
    // must be presented even when RT64 sees identical VI registers and RAM.
    bool (*force_present)() = nullptr;
};
}

extern "C" const wdc::render::FrameSource* wdc_render_frame_source();
