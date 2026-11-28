#pragma once
#include <cstdint>
#include <filesystem>

namespace wdc::state {
struct CheckpointPaths {
    std::filesystem::path slot, automatic_save, automatic_load;
};
// Configure once on the main thread before starting the guest. Requests are
// thread-safe; file I/O and guest access belong to the dispatcher hook.
bool configure(const std::filesystem::path& default_slot);
const CheckpointPaths& startup_paths(); // Main thread, before starting the guest.
void request_save();
void request_load();
// Game dispatcher thread only. Advances after a successful restoration.
uint64_t restore_generation();
}
