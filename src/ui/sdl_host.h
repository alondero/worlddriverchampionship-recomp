#pragma once
#include <SDL.h>
namespace wdc::ui {
// All entry points below belong to the SDL main thread. The installed UI
// callbacks read snapshots or enqueue commands, never call SDL themselves.
void install_host_services();
void attach_host_window(SDL_Window* window);
void snapshot_host_input();
void pump_host_services();
void shutdown_host_services();
}
