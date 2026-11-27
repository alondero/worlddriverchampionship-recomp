// Bounded startup diagnostic for the real translated USA game.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include "librecomp/game.hpp"
#include "ultramodern/events.hpp"
#include "ultramodern/error_handling.hpp"
#include "xxHash/xxh3.h"
#include "boss_rsp.h"
#include "controller_pak.h"
#include "game_warp.h"
#include "game_scenario.h"
#include "launch_options.h"
#include "savestate.h"
#ifdef WDC_PLAYER
#include <SDL.h>
#include <SDL_syswm.h>
#include "audio.h"
#include "graphics_config.h"
#include "graphics_window.h"
#include "render_statistics.h"
#include "ui/frontend.h"
#ifdef WDC_ENABLE_FRONTEND
#include "ui/input.h"
#endif
// SDL_syswm.h includes X11/X.h on Linux, and X11 defines these names as
// macros. That turns scoped enumerators such as SetupResult::Success into
// SetupResult::0. Undefine them after the headers, mirroring the same block
// in RT64's rt64_application_window.h.
#undef None
#undef Status
#undef LockMask
#undef ControlMask
#undef Success
#undef Always
std::unique_ptr<ultramodern::renderer::RendererContext> wdc_create_renderer(
    uint8_t*, ultramodern::renderer::WindowHandle, bool);
static SDL_Window* window;
#ifndef WDC_ENABLE_FRONTEND
static std::atomic<uint16_t> buttons{};
static std::atomic<int> stick_x{}, stick_y{};
#endif
#endif

extern "C" void recomp_entrypoint(uint8_t*, recomp_context*);
void register_overlays();
extern "C" [[noreturn]] void wdc_unimplemented(const char*);
static std::atomic<unsigned> threads{}, vis{}, graphics{};
static std::atomic<bool> booted{};

class BootRenderer final : public ultramodern::renderer::RendererContext {
    uint8_t* rdram;
public:
    explicit BootRenderer(uint8_t* memory) : rdram(memory) { setup_result = ultramodern::renderer::SetupResult::Success; }
    bool owns_task_interrupts() const override { return true; }
    bool valid() override { return true; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return false; }
    void enable_instant_present() override {}
    void send_dl(const OSTask* task) override {
        ++graphics;
        wdc::scenario::graphics_task();
        wdc::log(wdc::LogLevel::Debug, "[wdc] graphics task: ucode=%08x data=%08x size=%x\n",
                     task->t.ucode, task->t.data_ptr, task->t.data_size);
        wdc::rsp::run(rdram, task, [](void*, uint8_t*, uint32_t start, uint32_t end) {
            static unsigned batches = 0;
            if (++batches < 10) wdc::log(wdc::LogLevel::Trace, "[wdc] RDP output %08x..%08x\n", start, end);
        }, nullptr);
        wdc::rsp::signal_dp_complete();
    }
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override {}
    void shutdown() override {}
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1; }
};

namespace {
int launch_message(const char* message, bool error) {
    FILE* stream = error ? stderr : stdout;
#if defined(_WIN32) && defined(WDC_PLAYER)
    wdc::open_launch_console(false);
    if (std::fputs(message, stream) < 0 || std::fflush(stream) != 0)
        wdc::show_launch_message(message, error);
#else
    std::fputs(message, stream);
#endif
    return error ? 2 : 0;
}

int run_player(int argc, char** argv) {
    wdc::LaunchOptions launch;
    if (const auto error = wdc::parse_launch_options(argc, argv, launch); !error.empty())
        return launch_message((error + "\nRun with --help for usage.\n").c_str(), true);
    if (launch.action == wdc::LaunchAction::Help) return launch_message(wdc::launch_help(), false);
    if (launch.action == wdc::LaunchAction::Version)
        return launch_message("worlddriverchampionship_modern " WDC_VERSION "\n", false);

    std::filesystem::path executable_dir;
#if defined(WDC_PLAYER) && !defined(__ANDROID__)
    if (char* base = SDL_GetBasePath()) {
        executable_dir = std::filesystem::path(reinterpret_cast<const char8_t*>(base));
        SDL_free(base);
    }
#endif
    if (launch.portable && executable_dir.empty())
        return launch_message("--portable requires a desktop player with a known executable directory.\n", true);
#ifndef WDC_PLAYER
    if (launch.fullscreen.has_value())
        return launch_message("Window mode requires the graphical player.\n", true);
#endif

    std::error_code config_error;
    auto config_path = std::filesystem::absolute(
        launch.data_dir.empty() ? (launch.portable ? executable_dir / "user-data" : "build/user-data")
                                : launch.data_dir, config_error);
    if (config_error)
        return launch_message(("Cannot resolve the data directory: " + config_error.message() + "\n").c_str(), true);
#if defined(_WIN32) && defined(WDC_PLAYER)
    if (launch.console) wdc::open_launch_console(true);
    wdc::open_launch_log(config_path);
#endif
#ifdef WDC_PLAYER
    wdc::log_level = launch.log_level.value_or(wdc::LogLevel::Info);
#else
    wdc::log_level = launch.log_level.value_or(wdc::LogLevel::Trace);
#endif
    if (!wdc::warp::configure_from_environment() || !wdc::scenario::configure()) return 1;
#ifdef WDC_PLAYER
    SDL_SetMainReady();
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS4_RUMBLE, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_PS5_RUMBLE, "1");
#ifdef _WIN32
    // Zelda64Recomp and BanjoRecomp avoid DirectSound's queued-audio issues.
    // Respect an explicit SDL_AUDIODRIVER, including test/dummy devices.
    SDL_setenv("SDL_AUDIODRIVER", "wasapi", 0);
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER)) {
        wdc::log(wdc::LogLevel::Error, "SDL: %s\n", SDL_GetError());
        return 1;
    }
