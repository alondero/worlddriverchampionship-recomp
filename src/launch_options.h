#pragma once
#include "log.h"
#include <filesystem>
#include <optional>
#include <string>

namespace wdc {

enum class LaunchAction { Run, Help, Version };

struct LaunchOptions {
    std::filesystem::path rom;
    std::filesystem::path data_dir;
    std::filesystem::path controller_pak;
    std::optional<bool> fullscreen;
    std::optional<LogLevel> log_level;
    LaunchAction action = LaunchAction::Run;
    bool portable = false;
    bool console = false;
};

std::string parse_launch_options(int argc, const char* const* argv, LaunchOptions& options);
const char* launch_help();

#if defined(WDC_PLAYER) && !defined(__ANDROID__)
void write_launch_record(const std::filesystem::path& executable, const std::filesystem::path& rom,
                         const std::filesystem::path& data_dir, const std::filesystem::path& log,
                         bool shared_location);
#endif

#if defined(_WIN32) && defined(WDC_PLAYER)
void open_launch_console(bool allow_new_window);
std::filesystem::path open_launch_log(const std::filesystem::path& data_dir);
void show_launch_message(const char* message, bool error);
int application_main(int argc, char** argv);
#endif

}
