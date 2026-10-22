// RT64 owns raw RDP decoding and GPU presentation; BOSS RSP remains translated.
#define HLSL_CPU
#include "hle/rt64_application.h"
#include "hle/rt64_state.h"
#include "hle/rt64_rdp.h"
#include "render/rt64_shader_library.h"
#include "ultramodern/renderer_context.hpp"
#include "boss_rsp.h"
#include "graphics_rt64.h"
#include "game_camera.h"
#include "game_hud.h"
#include <algorithm>
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
    wdc::graphics::Settings settings;
    float aspect_target = 0.0f;
    wdc::hud::Stream hud_stream;
    bool hud_active = false;
    uint32_t hud_left = 0, hud_right = 0;
    RT64::ExtendedAlignment saved_rect;
    void end_hud() {
        if (!hud_active) return;
        app->state->rdp->setRectAlign(saved_rect);
        app->state->rdp->popScissor();
        hud_active = false;
    }
    void hud_marker(uint32_t payload) {
        end_hud();
        if (payload == wdc::hud::end_marker) return;
        auto& rdp = *app->state->rdp;
        hud_left = payload >> 16; hud_right = payload & 0xFFFF;
        const unsigned width = rdp.colorImage.width;
        if (!width || hud_left >= hud_right || hud_right > width) return;
        saved_rect = rdp.extended.global.rect;
        const auto scissor = rdp.scissorRectStack[rdp.scissorStackSize - 1];
        const auto mode = rdp.scissorModeStack[rdp.scissorStackSize - 1];
        rdp.pushScissor();
        RT64::ExtendedAlignment align;
        align.leftOrigin = hud_left * G_EX_ORIGIN_RIGHT / width;
        align.rightOrigin = hud_right * G_EX_ORIGIN_RIGHT / width;
        // Origins supply the viewport edges; retain the ROM's vertical clip.
        rdp.setScissor(mode, 0, scissor.uly, 0, scissor.lry, align);
        hud_active = true;
    }
    void send_rdp(uint8_t* memory, uint32_t start, uint32_t end) {
        uint32_t run = start;
        auto submit = [&](uint32_t until) {
            if (run < until) app->processDisplayLists(memory, run, until, false);
            run = until;
        };
        // BOSS supplies aligned, word-swapped RDRAM or DMEM synchronously on
        // the graphics worker (range checked by boss_rsp.cpp). Read native u32
        // words, never retain pointers or mutate the guest command stream.
        for (uint32_t pos = start; pos < end; pos += 8) {
            const auto* words = reinterpret_cast<const uint32_t*>(memory + pos);
            const bool boundary = hud_stream.remaining == 0;
            if (hud_stream.consume(words[0])) {
                submit(pos);
                hud_marker(words[1]);
                run = pos + 8;
            } else if (boundary && hud_active &&
                       (((words[0] >> 24) & 63) == 36 || ((words[0] >> 24) & 63) == 37)) {
                submit(pos);
                const float midpoint = float(hud_left + hud_right) * 2.0f;
                const float rect_center = float(((words[0] >> 12) & 4095) + ((words[1] >> 12) & 4095)) / 2.0f;
                const unsigned anchor = rect_center < midpoint ? hud_left : hud_right;
                RT64::ExtendedAlignment align;
                align.leftOrigin = align.rightOrigin = anchor * G_EX_ORIGIN_RIGHT / app->state->rdp->colorImage.width;
                align.leftOffset = align.rightOffset = -int(anchor * 4);
                app->state->rdp->setRectAlign(align);
            }
        }
        submit(end);
    }
    void sync_settings() {
        const auto next = wdc::graphics::snapshot();
        const auto size = app->core.decodeVI().fbSize();
        const float source = size.y > 0 ? float(size.x) / float(size.y) : 4.0f / 3.0f;
        const float scale = wdc::camera::width_scale();
        const float target = source * scale;
        if (next == settings && target == aspect_target) return;
        const auto old_samples = app->userConfig.antialiasing;
        const auto color_format = app->userConfig.internalColorFormat;
        wdc::graphics::configure(app->userConfig, next, scale, source);
        // RT64 allocates color-format-dependent shaders during setup only.
        app->userConfig.internalColorFormat = color_format;
        const auto counts = app->device->getSampleCountsSupported(RT64::RenderTarget::colorBufferFormat(app->shaderLibrary->usesHDR)) &
            app->device->getSampleCountsSupported(RT64::RenderTarget::depthBufferFormat());
        if (!(counts & app->userConfig.msaaSampleCount())) {
            app->userConfig.antialiasing = RT64::UserConfiguration::Antialiasing::None;
            std::fprintf(stderr, "[wdc] requested MSAA unsupported; using no MSAA\n");
        }
        app->updateUserConfig(true);
        if (old_samples != app->userConfig.antialiasing) app->updateMultisampling();
        settings = next;
        aspect_target = target;
    }
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
        settings = wdc::graphics::snapshot();
        wdc::graphics::configure(app->userConfig, settings);
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
        if (valid() && app->userConfig.msaaSampleCount() != unsigned(settings.samples))
            std::fprintf(stderr, "[wdc] requested MSAA unsupported; using no MSAA\n");
        std::fprintf(stderr, "[wdc] RT64 setup=%d\n", int(result));
    }
    bool valid() override { return setup_result == ultramodern::renderer::SetupResult::Success; }
    bool owns_task_interrupts() const override { return true; }
    bool update_config(const ultramodern::renderer::GraphicsConfig&, const ultramodern::renderer::GraphicsConfig&) override { return false; }
    void enable_instant_present() override {}
    void send_dl(const OSTask* task) override {
        sync_settings();
        wdc::rsp::run(rdram, task, [](void* context, uint8_t* memory, uint32_t start, uint32_t end) {
            static_cast<Renderer*>(context)->send_rdp(memory, start, end);
        }, this);
        end_hud(); // Fail closed if a task ever omits its closing marker.
    }
    void send_dummy_workload(uint32_t) override {}
    void update_screen() override {
        sync_settings();
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
    uint32_t get_display_framerate() const override {
        std::lock_guard lock(app->sharedQueueResources->configurationMutex);
        const auto rate = app->sharedQueueResources->swapChainRate;
        return rate ? rate : 60;
    }
    float get_resolution_scale() const override {
        // RT64's actual VI-derived scale handles both 240-line racing and
        // 480-line menus; a hard-coded reference height would misreport menus.
        std::lock_guard lock(app->sharedQueueResources->configurationMutex);
        return std::max(float(app->sharedQueueResources->resolutionScale.y), 1.0f);
    }
};
}
std::unique_ptr<ultramodern::renderer::RendererContext> wdc_create_renderer(
    uint8_t* memory, ultramodern::renderer::WindowHandle window, bool) {
    return std::make_unique<Renderer>(memory, window);
}
