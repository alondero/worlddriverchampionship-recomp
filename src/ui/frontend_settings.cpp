#include "frontend.h"
#include "frontend_settings.h"
#include "game_cheats.h"
#include "recompui/config.h"

namespace wdc::ui {
namespace {
using recomp::config::ConfigValueVariant;
constexpr const char* graphics_id = "graphics";
constexpr const char* enhancements_id = "enhancements";
constexpr const char* driving_id = "driving-controls";
constexpr const char* cheats_id = "cheats";
void sync(recomp::config::Config& page, const char* id, ConfigValueVariant value) {
    const auto index = page.get_config_schema().options_by_id.at(id);
    // A host shortcut can update a clean field while another field is staged.
    if (page.modified_options.contains(index) || page.get_option_value(id) == value) return;
    page.update_option_value(id, value);
    // Immediate tabs publish inside update_option_value. Applying them would
    // copy an empty staged value back over the live one.
    if (page.requires_confirmation) page.apply_option_value(id);
}
void apply_graphics() {
    auto& page = recompui::config::get_config(graphics_id);
    auto integer = [&](const char* id) { return int(std::get<uint32_t>(page.get_option_value(id))); };
    auto settings = graphics::snapshot();
    settings.resolution = integer("resolution");
    settings.aspect = integer("aspect");
    settings.hud = integer("hud");
    const int mode = integer("rate_mode");
    settings.rate = mode == 0 ? 0 : mode == 1 ? -1 : int(std::get<double>(page.get_option_value("rate")));
    settings.samples = integer("samples");
    settings.filtering = integer("filtering");
    settings.precision = integer("precision");
    settings.fullscreen = integer("fullscreen") != 0;
    request_settings(settings);
}
void apply_enhancements() {
    auto& page = recompui::config::get_config(enhancements_id);
    auto settings = graphics::snapshot();
    settings.full_car_detail = std::get<bool>(page.get_option_value("full_car_detail"));
    settings.full_track_detail = std::get<bool>(page.get_option_value("full_track_detail"));
    settings.draw_distance = int(std::get<uint32_t>(page.get_option_value("draw_distance")));
    request_settings(settings);
}
void seed_cheats() {
    auto& page = recompui::config::get_config(cheats_id);
    for (const auto& entry : wdc::cheats::catalog)
        sync(page, entry.id, wdc::cheats::enabled(entry.cheat));
}
}
void refresh_settings() {
    auto& page = recompui::config::get_config(graphics_id);
    const auto s = graphics::snapshot();
    sync(page, "resolution", uint32_t(s.resolution));
    sync(page, "aspect", uint32_t(s.aspect));
    sync(page, "hud", uint32_t(s.hud));
    sync(page, "rate_mode", uint32_t(s.rate == 0 ? 0 : s.rate == -1 ? 1 : 2));
    sync(page, "rate", double(s.rate > 0 ? s.rate : 60));
    sync(page, "samples", uint32_t(s.samples));
    sync(page, "filtering", uint32_t(s.filtering));
    sync(page, "precision", uint32_t(s.precision));
    sync(page, "fullscreen", uint32_t(s.fullscreen));
    auto& enhancements = recompui::config::get_config(enhancements_id);
    sync(enhancements, "full_car_detail", s.full_car_detail);
    sync(enhancements, "full_track_detail", s.full_track_detail);
    sync(enhancements, "draw_distance", uint32_t(s.draw_distance));
    seed_cheats();
}
void stage_defaults(int tab_index) {
    // Graphics, Enhancements, Driving, General, Controls, Cheats. Controls keeps
    // its own reset. The pinned frontend exposes the index, not the config ID.
    if (tab_index < 0 || tab_index == 4 || tab_index > 5) return;
    auto& page = recompui::config::get_config(tab_index == 0 ? graphics_id :
        tab_index == 1 ? enhancements_id : tab_index == 2 ? driving_id :
        tab_index == 3 ? recompui::config::general::id.c_str() : cheats_id);
    for (const auto& option : page.get_config_schema().options) {
        const auto value = std::visit([](const auto& choice) -> ConfigValueVariant {
            return choice.default_value;
        }, option.variant);
        page.update_option_value(option.id, value);
    }
}
void create_driving_settings() {
    auto& driving = recompui::config::create_config_tab("Driving", driving_id, false);
    for (const auto& pedal : {std::string("throttle"), std::string("brake")}) {
        const std::string label = pedal == "throttle" ? "Throttle" : "Brake";
        driving.add_enum_option(pedal + "_mode", label + " input mode",
            "Analog adds progressive pedal control. Digital A/B bindings still provide full power. Pedals do not select or cancel menu items.",
            {{0, "Digital"}, {1, "Analog"}}, 0u);
        driving.add_enum_option(pedal + "_axis", label + " controller axis",
            "Choose an SDL gamepad axis. Remove conflicting bindings in Controls if the axis also activates an unwanted N64 input.",
            {{0, "None"}, {1, "Left stick X"}, {2, "Left stick Y"}, {3, "Right stick X"},
             {4, "Right stick Y"}, {5, "Left trigger (LT)"}, {6, "Right trigger (RT)"}}, pedal == "throttle" ? 6u : 5u);
        driving.add_bool_option(pedal + "_negative", label + " negative half-axis",
            "Use the negative direction of a stick axis. Leave off for triggers.", false);
        driving.add_number_option(pedal + "_deadzone", label + " deadzone",
            "Ignore axis movement below this level. Independent of the steering deadzone.", 0, 0.95, 0.01, 2, false, 0.05);
        driving.add_number_option(pedal + "_saturation", label + " saturation",
            "Axis level that produces full power. Values at or below the deadzone use a range of 0.01 above the deadzone.",
            0.05, 1, 0.01, 2, false, 1);
        for (const auto* suffix : {"_axis", "_negative", "_deadzone", "_saturation"})
            driving.add_option_hidden_dependency(pedal + suffix, pedal + "_mode", 0u);
    }
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
    page.add_enum_option("hud", "HUD placement", "Pin race instruments and timing to the edges. Clamp to 16:9 keeps them within easy reach on ultrawide displays. Original keeps the centered layout.",
        {{0, "Original"}, {1, "16:9", "Clamp to 16:9"}, {2, "Full", "Full width"}}, 1u);
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
    auto& enhancements = ui::create_config_tab("Enhancements", enhancements_id, true);
    enhancements.external_storage = true;
    enhancements.add_bool_option("full_car_detail", "Full car detail",
        "Keeps the nearest car detail level at all visible distances. Draw distance still limits visibility. May reduce performance.", false);
    enhancements.add_bool_option("full_track_detail", "Full track detail",
        "Keeps the nearest track detail level for submitted sections. Does not load extra sections; use Draw distance to show more scenery. May reduce performance.", false);
    enhancements.add_enum_option("draw_distance", "Draw distance",
        "Extends the far plane and the number of track sections shown ahead and behind, up to the game's safe section limit. Combine with Full car detail to retain distant cars. Some scenery transitions may remain. Higher values cost performance.",
        {{1, "Original"}, {2, "2x"}, {3, "3x"}, {4, "4x"}}, 1u);
    enhancements.set_load_callback(refresh_settings);
    enhancements.set_save_callback(apply_enhancements);
    create_driving_settings();
    ui::create_general_tab({.has_rumble_strength = true});
    ui::create_controls_tab();
    // After Controls, so Restore defaults stays hidden only on that tab.
    auto& cheats = ui::create_config_tab("Cheats", cheats_id, false);
    cheats.external_storage = true; // Session-only: never load or write cheats.json.
    cheats.set_load_callback(seed_cheats);
    for (const auto& entry : wdc::cheats::catalog) {
        cheats.add_bool_option(entry.id, entry.name, entry.description, false);
        cheats.add_option_change_callback(entry.id,
            [cheat = entry.cheat](ConfigValueVariant value, ConfigValueVariant, recomp::config::OptionChangeContext context) {
                if (context == recomp::config::OptionChangeContext::Permanent)
                    wdc::cheats::set_enabled(cheat, std::get<bool>(value));
            });
    }
}
}
