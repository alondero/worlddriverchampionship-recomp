#include "graphics_config.h"
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
std::filesystem::path file;
}
Settings normalize(Settings s) {
    if (s.draw_distance != visibility::unlimited_draw_distance)
        s.draw_distance = std::clamp(s.draw_distance, 1, 8);
    s.resolution = std::clamp(s.resolution, 0, 8);
    if (s.aspect < 0 || s.aspect > 1) s.aspect = 1;
    if (s.hud < 0 || s.hud > 2) s.hud = 1;
    if (s.rate != 0 && s.rate != -1) {
        if (!std::isfinite(s.rate) || s.rate < 30 || s.rate > render::DisplayRate::max_hz) s.rate = 0;
        else if (s.interpolate_races) s.rate = std::round(s.rate * 1000) / 1000;
        else s.rate = std::round(s.rate);
    }
    if (s.rate == 0) s.interpolate_races = false;
    if (s.samples != 1 && s.samples != 2 && s.samples != 4 && s.samples != 8) s.samples = 1;
    if (s.filtering < 0 || s.filtering > 2) s.filtering = 1;
    if (s.precision < 0 || s.precision > 2) s.precision = 0;
    if (!laps::supported(s.quick_race_laps)) s.quick_race_laps = 0;
    return s;
}
Settings snapshot() {
    std::lock_guard lock(mutex);
    return current;
}
void load(const std::filesystem::path& path) {
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
            if (j.contains("rate")) {
                if (!j.at("rate").is_number()) throw std::runtime_error("rate");
                s.rate = j.at("rate").get<double>();
            }
            integer("samples", s.samples);
            integer("filtering", s.filtering); integer("precision", s.precision);
            s.fullscreen = j.value("fullscreen", false);
            s.full_car_detail = j.value("full_car_detail", false);
            s.full_track_detail = j.value("full_track_detail", false);
            s.native_60fps = j.value("native_60fps", false);
            s.interpolate_races = j.value("interpolate_races", false);
            integer("draw_distance", s.draw_distance);
            integer("quick_race_laps", s.quick_race_laps);
            // Version 1 forcibly disabled widescreen, including saved 0 values.
            // Upgrade those preferences to the working camera-aware default.
            if (j.value("version", 1) == 1) s.aspect = 1;
        } else if (std::filesystem::exists(file)) {
            throw std::runtime_error("cannot read settings");
        }
        s = normalize(s);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[wdc] graphics settings: %s; using defaults\n", e.what());
        s = Settings{};
    }
    std::lock_guard lock(mutex);
    current = s;
    visibility::configure(s.full_car_detail, s.full_track_detail, s.draw_distance);
    framerate::configure(s.native_60fps && !s.interpolate_races);
    laps::configure(s.quick_race_laps);
}
bool apply(Settings s) {
    s = normalize(s);
    { std::lock_guard lock(mutex); current = s; }
    visibility::configure(s.full_car_detail, s.full_track_detail, s.draw_distance);
    framerate::configure(s.native_60fps && !s.interpolate_races);
    laps::configure(s.quick_race_laps);
    try {
        if (file.empty()) throw std::runtime_error("settings path is not configured");
        nlohmann::json j = {{"version", 2}, {"resolution", s.resolution},
            {"aspect", s.aspect}, {"samples", s.samples},
            {"filtering", s.filtering}, {"precision", s.precision}, {"fullscreen", s.fullscreen},
            {"hud", s.hud},
            {"full_car_detail", s.full_car_detail}, {"full_track_detail", s.full_track_detail},
            {"draw_distance", s.draw_distance}, {"quick_race_laps", s.quick_race_laps},
            {"native_60fps", s.native_60fps},
            {"interpolate_races", s.interpolate_races}};
        // Keep whole rates readable by older players with an integer-only setting.
        j["rate"] = s.rate == std::trunc(s.rate) ? nlohmann::json(int(s.rate)) : nlohmann::json(s.rate);
        auto temporary = file;
        temporary += ".tmp";
        std::ofstream out(temporary, std::ios::trunc);
        out << j.dump(2) << '\n';
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
        std::fprintf(stderr, "[wdc] could not save graphics settings: %s\n", e.what());
        return false;
    }
}
std::string describe(const Settings& s) {
    std::ostringstream rate;
    if (s.interpolate_races) rate << "Interpolated ";
    if (s.rate == 0) rate << "Original rate";
    else if (s.rate == -1) rate << "Display rate";
    else rate << s.rate << " Hz";
    if (s.rate != 0) rate << " (experimental)";
    return (s.resolution ? std::to_string(s.resolution) + "x" : "Auto resolution") +
        (s.aspect == 0 ? " | 4:3" : " | Window aspect") +
        " | " + rate.str() +
        " | AA " + std::to_string(s.samples) + "x";
}
}
