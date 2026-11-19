#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <SDL.h>

namespace wdc::input {
// Serialize frontend profile edits/rendering with SDL sampling. Always acquire
// this before the frontend's internal UI lock. Guest callbacks use only read().
std::recursive_mutex& mutex();
void configure(); // Before config::finalize loads profiles.
// SDL main thread, once after loading frontend settings and before starting the runtime.
void initialize_controllers(const std::filesystem::path& executable_dir,
                            const std::filesystem::path& settings_dir);
void handle_event(SDL_Event& event); // SDL main thread, once per event.
void sample(bool captured); // SDL main thread, once after draining events.
void stop_rumble(); // SDL main thread, before exit or closing controller handles.
bool read(int port, uint16_t* buttons, float* x, float* y);
}