#endif
    auto rom_path = launch.rom;
    if (rom_path.empty()) {
        rom_path = "World Driver Championship (USA).z64";
        std::error_code error;
        if (!executable_dir.empty() && !std::filesystem::exists(rom_path, error) && !error)
            rom_path = executable_dir / rom_path;
    }
    std::ifstream input(rom_path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != 0x1000000) {
        wdc::log(wdc::LogLevel::Error, "Supply the 16 MiB USA ROM as a positional path or with --rom PATH.\n");
        return 1;
    }
    std::vector<uint8_t> rom(0x1000000);
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(rom.data()), rom.size())) {
        wdc::log(wdc::LogLevel::Error, "[wdc] Could not read the complete ROM.\n");
        return 1;
    }
    const auto hash = XXH3_64bits(rom.data(), rom.size());
    wdc::log(wdc::LogLevel::Info, "[wdc] ROM XXH3=%016llx; experimental recompiled player\n", (unsigned long long)hash);
    register_overlays();
    // The BOSS device posts OS events through this sink. The offline replay
    // tool installs none, so a replay cannot signal the game.
    wdc::rsp::set_game_event_sink(&wdc::rsp::post_game_event);
    std::filesystem::create_directories(config_path, config_error);
    if (config_error) {
        wdc::log(wdc::LogLevel::Error, "[wdc] Cannot create the data directory: %s\n", config_error.message().c_str());
        return 1;
    }
    recomp::register_config_path(config_path);
    wdc::pak::configure(launch.controller_pak.empty()
        ? config_path / "saves" / "worlddriverchampionship.us.mpk" : launch.controller_pak);
    if (!wdc::state::configure(config_path / "wdc_savestate.wstate")) return 1;
#ifdef WDC_PLAYER
    wdc::graphics::load(config_path / "graphics.json", launch.fullscreen);
#endif
    recomp::GameEntry game{};
    game.rom_hash = 0x1BF1E67DCFA21FB8ULL;
    game.internal_name = std::string(reinterpret_cast<const char*>(rom.data() + 0x20), 20);
    game.display_name = "World Driver Championship";
    game.game_id = u8"worlddriverchampionship.us";
    game.is_enabled = true;
    game.entrypoint_address = (int32_t)0x80000400;
    game.entrypoint = recomp_entrypoint;
    game.on_init_callback = [](uint8_t*, recomp_context*) {
        booted = true;
        wdc::log(wdc::LogLevel::Info, "[wdc] entering translated ROM entrypoint\n");
    };
    game.thread_create_callback = [](uint8_t*, recomp_context*) {
        wdc::log(wdc::LogLevel::Debug, "[wdc] guest thread started: %u\n", ++threads);
    };
    recomp::register_game(game);
    if (recomp::select_rom(rom_path, game.game_id) != recomp::RomValidationError::Good) {
        wdc::log(wdc::LogLevel::Error, "[wdc] ROM validation failed. Check the USA ROM and data directory.\n");
        return 1;
    }
#ifdef WDC_PLAYER
    wdc::ui::install();
