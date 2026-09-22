#pragma once
#include <SDL.h>
namespace wdc::graphics {
// All functions belong to the SDL main thread.
void attach_window(SDL_Window* window);
void handle_event(SDL_Window* window, const SDL_Event& event);
}
