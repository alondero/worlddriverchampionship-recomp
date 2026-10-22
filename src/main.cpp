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
#ifdef WDC_PLAYER
#include <SDL.h>
#include <SDL_syswm.h>
#include "audio.h"
#include "graphics_config.h"
#include "graphics_window.h"
#include "ui/frontend.h"
std::unique_ptr<ultramodern::renderer::RendererContext> wdc_create_renderer(
    uint8_t*, ultramodern::renderer::WindowHandle, bool);
static SDL_Window* window;
static std::atomic<uint16_t> buttons{};
static std::atomic<int> stick_x{}, stick_y{};
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
        std::fprintf(stderr, "[wdc] graphics task: ucode=%08x data=%08x size=%x\n",
                     task->t.ucode, task->t.data_ptr, task->t.data_size);
        wdc::rsp::run(rdram, task, [](void*, uint8_t*, uint32_t start, uint32_t end) {
            static unsigned batches = 0;
            if (++batches < 10) std::fprintf(stderr, "[wdc] RDP output %08x..%08x\n", start, end);
        }, nullptr);
        wdc::rsp::signal_dp_complete();
    }
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override {}
    void shutdown() override {}
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1; }
};

int main(int argc, char** argv) {
#ifdef WDC_PLAYER
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER)) {
        std::fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
#endif
    const auto rom_path = argc > 1 ? std::filesystem::path(argv[1]) :
        std::filesystem::path("World Driver Championship (USA).z64");
    std::ifstream input(rom_path, std::ios::binary | std::ios::ate);
    if (!input || input.tellg() != 0x1000000) {
        std::fprintf(stderr, "Supply the 16 MiB USA ROM as the first argument.\n");
        return 1;
    }
    std::vector<uint8_t> rom(0x1000000);
    input.seekg(0);
    input.read(reinterpret_cast<char*>(rom.data()), rom.size());
    const auto hash = XXH3_64bits(rom.data(), rom.size());
    std::fprintf(stderr, "[wdc] ROM XXH3=%016llx; experimental recompiled player\n", (unsigned long long)hash);
    register_overlays();
    const auto config_path = std::filesystem::current_path() / "build" / "user-data";
    std::filesystem::create_directories(config_path);
    recomp::register_config_path(config_path);
    wdc::pak::configure(config_path / "saves" / "worlddriverchampionship.us.mpk");
#ifdef WDC_PLAYER
    wdc::graphics::load(config_path / "graphics.json");
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
        std::fprintf(stderr, "[wdc] entering translated ROM entrypoint\n");
    };
    game.thread_create_callback = [](uint8_t*, recomp_context*) {
        std::fprintf(stderr, "[wdc] guest thread started: %u\n", ++threads);
    };
    recomp::register_game(game);
    if (recomp::select_rom(rom_path, game.game_id) != recomp::RomValidationError::Good) return 1;
#ifdef WDC_PLAYER
    wdc::ui::install();
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
        bool captured = wdc::ui::captures_input();
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            // These shortcuts stay owned by WDC, rather than the framework's
            // separate graphics schema. SDL window changes happen here only.
            if (event.type == SDL_KEYDOWN && (event.key.keysym.sym == SDLK_F11 ||
                (event.key.keysym.sym == SDLK_RETURN && (event.key.keysym.mod & KMOD_ALT)))) {
                wdc::graphics::handle_event(window, event);
                continue;
            }
            if (event.type == SDL_QUIT) {
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
    };
#endif
    cfg.events_callbacks.vi_callback = [] { ++vis; };
    cfg.error_handling_callbacks.message_box = [](const char* message) { std::fprintf(stderr, "[wdc] %s\n", message); };
    cfg.input_callbacks.poll_input = [] {};
    cfg.input_callbacks.get_input = [](int port, uint16_t* buttons, float* x, float* y) {
        *buttons = 0; *x = *y = 0; return port == 0;
    };
#ifdef WDC_PLAYER
    cfg.input_callbacks.get_input = [](int port, uint16_t* out_buttons, float* x, float* y) {
        if (wdc::ui::captures_input()) { *out_buttons = 0; *x = *y = 0; return port == 0; }
        *out_buttons = buttons.load(); *x = float(stick_x.load()); *y = float(stick_y.load());
        return port == 0;
    };
#endif
    cfg.input_callbacks.get_connected_device_info = wdc::pak::device_info;
    cfg.audio_callbacks.queue_samples = [](int16_t*, size_t) {};
    cfg.audio_callbacks.get_frames_remaining = []() -> size_t { return 0; };
    cfg.audio_callbacks.set_frequency = [](uint32_t frequency) { std::fprintf(stderr, "[wdc] audio Hz=%u\n", frequency); };
#ifdef WDC_PLAYER
    cfg.audio_callbacks.set_frequency = wdc::audio::set_frequency;
    cfg.audio_callbacks.queue_samples = wdc::audio::queue_samples;
    cfg.audio_callbacks.get_frames_remaining = wdc::audio::frames_remaining;
#endif
    std::thread([] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        recomp::start_game(u8"worlddriverchampionship.us", "");
    }).detach();
    if (std::getenv("WDC_DIAGNOSTIC")
#ifndef WDC_PLAYER
        || true
#endif
    ) std::thread([] {
        std::this_thread::sleep_for(std::chrono::seconds(15));
        std::fprintf(stderr, "[wdc] diagnostic timeout: boot=%u threads=%u vis=%u graphics=%u\n",
                     (unsigned)booted.load(), threads.load(), vis.load(), graphics.load());
        std::fflush(nullptr);
        std::_Exit(2);
    }).detach();
    recomp::start(cfg);
#ifdef WDC_PLAYER
    wdc::ui::shutdown();
    return 0;
#else
    return 2;
#endif
}