#ifdef WDC_ENABLE_FRONTEND
    wdc::input::initialize_controllers(executable_dir, config_path);
#endif
#endif
    recomp::Configuration cfg{};
    cfg.project_version = {0, 1, 0};
    cfg.rsp_callbacks.get_rsp_microcode = [](const OSTask*) -> RspUcodeFunc* {
        wdc_unimplemented("unidentified RSP task");
    };
    cfg.renderer_callbacks.create_render_context = [](uint8_t* memory, ultramodern::renderer::WindowHandle, bool)
        -> std::unique_ptr<ultramodern::renderer::RendererContext> { return std::make_unique<BootRenderer>(memory); };
    cfg.gfx_callbacks.create_window = [](void*) { return ultramodern::renderer::WindowHandle{}; };
    cfg.gfx_callbacks.update_gfx = [](void*) {};
#ifdef WDC_PLAYER
    cfg.renderer_callbacks.create_render_context = wdc_create_renderer;
    cfg.gfx_callbacks.create_window = [](void*) {
        window = SDL_CreateWindow("World Driver Championship: Recompiled", SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_RESIZABLE);
        if (!window) wdc_unimplemented(SDL_GetError());
        wdc::ui::set_window(window);
        wdc::graphics::attach_window(window);
#ifdef _WIN32
        SDL_SysWMinfo info{};
        SDL_VERSION(&info.version);
        if (!SDL_GetWindowWMInfo(window, &info)) wdc_unimplemented(SDL_GetError());
        return ultramodern::renderer::WindowHandle{info.info.win.window, GetCurrentThreadId()};
#else
        return window;
#endif
    };
    cfg.gfx_callbacks.update_gfx = [](void*) {
        wdc::audio::pump();
        wdc::ui::pump();
#ifdef WDC_ENABLE_FRONTEND
        std::lock_guard input_lock(wdc::input::mutex());
#endif
        bool captured = wdc::ui::captures_input();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_WINDOWEVENT && event.window.windowID == SDL_GetWindowID(window) &&
                (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
                 event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) && wdc::render::diagnostics()) {
                wdc::log(wdc::LogLevel::Info, "[wdc:render] window focus=%s\n",
                    event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED ? "gained" : "lost");
            }
            if (event.type == SDL_KEYDOWN && !event.key.repeat && !captured &&
                (event.key.keysym.sym == SDLK_F7 || event.key.keysym.sym == SDLK_F8)) {
                if (event.key.keysym.sym == SDLK_F7) wdc::state::request_save();
                else wdc::state::request_load();
                continue;
            }
            if (event.type == SDL_KEYDOWN && !event.key.repeat &&
                (event.key.keysym.mod & KMOD_CTRL) && (event.key.keysym.mod & KMOD_SHIFT)) {
                const auto key = event.key.keysym.sym;
                if (key == SDLK_r) { wdc::warp::repeat(); continue; }
                if (key >= SDLK_0 && key <= SDLK_9) {
                    const int venue = key == SDLK_0 ? 9 : int(key - SDLK_1);
                    wdc::warp::request({venue * 6 + 1});
                    continue;
                }
            }
            // These shortcuts stay owned by WDC, rather than the framework's
            // separate graphics schema. SDL window changes happen here only.
            if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_F11 ||
                (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)))) {
                wdc::graphics::handle_event(window, event);
                continue;
            }
            if (event.type == SDL_QUIT) {
#ifdef WDC_ENABLE_FRONTEND
                wdc::input::stop_rumble();
#endif
                std::fflush(nullptr);
                std::_Exit(0);
            }
            if (event.type == SDL_DROPFILE || event.type == SDL_DROPTEXT) {
                SDL_free(event.drop.file);
                continue;
            }
            if (wdc::ui::handle_event(event)) { captured = true; continue; }
            wdc::graphics::handle_event(window, event);
#ifndef WDC_ENABLE_FRONTEND
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                std::fflush(nullptr);
                std::_Exit(0);
            }
#endif
        }
#ifdef WDC_ENABLE_FRONTEND
        if (!wdc::scenario::enabled()) wdc::input::sample(captured);
