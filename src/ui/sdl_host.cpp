#include "sdl_host.h"
#include "recompui/recompui.h"
#include "util/file.h"
#include "RmlUi_Platform_SDL.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace wdc::ui {
namespace {
SDL_Window* host_window{};
std::array<SDL_Cursor*, SDL_NUM_SYSTEM_CURSORS> cursors{}; // SDL thread only.
std::atomic<int> modifiers{};
std::mutex mutex;
int width = 960, height = 720;
std::string clipboard;
std::optional<std::string> clipboard_write;
std::optional<SDL_SystemCursor> cursor_request;
std::optional<bool> capture_request, background_request;
std::vector<std::pair<std::string, std::string>> messages;

class HostSystemInterface final : public Rml::SystemInterface {
    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
public:
    double GetElapsedTime() override {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    }
    void SetMouseCursor(const Rml::String& name) override {
        SDL_SystemCursor cursor = SDL_SYSTEM_CURSOR_ARROW;
        if (name == "move") cursor = SDL_SYSTEM_CURSOR_SIZEALL;
        else if (name == "pointer") cursor = SDL_SYSTEM_CURSOR_HAND;
        else if (name == "resize") cursor = SDL_SYSTEM_CURSOR_SIZENWSE;
        else if (name == "cross") cursor = SDL_SYSTEM_CURSOR_CROSSHAIR;
        else if (name == "text") cursor = SDL_SYSTEM_CURSOR_IBEAM;
        else if (name == "unavailable") cursor = SDL_SYSTEM_CURSOR_NO;
        std::lock_guard lock(mutex);
        cursor_request = cursor;
    }
    void SetClipboardText(const Rml::String& text) override {
        std::lock_guard lock(mutex);
        clipboard = text;
        clipboard_write = text;
    }
    void GetClipboardText(Rml::String& text) override {
        std::lock_guard lock(mutex);
        text = clipboard;
    }
    bool LogMessage(Rml::Log::Type, const Rml::String& text) override {
        std::fprintf(stderr, "[wdc-ui] %s\n", text.c_str());
        return true;
    }
};
int key_modifiers(int sdl) {
    int result = 0;
    if (sdl & KMOD_CTRL) result |= Rml::Input::KM_CTRL;
    if (sdl & KMOD_SHIFT) result |= Rml::Input::KM_SHIFT;
    if (sdl & KMOD_ALT) result |= Rml::Input::KM_ALT;
    if (sdl & KMOD_NUM) result |= Rml::Input::KM_NUMLOCK;
    if (sdl & KMOD_CAPS) result |= Rml::Input::KM_CAPSLOCK;
    return result;
}
bool handle_input(Rml::Context* context, SDL_Event& event) {
    // ConvertKey/ConvertMouseButton are pure mappings in the pinned backend.
    // Do not call its InputEventHandler: it samples SDL and captures the mouse.
    const int mods = key_modifiers(event.type == SDL_KEYDOWN || event.type == SDL_KEYUP ?
        event.key.keysym.mod : modifiers.load(std::memory_order_acquire));
    switch (event.type) {
    case SDL_MOUSEMOTION: return context->ProcessMouseMove(event.motion.x, event.motion.y, mods);
    case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: {
        const bool down = event.type == SDL_MOUSEBUTTONDOWN;
        { std::lock_guard lock(mutex); capture_request = down; }
        const int button = RmlSDL::ConvertMouseButton(event.button.button);
        return down ? context->ProcessMouseButtonDown(button, mods) : context->ProcessMouseButtonUp(button, mods);
    }
    case SDL_MOUSEWHEEL:
        return context->ProcessMouseWheel(float(-event.wheel.y) * (event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1), mods);
    case SDL_KEYDOWN: {
        bool result = context->ProcessKeyDown(RmlSDL::ConvertKey(event.key.keysym.sym), mods);
        if (event.key.keysym.sym == SDLK_RETURN || event.key.keysym.sym == SDLK_KP_ENTER)
            result &= context->ProcessTextInput('\n');
        return result;
    }
    case SDL_KEYUP: return context->ProcessKeyUp(RmlSDL::ConvertKey(event.key.keysym.sym), mods);
    case SDL_TEXTINPUT: return context->ProcessTextInput(Rml::String(event.text.text));
    default: return true;
    }
}
}
void install_host_services() {
    char* base = SDL_GetBasePath();
    if (base) { recompui::file::set_program_path(std::filesystem::u8path(base)); SDL_free(base); }
    recompui::set_host_callbacks({
        .create_system_interface = [] { return std::make_unique<HostSystemInterface>(); },
        .get_window_size = [](int& w, int& h) { std::lock_guard lock(mutex); w = width; h = height; },
        .handle_input = handle_input,
        .set_background_input = [](bool enabled) { std::lock_guard lock(mutex); background_request = enabled; },
        .message_box = [](const char* title, const char* message) {
            std::lock_guard lock(mutex); messages.emplace_back(title, message);
        }
    });
}
void attach_host_window(SDL_Window* window) {
    host_window = window;
    int w, h;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    { std::lock_guard lock(mutex); width = w; height = h; }
    for (int i = 0; i < SDL_NUM_SYSTEM_CURSORS; ++i) cursors[i] = SDL_CreateSystemCursor(SDL_SystemCursor(i));
}
void snapshot_host_input() {
    modifiers.store(SDL_GetModState(), std::memory_order_release);
    char* text = SDL_GetClipboardText();
    if (text) {
        std::lock_guard lock(mutex);
        if (!clipboard_write) clipboard = text;
        SDL_free(text);
    }
}
void pump_host_services() {
    modifiers.store(SDL_GetModState(), std::memory_order_release);
    int w, h;
    SDL_GetWindowSizeInPixels(host_window, &w, &h);
    std::optional<SDL_SystemCursor> cursor;
    std::optional<bool> capture, background;
    std::optional<std::string> text;
    std::vector<std::pair<std::string, std::string>> dialogs;
    {
        std::lock_guard lock(mutex);
        width = w; height = h;
        cursor.swap(cursor_request); capture.swap(capture_request); background.swap(background_request);
        text.swap(clipboard_write); dialogs.swap(messages);
    }
    if (cursor && cursors[*cursor]) SDL_SetCursor(cursors[*cursor]);
    if (capture) SDL_CaptureMouse(*capture ? SDL_TRUE : SDL_FALSE);
    if (background) SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, *background ? "1" : "0");
    if (text) SDL_SetClipboardText(text->c_str());
    const bool visible = recompui::get_cursor_visible();
    static std::optional<bool> previous_visibility;
    if (previous_visibility != visible) { SDL_ShowCursor(visible ? SDL_ENABLE : SDL_DISABLE); previous_visibility = visible; }
    for (const auto& [title, message] : dialogs)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), message.c_str(), host_window);
}
void shutdown_host_services() {
    for (auto* cursor : cursors) if (cursor) SDL_FreeCursor(cursor);
    cursors.fill(nullptr);
}
}
