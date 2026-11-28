#include "launch_options.h"
#include "graphics_config.h"
#include "savestate.h"
#include "wdc_build_revision.h"
#include "json/json.hpp"
#include <SDL.h>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <stdexcept>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace {
using nlohmann::json;

json absolute_path(const std::filesystem::path& path) {
    if (path.empty()) return nullptr;
    const auto utf8 = std::filesystem::absolute(path).lexically_normal().u8string();
    return std::string(utf8.begin(), utf8.end());
}

json environment_value(const char* name) {
    if (const auto* value = std::getenv(name)) return value;
    return nullptr;
}
}

void wdc::write_launch_record(const std::filesystem::path& executable, const std::filesystem::path& rom,
                              const std::filesystem::path& data_dir, const std::filesystem::path& log,
                              bool shared_location) {
    std::filesystem::path temporary;
    try {
        auto directory = data_dir;
        if (shared_location) {
            if (char* pref = SDL_GetPrefPath("WDC", "World Driver Championship Recompiled")) {
                std::unique_ptr<char, decltype(&SDL_free)> owned(pref, SDL_free);
                directory = std::filesystem::path(reinterpret_cast<const char8_t*>(pref));
            }
        }
        const auto path = directory / "last-launch.json";
        const auto timestamp = std::chrono::system_clock::now().time_since_epoch();
#ifdef _WIN32
        const auto pid = _getpid();
#else
        const auto pid = getpid();
#endif
        const auto& checkpoint = state::startup_paths();
        const json record = {
            {"schema", 1},
            {"started_unix_ms", std::chrono::duration_cast<std::chrono::milliseconds>(timestamp).count()},
            {"pid", pid},
            {"version", WDC_VERSION},
            {"revision", WDC_BUILD_REVISION},
            {"tracked_changes", WDC_BUILD_TRACKED_CHANGES},
            {"executable", absolute_path(executable)},
            {"working_directory", absolute_path(std::filesystem::current_path())},
            {"rom", absolute_path(rom)},
            {"data_directory", absolute_path(data_dir)},
            {"checkpoint", {{"slot", absolute_path(checkpoint.slot)},
                            {"automatic_save", absolute_path(checkpoint.automatic_save)},
                            {"automatic_load", absolute_path(checkpoint.automatic_load)}}},
            {"warp", environment_value("WDC_WARP")},
            {"warp_at", environment_value("WDC_WARP_AT")},
            {"graphics", json::parse(graphics::settings_json(graphics::snapshot()))},
            {"log", {{"kind", log.empty() ? "inherited" : "file"}, {"path", absolute_path(log)}}},
        };
        temporary = directory / ("last-launch-" + std::to_string(pid) + ".tmp");
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        out << record.dump(2, ' ', false, json::error_handler_t::replace) << '\n';
        out.close();
        if (!out) throw std::runtime_error("cannot write startup record");
        std::filesystem::rename(temporary, path);
        wdc::log(wdc::LogLevel::Info, "[wdc] launch record: %s\n",
                 absolute_path(path).get<std::string>().c_str());
    } catch (const std::exception& e) {
        std::error_code ignored;
        if (!temporary.empty()) std::filesystem::remove(temporary, ignored);
        wdc::log(wdc::LogLevel::Warn, "[wdc] could not record launch: %s\n", e.what());
    }
}