#else
        auto* keys = SDL_GetKeyboardState(nullptr);
        uint16_t next_buttons = (keys[SDL_SCANCODE_RETURN] && !(SDL_GetModState() & KMOD_ALT) ? 0x1000 : 0) | (keys[SDL_SCANCODE_X] ? 0x8000 : 0) |
                  (keys[SDL_SCANCODE_Z] ? 0x4000 : 0) | (keys[SDL_SCANCODE_SPACE] ? 0x2000 : 0) |
                  (keys[SDL_SCANCODE_RSHIFT] ? 0x10 : 0);
        int next_x = (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) - (keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_A]);
        int next_y = (keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_W]) - (keys[SDL_SCANCODE_DOWN] || keys[SDL_SCANCODE_S]);
        // Closing with a held menu key must not press Start/A or steer the
        // guest. Resume after every mapped gameplay key has been released.
        static bool wait_for_release = false;
        if (captured || wdc::ui::captures_input()) wait_for_release = true;
        const bool held = keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_X] || keys[SDL_SCANCODE_Z] ||
            keys[SDL_SCANCODE_SPACE] || keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_RIGHT] ||
            keys[SDL_SCANCODE_LEFT] || keys[SDL_SCANCODE_UP] || keys[SDL_SCANCODE_DOWN] ||
            keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_S];
        if (!captured && !wdc::ui::captures_input() && !held) wait_for_release = false;
        if (wait_for_release) { next_buttons = 0; next_x = 0; next_y = 0; }
        buttons = next_buttons; stick_x = next_x; stick_y = next_y;
#endif
    };
#endif
    cfg.events_callbacks.vi_callback = [] {
        const auto vi = ++vis;
        wdc::warp::vi_tick(vi);
        wdc::scenario::vi_tick(vi);
    };
    cfg.error_handling_callbacks.message_box = [](const char* message) { wdc::log(wdc::LogLevel::Error, "[wdc] %s\n", message); };
    cfg.input_callbacks.poll_input = [] {};
    cfg.input_callbacks.get_input = [](int port, uint16_t* buttons, float* x, float* y) {
        *buttons = 0; *x = *y = 0; return port == 0;
    };
#ifdef WDC_PLAYER
#ifdef WDC_ENABLE_FRONTEND
    cfg.input_callbacks.get_input = wdc::input::read;
#else
    cfg.input_callbacks.get_input = [](int port, uint16_t* out_buttons, float* x, float* y) {
        if (wdc::ui::captures_input()) { *out_buttons = 0; *x = *y = 0; return port == 0; }
        if (port != 0) { *out_buttons = 0; *x = *y = 0; return false; }
        *out_buttons = buttons.load(); *x = float(stick_x.load()); *y = float(stick_y.load());
        return port == 0;
    };
#endif
#endif
    if (wdc::scenario::enabled()) cfg.input_callbacks.get_input = wdc::scenario::read_input;
    cfg.input_callbacks.get_connected_device_info = wdc::pak::device_info;
    cfg.audio_callbacks.queue_samples = [](int16_t*, size_t) {};
    cfg.audio_callbacks.get_frames_remaining = []() -> size_t { return 0; };
    cfg.audio_callbacks.set_frequency = [](uint32_t frequency) { wdc::log(wdc::LogLevel::Info, "[wdc] audio Hz=%u\n", frequency); };
#ifdef WDC_PLAYER
    cfg.audio_callbacks.set_frequency = wdc::audio::set_frequency;
    cfg.audio_callbacks.queue_samples = wdc::audio::queue_samples;
    cfg.audio_callbacks.get_frames_remaining = wdc::audio::frames_remaining;
#endif
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        recomp::start_game(u8"worlddriverchampionship.us", "");
    }).detach();
    if (!wdc::scenario::enabled() && (std::getenv("WDC_DIAGNOSTIC")
#ifndef WDC_PLAYER
        || true
#endif
    )) std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        wdc::log(wdc::LogLevel::Error, "[wdc] diagnostic timeout: boot=%u threads=%u vis=%u graphics=%u\n",
                     (unsigned)booted.load(), threads.load(), vis.load(), graphics.load());
        std::fflush(nullptr);
        std::_Exit(2);
    }).detach();
    recomp::start(cfg);
#ifdef WDC_PLAYER
#ifdef WDC_ENABLE_FRONTEND
    wdc::input::stop_rumble();
#endif
    wdc::ui::shutdown();
    return 0;
#else
    return 2;
#endif
}
}

#if defined(_WIN32) && defined(WDC_PLAYER)
int wdc::application_main(int argc, char** argv) { return run_player(argc, argv); }
#else
int main(int argc, char** argv) { return run_player(argc, argv); }
#endif
