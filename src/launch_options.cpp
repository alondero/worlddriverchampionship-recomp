#include "launch_options.h"
#include <string_view>

namespace wdc {
namespace {
std::optional<LogLevel> parse_log_level(std::string_view value) {
    if (value == "error") return LogLevel::Error;
    if (value == "warn" || value == "warning") return LogLevel::Warn;
    if (value == "info") return LogLevel::Info;
    if (value == "debug") return LogLevel::Debug;
    if (value == "trace") return LogLevel::Trace;
    return std::nullopt;
}

std::filesystem::path path_from_argument(std::string_view value) {
#ifdef _WIN32
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
#else
    return std::filesystem::path(value);
#endif
}
}

std::string parse_launch_options(int argc, const char* const* argv, LaunchOptions& options) {
    options = {};
    bool positional_only = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view argument = argv[i];
        if (argument.empty()) return "empty command-line argument";
        if (positional_only || !argument.starts_with('-')) {
            if (!options.rom.empty()) return "specify only one ROM path";
            options.rom = path_from_argument(argument);
            continue;
        }
        if (argument == "--") {
            positional_only = true;
            continue;
        }

        const auto equals = argument.find('=');
        const auto flag = argument.substr(0, equals);
        std::filesystem::path* path = nullptr;
        if (flag == "--rom") path = &options.rom;
        else if (flag == "--data-dir") path = &options.data_dir;
        else if (flag == "--controller-pak") path = &options.controller_pak;

        if (path || flag == "--log-level") {
            std::string_view value;
            if (equals != argument.npos) value = argument.substr(equals + 1);
            else if (i + 1 < argc && argv[i + 1][0] != '-') value = argv[++i];
            if (value.empty()) return std::string(flag) + (path ? " requires a path" : " requires a level");
            if (path) {
                if (path == &options.rom && !path->empty()) return "specify only one ROM path";
                *path = path_from_argument(value);
            } else {
                options.log_level = parse_log_level(value);
                if (!options.log_level) return "--log-level must be error, warn, info, debug, or trace";
            }
        } else if (argument == "--console") {
            options.console = true;
        } else if (argument == "--verbose") {
            options.log_level = LogLevel::Debug;
        } else if (argument == "--fullscreen" || argument == "--windowed") {
            options.fullscreen = argument == "--fullscreen";
        } else if (argument == "--portable") {
            options.portable = true;
        } else if (argument == "-h" || argument == "--help") {
            options.action = LaunchAction::Help;
        } else if (argument == "--version") {
            options.action = LaunchAction::Version;
        } else {
            return "unknown option '" + std::string(argument) + "'";
        }
    }
    if (options.portable && !options.data_dir.empty()) return "--portable and --data-dir cannot be used together";
    return {};
}

const char* launch_help() {
    return "Usage: worlddriverchampionship_modern [options] [ROM]\n\n"
           "  --rom PATH            Load the specified USA ROM\n"
           "  --console             Show live output on Windows\n"
           "  --verbose             Set the port log level to debug\n"
           "  --log-level LEVEL     Set error, warn, info, debug, or trace\n"
           "  --fullscreen          Start in borderless fullscreen for this run\n"
           "  --windowed            Start in a window for this run\n"
           "  --data-dir PATH       Store settings, saves, and logs in this directory\n"
           "  --portable            Use a user-data directory beside the desktop player\n"
           "  --controller-pak PATH Use the specified 32 KiB Controller Pak image\n"
           "  -h, --help            Show this help and exit\n"
           "  --version             Show the version and exit\n";
}
}
