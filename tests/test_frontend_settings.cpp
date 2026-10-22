#include "graphics_config.h"
#include "ui/frontend_settings.h"
#include "ui/overlay_state.h"
#include "recompui/config.h"
#include "librecomp/game.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>

SDL_Window* window = nullptr;
std::vector<recomp::GameEntry> supported_games;
static std::optional<wdc::graphics::Settings> requested;
namespace wdc::ui {
void request_settings(graphics::Settings settings) { requested = settings; }
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
        initial.resolution = 5; initial.rate = 165; initial.samples = 4;
        initial.filtering = 2; initial.precision = 2;
        require(wdc::graphics::apply(initial), "cannot seed settings");
        const auto original_file = read(path);
        wdc::ui::create_settings();
        recompui::config::finalize();
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
        wdc::graphics::Settings expected{3, 0, 144, 8, 0, 1, true};
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
        for (uint32_t mode : {0u, 1u}) {
            page.update_option_value("rate_mode", mode);
            page.save_config();
            require(requested && requested->rate == (mode == 0 ? 0 : -1), "original/display rate mapping failed");
        }
        // Enhancement edits are staged separately and preserve graphics choices.
        requested.reset();
        auto& enhancements = recompui::config::get_config("enhancements");
        enhancements.load_config();
        require(enhancements.external_storage && enhancements.requires_confirmation,
            "enhancements must use host storage and Apply/Discard");
        enhancements.update_option_value("full_car_detail", true);
        enhancements.update_option_value("full_track_detail", true);
        enhancements.update_option_value("draw_distance", uint32_t(4));
        wdc::ui::refresh_settings();
        require(!requested && !wdc::graphics::snapshot().full_car_detail,
            "staged enhancement reached game");
        enhancements.revert_temp_config();
        require(!requested && !enhancements.is_dirty(), "Discard published enhancements");
        enhancements.update_option_value("full_car_detail", true);
        enhancements.update_option_value("full_track_detail", true);
        enhancements.update_option_value("draw_distance", uint32_t(4));
        require(enhancements.save_config() && requested, "enhancement Apply failed");
        auto enhanced = wdc::graphics::snapshot();
        enhanced.full_car_detail = true;
        enhanced.full_track_detail = true;
        enhanced.draw_distance = 4;
        require(*requested == enhanced, "enhancement Apply overwrote graphics");
        require(wdc::graphics::apply(*requested), "enhancement publish failed");
        requested.reset();
        wdc::graphics::load(path);
        require(wdc::graphics::snapshot() == enhanced, "enhancements did not survive restart");
        page.load_config();
        page.update_option_value("resolution", uint32_t(6));
        require(page.save_config() && requested && requested->full_car_detail &&
            requested->full_track_detail && requested->draw_distance == 4,
            "graphics Apply reset enhancements");
        requested.reset();
        enhancements.load_config();
        wdc::ui::stage_defaults(1);
        require(enhancements.is_dirty() && !requested && wdc::graphics::snapshot() == enhanced,
            "enhancement defaults were not staged");
        require(std::get<uint32_t>(enhancements.get_temp_option_value("draw_distance")) == 1 &&
            !std::get<bool>(enhancements.get_temp_option_value("full_car_detail")) &&
            !std::get<bool>(enhancements.get_temp_option_value("full_track_detail")), "wrong enhancement defaults");
        enhancements.revert_temp_config();
        require(!enhancements.is_dirty() && wdc::graphics::snapshot() == enhanced, "discard defaults changed game");
        wdc::ui::stage_defaults(1);
        require(enhancements.save_config() && requested && !requested->full_car_detail &&
            !requested->full_track_detail && requested->draw_distance == 1 && requested->resolution == enhanced.resolution,
            "defaults Apply lost graphics or retained enhancements");
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
