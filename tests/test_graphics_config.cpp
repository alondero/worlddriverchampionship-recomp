#include "graphics_config.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>

using namespace wdc::graphics;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
int main() {
    const auto original_directory = std::filesystem::current_path();
    const auto dir = std::filesystem::temp_directory_path() /
        ("wdc-graphics-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    try {
        // An uninitialized save must publish live settings without staging a
        // write against the empty path (which otherwise overwrites CWD/.tmp).
        std::filesystem::current_path(dir);
        const auto temporary = dir / ".tmp";
        { std::ofstream out(temporary); out << "preserve unconfigured save sentinel"; }
        auto unconfigured = Settings{};
        unconfigured.resolution = 2;
        require(!apply(unconfigured), "unconfigured save unexpectedly succeeded");
        require(snapshot() == unconfigured, "unconfigured save lost live settings");
        std::ifstream before_load(temporary);
        const std::string preserved_temporary{std::istreambuf_iterator<char>(before_load), {}};
        before_load.close();
        require(preserved_temporary == "preserve unconfigured save sentinel",
            "unconfigured save wrote a temporary file");
        std::filesystem::current_path(original_directory);
        const auto path = dir / "graphics.json";
        load(path);
        require(snapshot() == Settings{}, "missing file must use defaults");
        Settings s;
        s.resolution = 4; s.frame_rate = FrameRateMode::InterpolateCustom; s.custom_rate = 539.975; s.samples = 4;
        s.filtering = 2; s.precision = 2; s.fullscreen = true;
        s.hud = 2;
        s.full_car_detail = true; s.full_track_detail = true; s.draw_distance = 8;
        s.quick_race_laps = 15;
        require(apply(s), "save failed");
        load(path);
        require(snapshot() == s, "settings did not survive restart");
        require(apply(Settings{}), "replacing an existing file failed");
        load(path);
        require(snapshot() == Settings{}, "defaults were not persisted");
        Settings inactive_custom;
        inactive_custom.custom_rate = 59.94;
        require(apply(inactive_custom), "inactive custom target save failed");
        load(path);
        require(snapshot() == inactive_custom, "inactive custom target did not survive restart");
        require(apply(s), "restoring full settings failed");
        const struct {
            const char* json;
            FrameRateMode mode;
            double custom_rate;
        } migrations[] = {
            {R"({"version":2,"rate":-1,"interpolate_races":true,"native_60fps":true})",
                FrameRateMode::InterpolateDisplay, 60},
            {R"({"version":2,"rate":119.88,"interpolate_races":true})",
                FrameRateMode::InterpolateCustom, 119.88},
            {R"({"version":2,"rate":-1})", FrameRateMode::LegacyDisplay, 60},
            {R"({"version":2,"rate":59.94})", FrameRateMode::LegacyCustom, 60},
            {R"({"version":2,"rate":144,"native_60fps":true})", FrameRateMode::Native60, 144},
            {R"({"version":2,"native_60fps":true})", FrameRateMode::Native60, 60},
            {R"({"version":2,"rate":0,"interpolate_races":true})", FrameRateMode::Original, 60},
        };
        for (const auto& migration : migrations) {
            { std::ofstream out(path); out << migration.json; }
            load(path);
            const auto migrated = snapshot();
            if (migrated.frame_rate != migration.mode || migrated.custom_rate != migration.custom_rate)
                std::fprintf(stderr, "migration %s produced mode %d rate %.3f\n", migration.json,
                    int(migrated.frame_rate), migrated.custom_rate);
            require(migrated.frame_rate == migration.mode && migrated.custom_rate == migration.custom_rate,
                "legacy frame-rate settings did not migrate to one mode");
        }
        auto shortcut = Settings{};
        shortcut.custom_rate = 59.94;
        const FrameRateMode cycle[] = {
            FrameRateMode::Original, FrameRateMode::Native60, FrameRateMode::InterpolateDisplay,
            FrameRateMode::InterpolateCustom, FrameRateMode::LegacyDisplay, FrameRateMode::LegacyCustom,
        };
        for (int i = 0; i < 6; ++i) {
            shortcut.frame_rate = cycle[i];
            const auto next = cycle_frame_rate(shortcut);
            require(next.frame_rate == cycle[(i + 1) % 6] && next.custom_rate == shortcut.custom_rate,
                "frame-rate shortcut did not cycle unified modes");
        }
        auto transition = Settings{};
        transition.frame_rate = FrameRateMode::InterpolateCustom;
        transition.custom_rate = 59.94;
        require(apply(transition), "interpolated custom transition setup failed");
        transition = cycle_frame_rate(snapshot());
        require(transition.frame_rate == FrameRateMode::LegacyDisplay && apply(transition),
            "applying legacy display transition failed");
        transition = cycle_frame_rate(snapshot());
        require(transition.frame_rate == FrameRateMode::LegacyCustom && apply(transition),
            "applying legacy custom transition failed");
        require(legacy_custom_target(snapshot()) == 60 && snapshot().custom_rate == 59.94,
            "legacy presentation did not use a separate whole-number target");
        load(path);
        require(snapshot().frame_rate == FrameRateMode::LegacyCustom && snapshot().custom_rate == 59.94 &&
            legacy_custom_target(snapshot()) == 60, "legacy custom target did not survive restart");
        while (snapshot().frame_rate != FrameRateMode::InterpolateCustom) {
            require(apply(cycle_frame_rate(snapshot())), "applying frame-rate transition failed");
        }
        load(path);
        require(snapshot().frame_rate == FrameRateMode::InterpolateCustom && snapshot().custom_rate == 59.94,
            "cycling through legacy custom erased the interpolated target");
        auto described = Settings{};
        described.frame_rate = FrameRateMode::Native60;
        require(describe(described).find("Native 60 fps") != std::string::npos,
            "window title omits Native 60 fps");
        described.frame_rate = FrameRateMode::LegacyDisplay;
        require(describe(described).find("Legacy display rate") != std::string::npos,
            "window title does not identify legacy display presentation");
        described.frame_rate = FrameRateMode::LegacyCustom;
        described.custom_rate = 59.94;
        require(describe(described).find("Legacy 60 Hz") != std::string::npos,
            "window title does not use the whole-number legacy target");
        for (int hud : {-1, 0, 1, 2, 3}) {
            { std::ofstream out(path); out << "{\"version\":2,\"hud\":" << hud << "}"; }
            load(path);
            require(snapshot().hud == (hud >= 0 && hud <= 2 ? hud : 1), "HUD placement not normalized");
        }
        for (int aspect : {0, 1, 2}) {
            { std::ofstream out(path); out << "{\"aspect\":" << aspect << ",\"resolution\":4}"; }
            load(path);
            require(snapshot().aspect == 1 && snapshot().resolution == 4,
                "legacy forced-4:3 setting did not upgrade to the wider camera");
            auto requested = snapshot();
            requested.aspect = 0;
            require(apply(requested), "saving original aspect failed");
            load(path);
            require(snapshot().aspect == 0, "explicit original aspect did not survive restart");
        }
        for (const char* invalid : {"{", "[]", "{\"version\":3}", "{\"resolution\":\"4\"}",
                                   "{\"rate\":18446744073709551615}", "{\"samples\":2.5}",
                                   "{\"full_car_detail\":1}", "{\"full_track_detail\":\"true\"}",
                                   "{\"draw_distance\":2.5}", "{\"hud\":\"Full\"}",
                                   "{\"quick_race_laps\":1.5}", "{\"quick_race_laps\":\"10\"}",
                                   "{\"native_60fps\":1}", "{\"native_60fps\":\"true\"}"}) {
            { std::ofstream out(path); out << invalid; }
            load(path);
            require(snapshot() == Settings{}, "invalid settings were accepted");
            std::ifstream in(path);
            const std::string preserved{std::istreambuf_iterator<char>(in), {}};
            require(preserved == invalid, "load destroyed malformed settings");
        }
        { std::ofstream out(path); out << R"({"resolution":999,"aspect":-3,"rate":1001,"samples":3,"filtering":-1,"precision":99})"; }
        load(path);
        const auto bounded = snapshot();
        require(bounded.resolution == 8 && bounded.aspect == 1 && bounded.frame_rate == FrameRateMode::Original &&
            bounded.custom_rate == 60 && bounded.samples == 1 && bounded.filtering == 1 && bounded.precision == 0,
            "values not bounded");
        for (int distance : {-10, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 100}) {
            auto requested = s;
            requested.draw_distance = distance;
            require(apply(requested), "distance save failed");
            load(path);
            const int expected = distance == 16 ? 16 : distance < 1 ? 1 : distance > 8 ? 8 : distance;
            require(snapshot().draw_distance == expected, "distance not bounded/persisted");
        }
        for (int laps : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 15, 20}) {
            { std::ofstream out(path); out << "{\"version\":2,\"resolution\":3,\"quick_race_laps\":" << laps << "}"; }
            load(path);
            require(snapshot().quick_race_laps == laps && snapshot().resolution == 3,
                "supported lap count not loaded");
            require(apply(snapshot()), "supported lap count not saved");
            load(path);
            require(snapshot().quick_race_laps == laps, "supported lap count not persisted");
        }
        for (int laps : {-1, 11, 12, 13, 14, 16, 17, 18, 19, 21}) {
            { std::ofstream out(path); out << "{\"version\":2,\"resolution\":3,\"quick_race_laps\":" << laps << "}"; }
            load(path);
            require(snapshot().quick_race_laps == 0 && snapshot().resolution == 3,
                "unsupported lap count was kept");
        }
        require(apply(bounded), "restoring bounds fixture failed");
        // A failed save still publishes a whole snapshot and leaves old data intact.
        load(dir / "missing-directory" / "graphics.json");
        require(!apply(s), "save unexpectedly succeeded");
        require(snapshot() == s, "failed save lost live settings");
        load(path);
        std::atomic<bool> stop{}, invalid{};
        std::thread reader([&] {
            while (!stop) {
                auto value = snapshot();
                if (value != bounded && value != s && value != Settings{}) invalid = true;
            }
        });
        bool saved = true;
        for (int i = 0; i < 12; ++i) saved &= apply(i % 2 ? s : Settings{});
        stop = true;
        reader.join();
        require(saved && !invalid, "concurrent snapshots were incomplete");
        std::filesystem::remove_all(dir);
        std::cout << "graphics configuration tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        std::filesystem::current_path(original_directory);
        std::filesystem::remove_all(dir);
        return 1;
    }
}
