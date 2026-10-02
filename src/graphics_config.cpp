#include "graphics_config.h"
#include "json/json.hpp"
#include <algorithm>
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
    s.resolution = std::clamp(s.resolution, 0, 8);
    // Raw BOSS triangles are already projected. RT64 expansion stretches the
    // world while retaining the original HUD/scissor; migrate old preferences.
    s.aspect = 0;
    if (s.rate != 0 && s.rate != -1 && (s.rate < 30 || s.rate > 360)) s.rate = 0;
    if (s.samples != 1 && s.samples != 2 && s.samples != 4 && s.samples != 8) s.samples = 1;
    if (s.filtering < 0 || s.filtering > 2) s.filtering = 1;
    if (s.precision < 0 || s.precision > 2) s.precision = 0;
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
            if (!j.is_object() || j.value("version", 1) != 1)
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
            integer("rate", s.rate); integer("samples", s.samples);
            integer("filtering", s.filtering); integer("precision", s.precision);
            s.fullscreen = j.value("fullscreen", false);
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
}
bool apply(Settings s) {
    s = normalize(s);
    { std::lock_guard lock(mutex); current = s; }
    try {
        nlohmann::json j = {{"version", 1}, {"resolution", s.resolution},
            {"aspect", s.aspect}, {"rate", s.rate}, {"samples", s.samples},
            {"filtering", s.filtering}, {"precision", s.precision}, {"fullscreen", s.fullscreen}};
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
    return (s.resolution ? std::to_string(s.resolution) + "x" : "Auto resolution") +
        " | 4:3" +
        " | " + (s.rate == 0 ? "Original rate" : s.rate == -1 ? "Display rate (experimental)" : std::to_string(s.rate) + " Hz (experimental)") +
        " | AA " + std::to_string(s.samples) + "x";
}
}
