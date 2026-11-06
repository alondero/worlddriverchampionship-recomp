#pragma once
#include <cstdint>
#include <filesystem>

namespace wdc::state {
// Configure once on the main thread before starting the guest. Requests are
// thread-safe; file I/O and guest access belong to the dispatcher hook.
bool configure(const std::filesystem::path& default_slot);
void request_save();
void request_load();
}
