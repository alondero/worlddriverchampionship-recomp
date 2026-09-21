// RT64 owns raw RDP decoding and GPU presentation; BOSS RSP remains translated.
#define HLSL_CPU
#include "hle/rt64_application.h"
#include "hle/rt64_state.h"
#include "ultramodern/renderer_context.hpp"
#include "boss_rsp.h"
#include <cstdio>
#include <memory>

namespace {
uint8_t header[64]{}, imem[4096]{}, renderer_dmem[4096]{};
uint32_t mi{}, dpc[8]{};
void interrupts() {
    if (mi & 0x20) {
        mi &= ~0x20u;
        wdc::rsp::signal_dp_complete();
    }
}
class Renderer final : public ultramodern::renderer::RendererContext {
    std::unique_ptr<RT64::Application> app;
    uint8_t* rdram;
public:
    Renderer(uint8_t* memory, ultramodern::renderer::WindowHandle window) : rdram(memory) {
        RT64::Application::Core core{};
#ifdef _WIN32
        core.window = window.window;
#else
        core.window = window;
#endif
        core.HEADER = header;
        core.RDRAM = memory;
        core.DMEM = renderer_dmem;
        core.IMEM = imem;
        core.checkInterrupts = interrupts;
        core.MI_INTR_REG = &mi;
        core.DPC_START_REG = &dpc[0];
        core.DPC_END_REG = &dpc[1];
        core.DPC_CURRENT_REG = &dpc[2];
        core.DPC_STATUS_REG = &dpc[3];
        core.DPC_CLOCK_REG = &dpc[4];
        core.DPC_BUFBUSY_REG = &dpc[5];
        core.DPC_PIPEBUSY_REG = &dpc[6];
        core.DPC_TMEM_REG = &dpc[7];
        auto* vi = ultramodern::renderer::get_vi_regs();
#define VI(field) core.field = &vi->field
        VI(VI_STATUS_REG); VI(VI_ORIGIN_REG); VI(VI_WIDTH_REG); VI(VI_INTR_REG);
        VI(VI_V_CURRENT_LINE_REG); VI(VI_TIMING_REG); VI(VI_V_SYNC_REG);
        VI(VI_H_SYNC_REG); VI(VI_LEAP_REG); VI(VI_H_START_REG); VI(VI_V_START_REG);
        VI(VI_V_BURST_REG); VI(VI_X_SCALE_REG); VI(VI_Y_SCALE_REG);
#undef VI
        RT64::ApplicationConfiguration config;
        config.appId = "WorldDriverChampionshipRecomp";
        config.useConfigurationFile = false;
        app = std::make_unique<RT64::Application>(core, config);
        // WDC switches between 320x240 racing and 320x480 menu buffers. Both
        // represent the same 4:3 TV output, not square-pixel framebuffers.
        app->enhancementConfig.presentation.removeBlackBorders = false;
        app->userConfig.resolution = RT64::UserConfiguration::Resolution::Manual;
        app->userConfig.resolutionMultiplier = 1;
        app->userConfig.aspectRatio = RT64::UserConfiguration::AspectRatio::Original;
        app->userConfig.refreshRate = RT64::UserConfiguration::RefreshRate::Original;
#ifdef _WIN32
        app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::D3D12;
        auto result = app->setup(window.thread_id);
        chosen_api = ultramodern::renderer::GraphicsApi::D3D12;
#else
        app->userConfig.graphicsAPI = RT64::UserConfiguration::GraphicsAPI::Vulkan;
        auto result = app->setup(0);
        chosen_api = ultramodern::renderer::GraphicsApi::Vulkan;
#endif
        setup_result = static_cast<ultramodern::renderer::SetupResult>(result);
        // This pinned RT64 version incorrectly reports Success on DLL failure.
        if (result == RT64::Application::SetupResult::Success && (!app->state || !app->device))
            setup_result = ultramodern::renderer::SetupResult::DynamicLibrariesNotFound;
        std::fprintf(stderr, "[wdc] RT64 setup=%d\n", int(result));
    }
    bool valid() override { return setup_result == ultramodern::renderer::SetupResult::Success; }
    bool owns_task_interrupts() const override { return true; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return false; }
    void enable_instant_present() override {}
    void send_dl(const OSTask* task) override {
        wdc::rsp::run(rdram, task, [](void* context, uint8_t* memory, uint32_t start, uint32_t end) {
            static_cast<Renderer*>(context)->app->processDisplayLists(memory, start, end, false);
        }, this);
    }
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override {
        static uint64_t reported = ~uint64_t{};
        auto* vi = ultramodern::renderer::get_vi_regs();
        uint64_t signature = uint64_t(vi->VI_WIDTH_REG) << 32 | vi->VI_Y_SCALE_REG;
        if (reported != signature && vi->VI_H_START_REG) {
            reported = signature;
            std::fprintf(stderr, "[wdc] VI status=%x width=%u x=%x y=%x h=%x v=%x\n",
                vi->VI_STATUS_REG, vi->VI_WIDTH_REG, vi->VI_X_SCALE_REG,
                vi->VI_Y_SCALE_REG, vi->VI_H_START_REG, vi->VI_V_START_REG);
        }
        app->updateScreen();
    }
    void shutdown() override { app->end(); }
    uint32_t get_display_framerate() const override { return 60; }
    float get_resolution_scale() const override { return 1; }
};
}
std::unique_ptr<ultramodern::renderer::RendererContext> wdc_create_renderer(
    uint8_t* memory, ultramodern::renderer::WindowHandle window, bool) {
    return std::make_unique<Renderer>(memory, window);
}
