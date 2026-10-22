#include "graphics_window.h"
#include "graphics_config.h"
#include "game_camera.h"
#include "game_hud.h"
#include "ui/frontend.h"
#include <SDL_syswm.h>
#include <algorithm>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

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
#ifdef _WIN32
// The window property owns the menu even while it is detached in fullscreen.
constexpr const wchar_t* menu_property = L"WdcGraphicsMenu";
void window_menu(SDL_Window* window, bool fullscreen) {
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (!SDL_GetWindowWMInfo(window, &info)) return;
    HWND hwnd = info.info.win.window;
    auto menu = reinterpret_cast<HMENU>(GetPropW(hwnd, menu_property));
    SetMenu(hwnd, fullscreen ? nullptr : menu);
    DrawMenuBar(hwnd);
}
#endif
}
std::string apply_window_settings(SDL_Window* window, Settings s) {
    std::string error;
    const bool previous = snapshot().fullscreen;
#ifdef _WIN32
    // Detach before SDL sizes the fullscreen client area to the desktop.
    window_menu(window, s.fullscreen);
#endif
    if (s.fullscreen != previous && SDL_SetWindowFullscreen(window, s.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0)) {
        std::fprintf(stderr, "[wdc] fullscreen: %s\n", SDL_GetError());
        error = "Could not change window mode. The previous window mode was kept. ";
        s.fullscreen = previous;
#ifdef _WIN32
        window_menu(window, previous);
#endif
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
#ifdef _WIN32
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info)) {
        HMENU bar = CreateMenu(), menu = CreatePopupMenu();
        const char* labels[]{"Graphics help\tF1", "Cycle resolution\tF2", "Cycle aspect ratio\tF3",
            "Cycle presentation rate (experimental)\tF4", "Cycle anti-aliasing\tF5", "Cycle output filter\tF6",
            "Cycle color precision (restart)\tF7", "Restore defaults\tF9", "Fullscreen\tF11"};
        for (unsigned i = 0; i < 9; ++i) AppendMenuA(menu, MF_STRING, 4000 + i, labels[i]);
        AppendMenuA(bar, MF_POPUP, reinterpret_cast<UINT_PTR>(menu), "Graphics");
        SetPropW(info.info.win.window, menu_property, bar);
        window_menu(window, snapshot().fullscreen);
        SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
    }
#endif
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
#ifdef _WIN32
    if (event.type == SDL_SYSWMEVENT && event.syswm.msg && event.syswm.msg->msg.win.msg == WM_COMMAND) {
        const auto id = LOWORD(event.syswm.msg->msg.win.wParam);
        const SDL_Keycode keys[]{SDLK_F1, SDLK_F2, SDLK_F3, SDLK_F4, SDLK_F5, SDLK_F6, SDLK_F7, SDLK_F9, SDLK_F11};
        if (id >= 4000 && id < 4009) change(window, keys[id - 4000]);
    }
#endif
}
void release_window(SDL_Window* window) {
#ifdef _WIN32
    SDL_SysWMinfo info{};
    SDL_VERSION(&info.version);
    if (SDL_GetWindowWMInfo(window, &info)) {
        auto menu = reinterpret_cast<HMENU>(RemovePropW(info.info.win.window, menu_property));
        SetMenu(info.info.win.window, nullptr);
        if (menu) DestroyMenu(menu);
    }
#endif
}
}
