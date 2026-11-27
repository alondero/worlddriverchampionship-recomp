#include "game_cheats.h"
#include "game_grid.h"
#include "graphics_config.h"
#include "ui/frontend_settings.h"
#include "ui/overlay_state.h"
#include "recompui/config.h"
#include "librecomp/game.hpp"
#include "recomp.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>

SDL_Window* window = nullptr;
std::vector<recomp::GameEntry> supported_games;
static std::optional<wdc::graphics::Settings> requested;
static bool reset_window_mode_requested;
namespace wdc::ui {
void request_settings(graphics::Settings settings, bool reset_window_mode) {
    requested = settings;
    reset_window_mode_requested = reset_window_mode;
}
}
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static std::string read(const std::filesystem::path& path) {
    std::ifstream in(path);
    return {std::istreambuf_iterator<char>(in), {}};
}
int main() {
    const auto dir = std::filesystem::temp_directory_path() /
        ("wdc-frontend-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    try {
        const auto path = dir / "graphics.json";
        recomp::register_config_path(dir);
        wdc::graphics::load(path);
        wdc::graphics::Settings initial;
        initial.resolution = 5; initial.frame_rate = wdc::graphics::FrameRateMode::LegacyCustom; initial.custom_rate = 165; initial.samples = 4;
        initial.filtering = 2; initial.precision = 2;
        require(wdc::graphics::apply(initial), "cannot seed settings");
        const auto original_file = read(path);
        // A hand-written cheats file must not enable anything at startup.
        { std::ofstream file(dir / "cheats.json");
          file << R"({"unlock_tracks":true,"unlock_gt1_cars":true,"unlock_gt2_cars":true})"; }
        const auto stale_cheats = read(dir / "cheats.json");
        { std::ofstream file(dir / "custom-grid.json");
          file << R"({"opponent_1_car":"34","opponent_1_color":"2","opponent_2_car":"35","opponent_2_color":3})"; }
        wdc::ui::create_settings();
        recompui::config::finalize();
        {
            std::vector<uint8_t> memory(8 * 1024 * 1024);
            auto* rdram = memory.data();
            constexpr int32_t config = int32_t(0x80092360);
            MEM_W(0, config) = 1;
            MEM_W(4, config) = 3;
            MEM_W(0x84, config) = 3;
            MEM_W(0x48, config) = 2;
            MEM_W(0x4C, config) = 1;
            MEM_W(0x50, config) = 0;
            MEM_W(0x0C, config) = 11;
            MEM_W(0x2C, config) = 1;
            wdc_apply_custom_grid(rdram);
            require(MEM_W(0x10, config) == 34 && MEM_W(0x30, config) == 2,
                "saved custom grid was not published");
            require(MEM_W(0x0C, config) == 11 && MEM_W(0x2C, config) == 1,
                "invalid custom grid choices did not fall back to game selection");
            wdc::ui::stage_defaults(6);
            MEM_W(0x10, config) = 12;
            MEM_W(0x30, config) = 1;
            wdc_apply_custom_grid(rdram);
            require(MEM_W(0x10, config) == 12 && MEM_W(0x30, config) == 1,
                "restoring defaults did not clear the custom grid");
            require(read(dir / "custom-grid.json").find("\"original\"") != std::string::npos,
                "restoring defaults did not save the custom grid");
        }
        auto& cheats = recompui::config::get_config("cheats");
        require(cheats.external_storage && !cheats.requires_confirmation,
            "cheats must be immediate and session-only");
        require(read(dir / "cheats.json") == stale_cheats, "opening rewrote cheats.json");
        for (const auto& entry : wdc::cheats::catalog) {
            require(!wdc::cheats::enabled(entry.cheat) &&
                    !std::get<bool>(cheats.get_option_value(entry.id)),
                    "stale cheat file was applied");
            cheats.set_option_value(entry.id, true);
            require(wdc::cheats::enabled(entry.cheat), "cheat did not enable immediately");
            require(!cheats.is_dirty(), "cheat requires Apply");
            cheats.set_option_value(entry.id, false);
            require(!wdc::cheats::enabled(entry.cheat), "cheat did not disable immediately");
            wdc::cheats::set_enabled(entry.cheat, true);
            wdc::ui::refresh_settings();
            require(std::get<bool>(cheats.get_option_value(entry.id)), "cheat refresh lost live flag");
            cheats.set_option_value(entry.id, false);
        }
        wdc::cheats::set_enabled(wdc::cheats::Cheat::Tracks, true);
        cheats.set_option_value("unlock_tracks", true);
        wdc::ui::stage_defaults(4);
        require(wdc::cheats::enabled(wdc::cheats::Cheat::Tracks) &&
                std::get<bool>(cheats.get_option_value("unlock_tracks")),
                "Controls reset cleared a cheat");
        for (const auto& entry : wdc::cheats::catalog) cheats.set_option_value(entry.id, true);
        wdc::ui::stage_defaults(5);
        for (const auto& entry : wdc::cheats::catalog)
            require(!wdc::cheats::enabled(entry.cheat) &&
                !std::get<bool>(cheats.get_option_value(entry.id)), "Cheats reset left an option enabled");
        require(!wdc::cheats::enabled(wdc::cheats::Cheat::Tracks) &&
                !std::get<bool>(cheats.get_option_value("unlock_tracks")) &&
                !cheats.is_dirty(),
                "Cheats reset did not clear the session flag");
        require(cheats.save_config() && read(dir / "cheats.json") == stale_cheats,
            "cheat save rewrote the stale file");
        std::filesystem::remove(dir / "cheats.json");
        require(cheats.save_config() && !std::filesystem::exists(dir / "cheats.json"),
            "cheats unexpectedly persisted");
        auto& general = recompui::config::get_config(recompui::config::general::id);
        const auto& rumble_id = recompui::config::general::options::rumble_strength;
        require(recompui::config::general::has_rumble_strength_option() &&
            recompui::config::general::get_rumble_strength() == 25.0, "wrong rumble default");
        general.update_option_value(rumble_id, 63.0);
        require(general.save_config(), "rumble settings did not save");
        general.update_option_value(rumble_id, 0.0);
        require(general.load_config() && recompui::config::general::get_rumble_strength() == 63.0,
            "rumble strength did not survive reload");
        wdc::ui::stage_defaults(3);
        require(recompui::config::general::get_rumble_strength() == 25.0,
            "General reset did not restore rumble strength");
        auto& driving = recompui::config::get_config("driving-controls");
        require(!driving.external_storage && !driving.requires_confirmation, "Driving must use live native storage");
        require(std::get<uint32_t>(driving.get_option_value("throttle_mode")) == 0 &&
            std::get<uint32_t>(driving.get_option_value("brake_mode")) == 0 &&
            std::get<uint32_t>(driving.get_option_value("throttle_axis")) == 6 &&
            std::get<uint32_t>(driving.get_option_value("brake_axis")) == 5 &&
            std::get<double>(driving.get_option_value("throttle_deadzone")) == 0.05 &&
            std::get<double>(driving.get_option_value("brake_saturation")) == 1.0,
            "wrong Driving defaults");
        driving.update_option_value("throttle_mode", 1u);
        driving.update_option_value("brake_mode", 1u);
        driving.update_option_value("throttle_axis", 4u);
        driving.update_option_value("throttle_negative", true);
        driving.update_option_value("brake_deadzone", 0.1);
        require(driving.save_config(), "Driving save failed");
        driving.update_option_value("throttle_axis", 0u);
        require(driving.load_config() && std::get<uint32_t>(driving.get_option_value("throttle_axis")) == 4 &&
            std::get<bool>(driving.get_option_value("throttle_negative")) &&
            std::get<double>(driving.get_option_value("brake_deadzone")) == 0.1, "Driving settings did not survive reload");
        wdc::ui::stage_defaults(2);
        require(std::get<uint32_t>(driving.get_option_value("throttle_mode")) == 0 &&
            std::get<uint32_t>(driving.get_option_value("throttle_axis")) == 6 &&
            !std::get<bool>(driving.get_option_value("throttle_negative")) &&
            std::get<double>(driving.get_option_value("brake_deadzone")) == 0.05,
            "Driving reset did not follow live edit policy");
        require(driving.save_config() && std::filesystem::exists(dir / "driving-controls.json"), "Driving defaults did not save");
        auto& page = recompui::config::get_config("graphics");
        require(read(path) == original_file && !requested, "opening overwrote host graphics storage");
        require(page.external_storage && page.requires_confirmation, "settings must be staged and host-owned");
        require(std::get<uint32_t>(page.get_option_value("resolution")) == 5 &&
                std::get<double>(page.get_option_value("rate")) == 165, "custom host values were lost");
        page.update_option_value("resolution", uint32_t(8));
        page.update_option_value("fullscreen", uint32_t(1));
        require(page.is_dirty() && wdc::graphics::snapshot() == initial && !requested, "staged edit reached renderer");
        wdc::ui::refresh_settings();
        require(std::get<uint32_t>(page.get_temp_option_value("resolution")) == 8, "refresh discarded staged edit");
        page.revert_temp_config();
        require(!page.is_dirty() && !requested && read(path) == original_file, "Discard changed live settings");
        page.update_option_value("resolution", uint32_t(3));
        page.update_option_value("rate", double(144));
        page.update_option_value("samples", uint32_t(8));
        page.update_option_value("filtering", uint32_t(0));
        page.update_option_value("precision", uint32_t(1));
        page.update_option_value("aspect", uint32_t(0));
        page.update_option_value("hud", uint32_t(2));
        page.update_option_value("fullscreen", uint32_t(1));
        require(page.save_config() && requested.has_value(), "Apply did not hand settings to SDL");
        wdc::graphics::Settings expected{};
        expected.resolution = 3;
        expected.aspect = 0;
        expected.frame_rate = wdc::graphics::FrameRateMode::LegacyCustom;
        expected.custom_rate = 144;
        expected.samples = 8;
        expected.filtering = 0;
        expected.precision = 1;
        expected.fullscreen = true;
        expected.hud = 2;
        require(*requested == expected && read(path) == original_file, "Apply mapped settings incorrectly or wrote from UI thread");
        require(wdc::graphics::apply(*requested), "SDL-side publish failed");
        requested.reset();
        wdc::ui::refresh_settings();
        wdc::graphics::load(path);
        require(wdc::graphics::snapshot() == expected, "applied settings did not survive restart");
        // A fullscreen shortcut refreshes clean fields without losing another
        // option's uncommitted edit.
        page.update_option_value("samples", uint32_t(2));
        expected.fullscreen = false;
        require(wdc::graphics::apply(expected), "host shortcut failed");
        wdc::ui::refresh_settings();
        require(std::get<uint32_t>(page.get_option_value("fullscreen")) == 0 &&
                std::get<uint32_t>(page.get_temp_option_value("samples")) == 2, "host refresh lost staged edits");
        page.revert_temp_config();
        const struct {
            uint32_t choice;
            wdc::graphics::FrameRateMode mode;
            double custom_rate;
        } frame_rates[] = {
            {0, wdc::graphics::FrameRateMode::Original, 60},
            {1, wdc::graphics::FrameRateMode::Native60, 60},
            {2, wdc::graphics::FrameRateMode::InterpolateDisplay, 60},
            {3, wdc::graphics::FrameRateMode::InterpolateCustom, 144},
            {4, wdc::graphics::FrameRateMode::LegacyDisplay, 60},
            {5, wdc::graphics::FrameRateMode::LegacyCustom, 144},
        };
        for (const auto& frame_rate : frame_rates) {
            auto baseline = wdc::graphics::Settings{};
            require(wdc::graphics::apply(baseline), "frame-rate baseline publish failed");
            wdc::ui::refresh_settings();
            page.load_config();
            requested.reset();
            page.update_option_value("rate_mode", frame_rate.choice);
            if (frame_rate.choice == 3 || frame_rate.choice == 5)
                page.update_option_value("rate", 144.0);
            page.save_config();
            require(requested && requested->frame_rate == frame_rate.mode &&
                    requested->custom_rate == frame_rate.custom_rate, "race frame-rate mode mapping failed");
        }
        page.update_option_value("rate_mode", 3u);
        page.update_option_value("rate", 59.94);
        page.save_config();
        require(requested && requested->frame_rate == wdc::graphics::FrameRateMode::InterpolateCustom &&
            requested->custom_rate == 59.94, "fractional interpolated rate lost at Apply");
        require(wdc::graphics::apply(*requested), "interpolated rate publish failed");
        wdc::ui::refresh_settings();
        page.load_config();
        requested.reset();
        page.update_option_value("rate_mode", 0u);
        page.save_config();
        require(requested && requested->frame_rate == wdc::graphics::FrameRateMode::Original &&
            requested->custom_rate == 59.94, "leaving a custom mode erased its target");
        require(wdc::graphics::apply(*requested), "original frame-rate publish failed");
        wdc::ui::refresh_settings();
        page.load_config();
        requested.reset();
        wdc::ui::stage_defaults(0);
        require(page.save_config() && requested &&
            requested->frame_rate == wdc::graphics::FrameRateMode::Original && requested->custom_rate == 60,
            "graphics defaults did not reset the custom frame-rate target");
        // Enhancement edits are staged separately and preserve graphics choices.
        requested.reset();
        auto& enhancements = recompui::config::get_config("enhancements");
        enhancements.load_config();
        require(enhancements.external_storage && enhancements.requires_confirmation,
            "enhancements must use host storage and Apply/Discard");
        require(!enhancements.has_option("native_60fps"), "native 60 fps must be part of race frame rate");
        require(std::get<uint32_t>(enhancements.get_option_value("quick_race_laps")) == 0,
            "quick race laps must default to the course count");
        enhancements.update_option_value("full_car_detail", true);
        enhancements.update_option_value("full_track_detail", true);
        enhancements.update_option_value("draw_distance", uint32_t(16));
        enhancements.update_option_value("quick_race_laps", uint32_t(20));
        wdc::ui::refresh_settings();
        require(!requested && !wdc::graphics::snapshot().full_car_detail &&
            wdc::graphics::snapshot().quick_race_laps == 0,
            "staged enhancement reached game");
        enhancements.revert_temp_config();
        require(!requested && !enhancements.is_dirty() &&
            std::get<uint32_t>(enhancements.get_option_value("quick_race_laps")) == 0,
            "Discard published enhancements");
        enhancements.update_option_value("full_car_detail", true);
        enhancements.update_option_value("full_track_detail", true);
        enhancements.update_option_value("draw_distance", uint32_t(8));
        enhancements.update_option_value("quick_race_laps", uint32_t(15));
        require(enhancements.save_config() && requested, "enhancement Apply failed");
        auto enhanced = wdc::graphics::snapshot();
        enhanced.full_car_detail = true;
        enhanced.full_track_detail = true;
        enhanced.draw_distance = 8;
        enhanced.quick_race_laps = 15;
        require(*requested == enhanced, "enhancement Apply overwrote graphics");
        require(wdc::graphics::apply(*requested), "enhancement publish failed");
        requested.reset();
        wdc::graphics::load(path);
        require(wdc::graphics::snapshot() == enhanced, "enhancements did not survive restart");
        page.load_config();
        page.update_option_value("resolution", uint32_t(6));
        require(page.save_config() && requested && requested->full_car_detail &&
            requested->full_track_detail && requested->draw_distance == 8 && requested->quick_race_laps == 15 &&
            requested->frame_rate == enhanced.frame_rate,
            "graphics Apply reset enhancements");
        requested.reset();
        enhancements.load_config();
        wdc::ui::stage_defaults(1);
        require(enhancements.is_dirty() && !requested && wdc::graphics::snapshot() == enhanced,
            "enhancement defaults were not staged");
        require(std::get<uint32_t>(enhancements.get_temp_option_value("draw_distance")) == 1 &&
            std::get<uint32_t>(enhancements.get_temp_option_value("quick_race_laps")) == 0 &&
            !std::get<bool>(enhancements.get_temp_option_value("full_car_detail")) &&
            !std::get<bool>(enhancements.get_temp_option_value("full_track_detail")), "wrong enhancement defaults");
        enhancements.revert_temp_config();
        require(!enhancements.is_dirty() && wdc::graphics::snapshot() == enhanced, "discard defaults changed game");
        wdc::ui::stage_defaults(1);
        require(enhancements.save_config() && requested && !requested->full_car_detail &&
            !requested->full_track_detail && requested->frame_rate == enhanced.frame_rate && requested->draw_distance == 1 &&
            requested->quick_race_laps == 0 && requested->resolution == enhanced.resolution,
            "defaults Apply lost graphics or retained enhancements");
        // A startup window override is live and survives unrelated changes
        // without replacing the saved preference.
        auto window_override = wdc::graphics::snapshot();
        const bool stored_fullscreen = window_override.fullscreen;
        wdc::graphics::load(path, !stored_fullscreen);
        window_override = wdc::graphics::snapshot();
        require(window_override.fullscreen != stored_fullscreen, "startup window override was ignored");
        window_override.resolution = 4;
        require(wdc::graphics::apply(window_override), "unrelated setting could not be saved");
        require(wdc::graphics::snapshot().fullscreen != stored_fullscreen, "Apply dropped startup window mode");
        wdc::graphics::load(path);
        require(wdc::graphics::snapshot().fullscreen == stored_fullscreen, "Apply saved the startup override");

        // Restore defaults must stage a change when the launch already uses
        // the default mode but the saved preference is still fullscreen.
        wdc::graphics::Settings fullscreen_defaults;
        fullscreen_defaults.fullscreen = true;
        require(wdc::graphics::apply(fullscreen_defaults), "cannot seed fullscreen preference");
        wdc::graphics::load(path, false);
        page.load_config();
        requested.reset();
        const auto fullscreen_file = read(path);
        wdc::ui::stage_defaults(0);
        require(page.is_dirty() && !requested && read(path) == fullscreen_file,
            "Restore defaults must stage a window-mode reset under --windowed");
        page.revert_temp_config();
        require(!page.is_dirty() && !requested && read(path) == fullscreen_file,
            "Discard should leave the saved window mode intact");
        page.update_option_value("resolution", 3u);
        require(page.save_config() && requested && !reset_window_mode_requested &&
            wdc::graphics::apply(*requested, reset_window_mode_requested), "Discard leaked into another Apply");
        wdc::graphics::load(path);
        require(wdc::graphics::snapshot().fullscreen, "Unrelated Apply after Discard cleared fullscreen");

        wdc::graphics::load(path, false);
        page.load_config();
        requested.reset();
        wdc::ui::stage_defaults(0);
        require(page.save_config() && requested && reset_window_mode_requested && !requested->fullscreen &&
            wdc::graphics::apply(*requested, reset_window_mode_requested), "Apply did not commit the window-mode reset");
        wdc::graphics::load(path);
        require(!wdc::graphics::snapshot().fullscreen, "Restored window mode did not survive restart");
        page.load_config();
        requested.reset();
        wdc::ui::stage_defaults(0);
        require(!page.is_dirty() && !requested, "Restore defaults staged a reset after it was already applied");

        // An invalid host file must not be replaced just by loading the UI.
        { std::ofstream out(path); out << "{"; }
        requested.reset();
        wdc::graphics::load(path);
        page.load_config();
        require(read(path) == "{" && !requested, "frontend destroyed malformed host file");

        wdc::ui::OverlayState gate;
        require(!gate.captures_input(), "initial gate captures gameplay");
        gate.open();
        require(gate.captures_input(), "Open leaked guest input before rendering");
        gate.publish_capture(false); // Previous frame finished after the Open request.
        require(gate.captures_input(), "previous frame erased pending Open capture");
        require(gate.take_request() == wdc::ui::OverlayState::Request::Open && gate.captures_input(), "consuming Open leaked input");
        gate.publish_capture(true);
        gate.close();
        require(gate.captures_input(), "Close released input before rendering");
        gate.take_request();
        gate.publish_capture(true); // Dirty tab opened Apply/Discard.
        require(gate.captures_input(), "confirmation prompt leaked input");
        gate.publish_capture(false);
        require(!gate.captures_input(), "closing failed to release capture");
        gate.close(); gate.take_request(); gate.open(); gate.publish_capture(false);
        require(gate.captures_input(), "in-flight close erased a later open");
        std::filesystem::remove_all(dir);
        std::cout << "frontend settings tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        std::filesystem::remove_all(dir);
        return 1;
    }
}
