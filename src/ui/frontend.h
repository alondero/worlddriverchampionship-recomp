#pragma once
#include "graphics_config.h"
#include <SDL.h>

namespace wdc::ui {
#ifdef WDC_ENABLE_FRONTEND
// Install before RT64 setup; window and pump/handle_event belong to SDL's main
// thread. Contexts and staged options belong to RT64's presentation callback.
void install();
void set_window(SDL_Window* window);
void toggle_settings();
bool captures_input();
bool handle_event(const SDL_Event& event);
void pump();
void shutdown();
void request_settings(graphics::Settings settings);
#else
inline void install() {}
inline void set_window(SDL_Window*) {}
inline void toggle_settings() {}
inline bool captures_input() { return false; }
inline bool handle_event(const SDL_Event&) { return false; }
inline void pump() {}
inline void shutdown() {}
#endif
}
