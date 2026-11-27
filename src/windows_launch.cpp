#include "launch_options.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <io.h>
#include <chrono>
#include <utility>
#include <vector>

namespace {
bool has_handle(DWORD stream) {
    const HANDLE handle = GetStdHandle(stream);
    return handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
}

bool reopen(FILE* stream, const wchar_t* path, const wchar_t* mode) {
    FILE* reopened = nullptr;
    return _wfreopen_s(&reopened, path, mode, stream) == 0;
}
}

void wdc::open_launch_console(bool allow_new_window) {
    const bool has_input = has_handle(STD_INPUT_HANDLE);
    const bool has_output = has_handle(STD_OUTPUT_HANDLE);
    const bool has_error = has_handle(STD_ERROR_HANDLE);
    if (has_output && has_error) return;

    if (!GetConsoleCP() && !AttachConsole(ATTACH_PARENT_PROCESS) &&
        (!allow_new_window || has_output || has_error || !AllocConsole())) return;

    // Rebind only missing streams so redirected output keeps its original destination.
    if (!has_input) reopen(stdin, L"CONIN$", L"r");
    if (!has_output) reopen(stdout, L"CONOUT$", L"w");
    if (!has_error) reopen(stderr, L"CONOUT$", L"w");
}

void wdc::open_launch_log(const std::filesystem::path& data_dir) {
    const bool has_output = has_handle(STD_OUTPUT_HANDLE);
    const bool has_error = has_handle(STD_ERROR_HANDLE);
    if (has_error) return;

    const auto directory = data_dir / "logs";
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    const auto timestamp = std::chrono::system_clock::now().time_since_epoch();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timestamp).count();
    const auto path = directory / ("wdc-" + std::to_string(seconds) + "-" +
                                   std::to_string(GetCurrentProcessId()) + ".log");
    if (error || !reopen(stderr, path.c_str(), L"a")) {
        show_launch_message("Could not open the player log. Check that the data directory is writable.", true);
        return;
    }
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    // Give stdout a CRT descriptor, then share the log's file position.
    if (!has_output && reopen(stdout, L"NUL", L"w") &&
        _dup2(_fileno(stderr), _fileno(stdout)) == 0) std::setvbuf(stdout, nullptr, _IONBF, 0);
}

void wdc::show_launch_message(const char* message, bool error) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
    std::wstring wide(length, L'\0');
    if (length && MultiByteToWideChar(CP_UTF8, 0, message, -1, wide.data(), length))
        MessageBoxW(nullptr, wide.c_str(), L"World Driver Championship: Recompiled",
                    MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    int argc = 0;
    auto** wide_args = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!wide_args) return 2;

    std::vector<std::string> arguments;
    arguments.reserve(argc);
    for (int i = 0; i < argc; ++i) {
        const int length = WideCharToMultiByte(CP_UTF8, 0, wide_args[i], -1, nullptr, 0, nullptr, nullptr);
        std::string argument(length, '\0');
        if (!length || !WideCharToMultiByte(CP_UTF8, 0, wide_args[i], -1,
                                          argument.data(), length, nullptr, nullptr)) {
            LocalFree(wide_args);
            return 2;
        }
        argument.pop_back();
        arguments.push_back(std::move(argument));
    }
    LocalFree(wide_args);

    std::vector<char*> argv;
    argv.reserve(argc + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    return wdc::application_main(argc, argv.data());
}
