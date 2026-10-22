#pragma once
#include <SDL.h>
#include "graphics_config.h"
namespace wdc::graphics {
// All functions belong to the SDL main thread.
void attach_window(SDL_Window* window);
void handle_event(SDL_Window* window, const SDL_Event& event);
// Apply fullscreen and publish/save settings on the SDL thread. An empty
// result means success; failures preserve the live change and explain it.
std::string apply_window_settings(SDL_Window* window, Settings settings);
}
