#include "frontend.h"
#include "frontend_settings.h"
#include "overlay_state.h"
#include "sdl_host.h"
#include "graphics_window.h"
#include "recompui/recompui.h"
#include "recompui/config.h"
#include "recompui/program_config.h"
#include "elements/ui_label.h"
#include "librecomp/game.hpp"
#include "rt64_render_hooks.h"
#include <mutex>
#include <optional>
#include <string>

// Required RecompFrontend host symbols. WDC retains its own RT64 context.
SDL_Window* window = nullptr;
std::vector<recomp::GameEntry> supported_games;
void init_hook(plume::RenderInterface*, plume::RenderDevice*);
void draw_hook(plume::RenderCommandList*, plume::RenderFramebuffer*);
void deinit_hook();

namespace wdc::ui {
namespace {
OverlayState overlay;
std::mutex settings_mutex;
std::optional<graphics::Settings> pending_settings;
std::string error;
void initialize(plume::RenderInterface* interface, plume::RenderDevice* device) {
    init_hook(interface, device);
    auto context = recompui::config::get_config_context_id();
    context.open();
    auto* modal = recompui::config::get_config_modal();
    auto* button = context.create_element<recompui::Button>(modal->get_header()->get_right(),
        "Restore defaults", recompui::ButtonStyle::Tertiary, recompui::ButtonSize::Medium);
    button->set_white_space(recompui::WhiteSpace::Nowrap);
    button->set_flex_shrink(0.0f);
    button->add_pressed_callback([] {
        // Stage defaults; Apply/Discard handles them just like individual edits.
        auto* active = recompui::config::get_config_modal()->get_active_tab();
        if (active) {
            auto* tabs = static_cast<recompui::TabSet*>(active->get_parent());
            stage_defaults(tabs->get_active_tab());
        }
    });
    context.close();
}
void render(plume::RenderCommandList* commands, plume::RenderFramebuffer* framebuffer) {
    const auto request = overlay.take_request();
    // Keep capture raised while consuming an Open, including before contexts
    // are visible. The SDL/game threads may read the gate during this callback.
    if (request == OverlayState::Request::Open) overlay.publish_capture(true);
    {
        std::lock_guard lock(settings_mutex);
        // Apply crosses to SDL before refreshing, avoiding a stale snapshot in
        // the frame between pressing Apply and the main thread processing it.
        if (!pending_settings) refresh_settings();
        if (!error.empty()) {
            recompui::open_info_prompt("Settings", error, "OK", {}, recompui::ButtonStyle::Warning);
            error.clear();
        }
    }
    if (request == OverlayState::Request::Open) {
        recompui::config::open();
        recompui::config::set_tab("graphics");
    } else if (request == OverlayState::Request::Close) {
        if (recompui::is_prompt_open()) recompui::close_prompt();
        else if (recompui::config::close()) recompui::hide_all_contexts();
    }
    draw_hook(commands, framebuffer);
    overlay.publish_capture(recompui::is_context_capturing_input());
}
void deinitialize() {
    deinit_hook();
    overlay.publish_capture(false);
}
}
void install() {
    install_host_services();
    recompui::programconfig::set_program_name("World Driver Championship: Recompiled");
    recompui::programconfig::set_program_id(u8"WorldDriverChampionshipRecomp");
    recompui::register_primary_font("LatoLatin-Regular.ttf", "LatoLatin");
    recompui::register_extra_font("LatoLatin-Bold.ttf");
    create_settings();
    recompui::config::finalize();
    // WDC starts its selected ROM automatically. The default frontend launcher
    // assumes supported_games[0] exists and also offers unsupported mod flows.
    recompui::register_launcher_init_callback([](recompui::LauncherMenu* menu) {
        auto context = recompui::get_launcher_context_id();
        auto* content = menu->get_menu_container();
        context.create_element<recompui::Label>(content, "Starting World Driver Championship...", recompui::LabelStyle::Large);
    });
    RT64::SetRenderHooks(initialize, render, deinitialize);
}
void set_window(SDL_Window* value) { window = value; attach_host_window(value); }
bool captures_input() { return overlay.captures_input(); }
void toggle_settings() { if (captures_input()) overlay.close(); else overlay.open(); }
bool handle_event(const SDL_Event& event) {
    if (event.type == SDL_KEYDOWN && (event.key.keysym.mod & KMOD_CTRL)) snapshot_host_input();
    if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_ESCAPE || event.key.keysym.sym == SDLK_F1)) {
        if (!event.key.repeat) toggle_settings();
        return true;
    }
    if (captures_input()) {
        switch (event.type) {
        case SDL_KEYDOWN: case SDL_KEYUP: case SDL_TEXTINPUT:
        case SDL_MOUSEMOTION: case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEWHEEL:
            recompui::queue_event(event);
            break;
        default: break;
        }
        return true;
    }
    return false;
}
void request_settings(graphics::Settings settings) {
    std::lock_guard lock(settings_mutex);
    pending_settings = settings;
}
void pump() {
    pump_host_services();
    std::lock_guard lock(settings_mutex);
    if (pending_settings) {
        const auto result = graphics::apply_window_settings(window, *pending_settings);
        pending_settings.reset();
        if (!result.empty()) error = result;
    }
}
void shutdown() { shutdown_host_services(); }
}
