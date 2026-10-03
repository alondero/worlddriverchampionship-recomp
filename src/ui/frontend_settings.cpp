#include "frontend.h"
#include "frontend_settings.h"
#include "recompui/config.h"
#include "elements/ui_container.h"
#include "elements/ui_label.h"

namespace wdc::ui {
namespace {
using recomp::config::ConfigValueVariant;
constexpr const char* graphics_id = "graphics";
void sync(recomp::config::Config& page, const char* id, ConfigValueVariant value) {
    const auto index = page.get_config_schema().options_by_id.at(id);
    // A host shortcut can update a clean field while another field is staged.
    if (page.modified_options.contains(index) || page.get_option_value(id) == value) return;
    page.update_option_value(id, value);
    page.apply_option_value(id);
}
void apply_graphics() {
    auto& page = recompui::config::get_config(graphics_id);
    auto integer = [&](const char* id) { return int(std::get<uint32_t>(page.get_option_value(id))); };
    graphics::Settings settings;
    settings.resolution = integer("resolution");
    settings.aspect = integer("aspect");
    const int mode = integer("rate_mode");
    settings.rate = mode == 0 ? 0 : mode == 1 ? -1 : int(std::get<double>(page.get_option_value("rate")));
    settings.samples = integer("samples");
    settings.filtering = integer("filtering");
    settings.precision = integer("precision");
    settings.fullscreen = integer("fullscreen") != 0;
    request_settings(settings);
}
void controls(recompui::ContextId context, recompui::Element* parent) {
    using namespace recompui;
    auto* content = context.create_element<Container>(parent, FlexDirection::Column, JustifyContent::FlexStart);
    content->set_padding(32.0f);
    content->set_gap(16.0f);
    content->set_overflow_y(Overflow::Auto);
    context.create_element<Label>(content, "Keyboard controls", LabelStyle::Large);
    for (const char* line : {
        "Enter: Start / pause", "X: A / accelerate / select", "Z: B / brake / back",
        "Arrows or WASD: steer / move", "Space: Z trigger", "Right Shift: R",
        "Escape or F1: open / close settings", "F11 or Alt+Enter: fullscreen",
        "Menu: mouse, arrows / Tab and Enter. Use Apply to save changes.",
        "Controls are fixed in this version. Gamepad input, rebinding and rumble are not available.",
        "Pause the race with Enter before opening settings. The game continues running while settings are open."}) {
        context.create_element<Label>(content, line, LabelStyle::Normal);
    }
}
}
void refresh_settings() {
    auto& page = recompui::config::get_config(graphics_id);
    const auto s = graphics::snapshot();
    sync(page, "resolution", uint32_t(s.resolution));
    sync(page, "aspect", uint32_t(s.aspect));
    sync(page, "rate_mode", uint32_t(s.rate == 0 ? 0 : s.rate == -1 ? 1 : 2));
    sync(page, "rate", double(s.rate > 0 ? s.rate : 60));
    sync(page, "samples", uint32_t(s.samples));
    sync(page, "filtering", uint32_t(s.filtering));
    sync(page, "precision", uint32_t(s.precision));
    sync(page, "fullscreen", uint32_t(s.fullscreen));
}
void create_settings() {
    namespace ui = recompui::config;
    auto& page = ui::create_config_tab("Graphics", graphics_id, true);
    page.external_storage = true; // graphics.json remains owned by the SDL host.
    page.add_enum_option("fullscreen", "Window mode", "Fullscreen uses the desktop resolution. Changes apply when you press Apply.",
        {{0, "Windowed"}, {1, "Fullscreen"}}, 0u);
    page.add_enum_option("resolution", "Internal resolution", "Auto follows the window. Fixed values multiply the original resolution; higher values require more GPU power.",
        {{0, "Auto"}, {1, "1x"}, {2, "2x"}, {3, "3x"}, {4, "4x"}, {5, "5x"}, {6, "6x"}, {7, "7x"}, {8, "8x"}}, 0u);
    page.add_enum_option("aspect", "Aspect ratio", "Match window widens the camera while preserving proportions. Original keeps the 4:3 view. Narrow windows retain 4:3.",
        {{0, "Original", "Original 4:3"}, {1, "Window", "Match window"}}, 1u);
    page.add_enum_option("rate_mode", "Presentation rate", "Original is recommended. Display and Custom are experimental. They do not smooth motion or change game speed.",
        {{0, "Original"}, {1, "Display"}, {2, "Custom"}}, 0u);
    page.add_number_option("rate", "Custom presentation target (Hz)", "A presentation target from 30 to 360 Hz. This does not add motion interpolation.", 30, 360, 1, 0, false, 60);
    page.add_option_hidden_dependency("rate", "rate_mode", 0u, 1u);
    page.add_enum_option("samples", "Anti-aliasing", "Smooths edges. Unsupported MSAA falls back to Off; your requested choice is saved.",
        {{1, "Off"}, {2, "2x"}, {4, "4x"}, {8, "8x"}}, 1u);
    page.add_enum_option("filtering", "Output filter", "Filters the final image. This does not replace or enhance the game's texture assets.",
        {{0, "Nearest"}, {1, "Linear"}, {2, "Pixel", "Pixel scaling"}}, 1u);
    page.add_enum_option("precision", "Color precision (restart)", "Changes take effect after restarting the player. High precision affects internal framebuffers; it does not enable HDR monitor output.",
        {{0, "Auto"}, {1, "Standard"}, {2, "High"}}, 0u);
    page.set_load_callback(refresh_settings);
    page.set_save_callback(apply_graphics);
    // The framework reads General internally. None of its controller settings
    // currently affect WDC's keyboard adapter, so keep this internal tab hidden.
    auto& general = ui::create_general_tab({.has_rumble_strength = false});
    general.external_storage = true;
    ui::set_tab_visible(ui::general::id, false);
    ui::create_tab("Controls", "wdc-controls", controls);
}
}
