#include "graphics_config.h"
#include "log.h"
#include "render_cadence.h"
#include "game_visibility.h"
#include "game_framerate.h"
#include "game_laps.h"
#include "json/json.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace wdc::graphics {
namespace {
std::mutex mutex;
Settings current;
// Set by the SDL main thread while a command-line mode is active.
std::optional<bool> saved_fullscreen;
std::filesystem::path file;

bool custom_mode(FrameRateMode mode) {
    return mode == FrameRateMode::InterpolateCustom || mode == FrameRateMode::LegacyCustom;
}
bool interpolated_mode(FrameRateMode mode) {
    return mode == FrameRateMode::InterpolateDisplay || mode == FrameRateMode::InterpolateCustom;
}
bool valid_mode(FrameRateMode mode) {
    switch (mode) {
    case FrameRateMode::Original:
    case FrameRateMode::Native60:
    case FrameRateMode::InterpolateDisplay:
    case FrameRateMode::InterpolateCustom:
    case FrameRateMode::LegacyDisplay:
    case FrameRateMode::LegacyCustom:
        return true;
    }
    return false;
}
void publish(const Settings& s) {
    visibility::configure(s.full_car_detail, s.full_track_detail, s.draw_distance);
    framerate::configure(s.frame_rate == FrameRateMode::Native60);
    laps::configure(s.quick_race_laps);
}
}
Settings normalize(Settings s) {
    if (s.draw_distance != visibility::unlimited_draw_distance)
        s.draw_distance = std::clamp(s.draw_distance, 1, 8);
    s.resolution = std::clamp(s.resolution, 0, 8);
    if (s.aspect < 0 || s.aspect > 1) s.aspect = 1;
    if (s.hud < 0 || s.hud > 2) s.hud = 1;
    if (!valid_mode(s.frame_rate)) s.frame_rate = FrameRateMode::Original;
    const bool valid_rate = std::isfinite(s.custom_rate) && s.custom_rate >= 30 &&
        s.custom_rate <= render::DisplayRate::max_hz;
    if (!valid_rate) {
        s.custom_rate = 60;
        if (custom_mode(s.frame_rate)) s.frame_rate = FrameRateMode::Original;
    } else {
        s.custom_rate = std::round(s.custom_rate * 1000) / 1000;
    }
    if (s.samples != 1 && s.samples != 2 && s.samples != 4 && s.samples != 8) s.samples = 1;
    if (s.filtering < 0 || s.filtering > 2) s.filtering = 1;
    if (s.precision < 0 || s.precision > 2) s.precision = 0;
    if (!laps::supported(s.quick_race_laps)) s.quick_race_laps = 0;
    return s;
}
int legacy_custom_target(const Settings& s) {
    return int(std::lround(s.custom_rate));
}
Settings snapshot() {
    std::lock_guard lock(mutex);
    return current;
}
bool windowed_with_saved_fullscreen() {
    std::lock_guard lock(mutex);
    return !current.fullscreen && saved_fullscreen.value_or(false);
}
void load(const std::filesystem::path& path, std::optional<bool> initial_fullscreen) {
    file = path;
    Settings s;
    try {
        std::ifstream in(file);
        if (in) {
            const auto j = nlohmann::json::parse(in);
            if (!j.is_object() || (j.value("version", 1) != 1 && j.value("version", 1) != 2))
                throw std::runtime_error("unsupported graphics settings format");
            auto integer = [&](const char* key, int& value) {
                if (j.contains(key)) {
                    if (!j.at(key).is_number_integer()) throw std::runtime_error(key);
                    if (j.at(key).is_number_unsigned() && j.at(key).get<uint64_t>() > 10000)
                        throw std::runtime_error(key);
                    const auto number = j.at(key).get<int64_t>();
                    if (number < -10000 || number > 10000) throw std::runtime_error(key);
                    value = int(number);
                }
            };
            integer("resolution", s.resolution); integer("aspect", s.aspect);
            integer("hud", s.hud);
            double stored_rate = 0;
            if (j.contains("rate")) {
                if (!j.at("rate").is_number()) throw std::runtime_error("rate");
                stored_rate = j.at("rate").get<double>();
            }
            double inactive_custom_rate = 60;
            const bool has_inactive_custom_rate = j.contains("custom_rate");
            if (has_inactive_custom_rate) {
                if (!j.at("custom_rate").is_number()) throw std::runtime_error("custom_rate");
                inactive_custom_rate = j.at("custom_rate").get<double>();
            }
            integer("samples", s.samples);
            integer("filtering", s.filtering); integer("precision", s.precision);
            s.fullscreen = j.value("fullscreen", false);
            s.full_car_detail = j.value("full_car_detail", false);
            s.full_track_detail = j.value("full_track_detail", false);
            const bool native_60fps = j.value("native_60fps", false);
            bool interpolate_races = j.value("interpolate_races", false);
            integer("draw_distance", s.draw_distance);
            integer("quick_race_laps", s.quick_race_laps);
            // Version 1 forcibly disabled widescreen, including saved 0 values.
            // Upgrade those preferences to the working camera-aware default.
            if (j.value("version", 1) == 1) s.aspect = 1;

            // Version 2 stored three independent implementation knobs. Fold
            // them into the effective user-facing mode. Interpolation already
            // took precedence over native 60 Hz; native 60 takes precedence
            // over the legacy renderer target because that target cannot add
            // motion interpolation in WDC.
            if (stored_rate != 0 && stored_rate != -1 &&
                (!std::isfinite(stored_rate) || stored_rate < 30 || stored_rate > render::DisplayRate::max_hz))
                stored_rate = 0;
            if (stored_rate == 0) interpolate_races = false;
            if (interpolate_races) {
                s.frame_rate = stored_rate == -1 ? FrameRateMode::InterpolateDisplay : FrameRateMode::InterpolateCustom;
            } else if (native_60fps) {
                s.frame_rate = FrameRateMode::Native60;
            } else if (stored_rate == -1) {
                s.frame_rate = FrameRateMode::LegacyDisplay;
            } else if (stored_rate != 0) {
                s.frame_rate = FrameRateMode::LegacyCustom;
            }
            if (custom_mode(s.frame_rate)) {
                s.custom_rate = stored_rate;
                if (s.frame_rate == FrameRateMode::LegacyCustom && !has_inactive_custom_rate)
                    s.custom_rate = std::round(s.custom_rate);
            } else {
                s.custom_rate = inactive_custom_rate;
                if (s.frame_rate == FrameRateMode::Native60 && !has_inactive_custom_rate && stored_rate > 0)
                    s.custom_rate = std::round(stored_rate);
            }
        } else if (std::filesystem::exists(file)) {
            throw std::runtime_error("cannot read settings");
        }
        s = normalize(s);
    } catch (const std::exception& e) {
        wdc::log(wdc::LogLevel::Warn, "[wdc] graphics settings: %s; using defaults\n", e.what());
        s = Settings{};
    }
    std::lock_guard lock(mutex);
    saved_fullscreen.reset();
    if (initial_fullscreen.has_value()) {
        saved_fullscreen = s.fullscreen;
        s.fullscreen = *initial_fullscreen;
    }
    current = s;
    publish(s);
}
void fallback_to_windowed() {
    auto fallback = snapshot();
    fallback.fullscreen = false;
    if (saved_fullscreen.has_value()) {
        std::lock_guard lock(mutex);
        current = fallback;
    } else {
        apply(fallback);
    }
}
std::string settings_json(const Settings& s) {
    nlohmann::json j = {{"version", 2}, {"resolution", s.resolution},
        {"aspect", s.aspect}, {"samples", s.samples},
        {"filtering", s.filtering}, {"precision", s.precision}, {"fullscreen", s.fullscreen},
        {"hud", s.hud},
        {"full_car_detail", s.full_car_detail}, {"full_track_detail", s.full_track_detail},
        {"draw_distance", s.draw_distance}, {"quick_race_laps", s.quick_race_laps},
        {"native_60fps", s.frame_rate == FrameRateMode::Native60},
        {"interpolate_races", interpolated_mode(s.frame_rate)},
        {"custom_rate", s.custom_rate}};
    double stored_rate = 0;
    if (s.frame_rate == FrameRateMode::InterpolateDisplay || s.frame_rate == FrameRateMode::LegacyDisplay)
        stored_rate = -1;
    else if (custom_mode(s.frame_rate)) stored_rate = s.custom_rate;
    // Keep whole rates readable by older players with an integer-only setting.
    j["rate"] = stored_rate == std::trunc(stored_rate) ? nlohmann::json(int(stored_rate)) : nlohmann::json(stored_rate);
    return j.dump(2);
}
bool apply(Settings s, bool reset_window_mode) {
    s = normalize(s);
    {
        std::lock_guard lock(mutex);
        if (current.fullscreen != s.fullscreen || reset_window_mode) saved_fullscreen.reset();
        current = s;
    }
    publish(s);
    try {
        if (file.empty()) throw std::runtime_error("settings path is not configured");
        s.fullscreen = saved_fullscreen.value_or(s.fullscreen);
        auto temporary = file;
        temporary += ".tmp";
        std::ofstream out(temporary, std::ios::trunc);
        out << settings_json(s) << '\n';
        out.close();
        if (!out) throw std::runtime_error("cannot write settings");
        // Replace only after a complete write, keeping the previous file on failure.
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("cannot replace settings");
#else
        std::filesystem::rename(temporary, file);
#endif
        return true;
    } catch (const std::exception& e) {
        wdc::log(wdc::LogLevel::Error, "[wdc] could not save graphics settings: %s\n", e.what());
        return false;
    }
}
Settings cycle_frame_rate(Settings settings) {
    static constexpr FrameRateMode modes[] = {
        FrameRateMode::Original, FrameRateMode::Native60, FrameRateMode::InterpolateDisplay,
        FrameRateMode::InterpolateCustom, FrameRateMode::LegacyDisplay, FrameRateMode::LegacyCustom,
    };
    for (size_t i = 0; i < std::size(modes); ++i) {
        if (settings.frame_rate == modes[i]) {
            settings.frame_rate = modes[(i + 1) % std::size(modes)];
            return settings;
        }
    }
    settings.frame_rate = FrameRateMode::Original;
    return settings;
}
std::string describe(const Settings& s) {
    std::ostringstream rate;
    switch (s.frame_rate) {
    case FrameRateMode::Original:
        rate << "Original rate";
        break;
    case FrameRateMode::Native60:
        rate << "Native 60 fps (experimental)";
        break;
    case FrameRateMode::InterpolateDisplay:
        rate << "Interpolated display rate (experimental)";
        break;
    case FrameRateMode::InterpolateCustom:
        rate << "Interpolated " << s.custom_rate << " Hz (experimental)";
        break;
    case FrameRateMode::LegacyDisplay:
        rate << "Legacy display rate (experimental)";
        break;
    case FrameRateMode::LegacyCustom:
        rate << "Legacy " << legacy_custom_target(s) << " Hz (experimental)";
        break;
    }
    return (s.resolution ? std::to_string(s.resolution) + "x" : "Auto resolution") +
        (s.aspect == 0 ? " | 4:3" : " | Window aspect") +
        " | " + rate.str() +
        " | AA " + std::to_string(s.samples) + "x";
}
}
