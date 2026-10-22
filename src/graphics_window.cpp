#include "graphics_window.h"
#include "graphics_config.h"
#include "game_camera.h"
#include "game_hud.h"
#include "ui/frontend.h"
#include <algorithm>
#include <cstdio>

// Graphics options live in the RT64 frontend overlay. This file owns the
// cross-platform SDL shortcuts and the window changes they trigger; it does
// not attach a native window menu, because a Win32 menu bar has no
// counterpart on the Linux and Android paths this port also ships.
namespace wdc::graphics {
namespace {
const char* help =
    "F2: Resolution - Auto, 1x, 2x, 3x, 4x, 6x, 8x\n"
    "F3: Aspect - Match window (wider camera), Original 4:3\n"
    "F4: Presentation - Original, Display, 60, 120, 144, 240 Hz\n"
    "F5: Anti-aliasing - Off, 2x, 4x, 8x MSAA\n"
    "F6: Output filter - Nearest, Linear, Pixel scaling\n"
    "F7: Framebuffer precision - Auto, Standard, High (restart)\n"
    "F11 / Alt+Enter: Fullscreen\n"
    "F9: Restore graphics defaults\n\n"
    "Changes are saved; color precision needs a restart.\n"
    "Higher presentation rates are experimental and do not add\n"
    "interpolated motion. Game speed remains unchanged.";
template<size_t N> int cycle(int value, const int (&choices)[N]) {
    auto it = std::find(std::begin(choices), std::end(choices), value);
    return it == std::end(choices) || ++it == std::end(choices) ? choices[0] : *it;
}
void title(SDL_Window* window) {
    const auto s = snapshot();
    const auto text = "World Driver Championship | " + describe(s) +
        " | Filter " + (s.filtering == 0 ? "Nearest" : s.filtering == 1 ? "Linear" : "Pixel") +
        " | Color (restart) " + (s.precision == 0 ? "Auto" : s.precision == 1 ? "Standard" : "High") +
#ifdef WDC_ENABLE_FRONTEND
        " | Esc / F1: Settings";
#else
        " | F1: Graphics help";
#endif
    SDL_SetWindowTitle(window, text.c_str());
}
void change(SDL_Window* window, SDL_Keycode key) {
    auto s = snapshot();
    const int resolutions[]{0, 1, 2, 3, 4, 6, 8}, rates[]{0, -1, 60, 120, 144, 240};
    switch (key) {
    case SDLK_F1:
#ifdef WDC_ENABLE_FRONTEND
        wdc::ui::toggle_settings();
#else
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Graphics settings", help, window);
#endif
        return;
    case SDLK_F2: s.resolution = cycle(s.resolution, resolutions); break;
    case SDLK_F3: s.aspect = 1 - s.aspect; break;
    case SDLK_F4: s.rate = cycle(s.rate, rates); break;
    case SDLK_F5: s.samples = s.samples == 8 ? 1 : s.samples * 2; break;
    case SDLK_F6: s.filtering = (s.filtering + 1) % 3; break;
    case SDLK_F7: s.precision = (s.precision + 1) % 3; break;
    case SDLK_F9: s = Settings{}; break;
    case SDLK_F11: s.fullscreen = !s.fullscreen; break;
    default: return;
    }
    const auto error = apply_window_settings(window, s);
    if (!error.empty()) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING, "Graphics settings", error.c_str(), window);
}
void output_size(SDL_Window* window) {
    int width = 0, height = 0;
    SDL_GetWindowSize(window, &width, &height);
    if (snapshot().aspect == 0) { width = 4; height = 3; }
    wdc::camera::set_output_size(width, height);
    wdc::hud::set_placement(snapshot().hud);
}
}
std::string apply_window_settings(SDL_Window* window, Settings s) {
    std::string error;
    const bool previous = snapshot().fullscreen;
    if (s.fullscreen != previous && SDL_SetWindowFullscreen(window, s.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0)) {
        std::fprintf(stderr, "[wdc] fullscreen: %s\n", SDL_GetError());
        error = "Could not change window mode. The previous window mode was kept. ";
        s.fullscreen = previous;
    }
    if (!apply(s)) error += "Settings applied but could not be saved. Check the user-data directory is writable.";
    output_size(window);
    title(window);
    return error;
}
void attach_window(SDL_Window* window) {
    const auto s = snapshot();
    if (s.fullscreen && SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP)) {
        auto fallback = s;
        fallback.fullscreen = false;
        apply(fallback);
        std::fprintf(stderr, "[wdc] fullscreen: %s\n", SDL_GetError());
    }
    output_size(window);
    title(window);
}
void handle_event(SDL_Window* window, const SDL_Event& event) {
    if (event.type == SDL_WINDOWEVENT && (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
        event.window.event == SDL_WINDOWEVENT_DISPLAY_CHANGED)) output_size(window);
    if (event.type == SDL_KEYDOWN && !event.key.repeat) {
        auto key = event.key.keysym.sym;
        if (key == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)) key = SDLK_F11;
        change(window, key);
    }
}
}
