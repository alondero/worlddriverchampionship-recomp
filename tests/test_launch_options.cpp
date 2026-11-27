#include "launch_options.h"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string_view>
#include <vector>

namespace {
using wdc::LaunchAction;
using wdc::LaunchOptions;
using wdc::LogLevel;

void require(bool condition, std::string_view message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

using Arguments = std::initializer_list<const char*>;

std::string parse(Arguments args, LaunchOptions& options) {
    std::vector<const char*> argv{"worlddriverchampionship_modern"};
    argv.insert(argv.end(), args.begin(), args.end());
    return wdc::parse_launch_options(static_cast<int>(argv.size()), argv.data(), options);
}

LaunchOptions accepted(Arguments args) {
    LaunchOptions options;
    const auto error = parse(args, options);
    require(error.empty(), error);
    return options;
}

void rejected(Arguments args, std::string_view expected) {
    LaunchOptions options;
    const auto error = parse(args, options);
    if (error != expected) {
        std::cerr << "Expected '" << expected << "', got '" << error << "'\n";
        std::exit(1);
    }
}
}

int main() {
    auto options = accepted({"--console", "--rom", "USA ROM.z64", "--fullscreen",
                             "--data-dir=Race Profile", "--controller-pak", "Player Save.mpk",
                             "--log-level", "trace"});
    require(options.action == LaunchAction::Run && options.console && !options.portable,
        "named options changed the startup action");
    require(options.rom == "USA ROM.z64" && options.data_dir == "Race Profile" &&
        options.controller_pak == "Player Save.mpk", "paths with spaces were not kept intact");
    require(options.fullscreen == true && options.log_level == LogLevel::Trace,
        "fullscreen or separated log level was ignored");

    options = accepted({"--windowed", "USA ROM.z64", "--controller-pak=-backup.mpk", "--portable", "--verbose"});
    require(options.rom == "USA ROM.z64" && options.controller_pak == "-backup.mpk" &&
        options.portable && options.data_dir.empty(), "positional ROM or portable mode was ignored");
    require(options.fullscreen == false && options.log_level == LogLevel::Debug,
        "windowed or verbose mode was ignored");

    options = accepted({"--rom=Test \xE6\x97\xA5.z64", "--data-dir", "Profile"});
    require(options.rom == std::filesystem::path(u8"Test \u65E5.z64") && options.data_dir == "Profile",
        "equals ROM path lost its UTF-8 name or separated data directory");
    options = accepted({"--console", "--", "--help"});
    require(options.rom == "--help" && options.action == LaunchAction::Run && options.console,
        "option-shaped ROM after -- was treated as a flag");

    options = accepted({"--fullscreen", "--windowed", "--verbose", "--log-level=warn"});
    require(options.fullscreen == false && options.log_level == LogLevel::Warn, "last options did not win");
    options = accepted({"--windowed", "--fullscreen", "--log-level", "error", "--verbose"});
    require(options.fullscreen == true && options.log_level == LogLevel::Debug, "reverse option order did not win");
    require(accepted({"--log-level=error"}).log_level == LogLevel::Error, "error level was ignored");
    require(accepted({"--log-level=warning"}).log_level == LogLevel::Warn, "warning alias was ignored");
    require(accepted({"--log-level=info"}).log_level == LogLevel::Info, "info level was ignored");
    require(accepted({"--log-level=debug"}).log_level == LogLevel::Debug, "debug level was ignored");
    require(accepted({"-h"}).action == LaunchAction::Help, "-h did not request help");
    require(accepted({"--help"}).action == LaunchAction::Help, "--help did not request help");
    require(accepted({"--version"}).action == LaunchAction::Version, "--version did not request version");

    require(parse({}, options).empty() && options.action == LaunchAction::Run &&
        options.rom.empty() && options.data_dir.empty() && options.controller_pak.empty() &&
        !options.fullscreen && !options.log_level && !options.portable && !options.console,
        "parsing an empty command line did not restore the defaults");

    rejected({""}, "empty command-line argument");
    rejected({"--invalid"}, "unknown option '--invalid'");
    rejected({"--console=true"}, "unknown option '--console=true'");
    rejected({"first.z64", "second.z64"}, "specify only one ROM path");
    rejected({"first.z64", "--rom=second.z64"}, "specify only one ROM path");
    rejected({"--rom=first.z64", "second.z64"}, "specify only one ROM path");
    rejected({"--rom", "first.z64", "--rom", "second.z64"}, "specify only one ROM path");
    rejected({"--rom"}, "--rom requires a path");
    rejected({"--rom="}, "--rom requires a path");
    rejected({"--data-dir", "--console"}, "--data-dir requires a path");
    rejected({"--controller-pak", ""}, "--controller-pak requires a path");
    rejected({"--log-level"}, "--log-level requires a level");
    rejected({"--log-level="}, "--log-level requires a level");
    rejected({"--log-level=loud"}, "--log-level must be error, warn, info, debug, or trace");
    rejected({"--portable", "--data-dir=Profile"}, "--portable and --data-dir cannot be used together");
    rejected({"--data-dir", "Profile", "--portable"}, "--portable and --data-dir cannot be used together");
}
