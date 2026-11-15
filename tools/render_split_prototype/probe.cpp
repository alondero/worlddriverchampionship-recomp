// THROWAWAY native experiment. Linked only by run.py, never into the player.
// Question: can the race draw root run again without consuming simulation or
// sound events? This intentionally retains the original VI/swap/audio clock.
#include "recomp.h"
#include "boss_rsp.h"
#include "context.h"
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace ultramodern { void wait_for_external_message_timed(uint8_t*, uint32_t); }
#ifdef WDC_PROTOTYPE_PRESENT
extern "C" void prototype_set_present(bool);
#else
static void prototype_set_present(bool) {}
#endif

namespace {
bool extra = false;
bool one_vi = false;
bool direct_present = false, inside_swap = false;
unsigned skipped_vi_waits = 0;
std::atomic_uint prototype_vi_ticks{0};
unsigned skipped_publish = 0, skipped_consume = 0;
unsigned skipped_audio_build = 0, skipped_audio_retire = 0, preserved_flare_history = 0;
unsigned race_frame = 0;
unsigned draw_number = 0;
struct Label { unsigned frame; unsigned draw; };
std::mutex label_mutex;
std::mutex output_mutex;
std::deque<Label> labels;
const char* mode() {
    const char* value = std::getenv("WDC_RENDER_PROTOTYPE_MODE");
    return value ? value : "raw";
}
bool gated() {
    return std::strcmp(mode(), "raw") != 0;
}
bool render_only() { return std::strcmp(mode(), "render-only") == 0 || std::strcmp(mode(), "present120") == 0; }
uint32_t word(uint8_t* rdram, uint32_t address) {
    return uint32_t(MEM_W(0, int32_t(address)));
}
}
extern "C" {
void prototype_vi_observed() { ++prototype_vi_ticks; }
void prototype_present_observed(uint32_t origin) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    std::lock_guard lock(output_mutex);
    std::fprintf(stderr, "[prototype-present] {\"us\":%lld,\"origin\":%u,\"vi_ticks\":%u}\n",
        (long long)std::chrono::duration_cast<std::chrono::microseconds>(now).count(), origin, prototype_vi_ticks.load());
}
void __real_func_800013DC(uint8_t*, recomp_context*);
void __wrap_func_800013DC(uint8_t* rdram, recomp_context* ctx) {
    inside_swap = direct_present;
    __real_func_800013DC(rdram, ctx);
    inside_swap = false;
}
void __real_osRecvMesg_recomp(uint8_t*, recomp_context*);
void __wrap_osRecvMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    // Only the swap routine's VI queue, not SP_BREAK, DP completion, audio
    // or the input worker. The ROM supplies a null result pointer here.
    if (inside_swap && uint32_t(ctx->r4) == 0x800AAA28) {
        if (ctx->r5 != 0) std::_Exit(7);
        ++skipped_vi_waits;
        ctx->r2 = 0;
        return;
    }
    __real_osRecvMesg_recomp(rdram, ctx);
}
void __real_osViSwapBuffer_recomp(uint8_t*, recomp_context*);
void __wrap_osViSwapBuffer_recomp(uint8_t* rdram, recomp_context* ctx) {
    if (inside_swap) {
        // Service external messages while pacing: this may schedule the
        // input worker, but never consumes simulation samples in this thread.
        static auto deadline = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now > deadline + std::chrono::milliseconds(34)) deadline = now;
        while (now < deadline) {
            ultramodern::wait_for_external_message_timed(rdram, 1);
            now = std::chrono::steady_clock::now();
        }
        deadline += std::chrono::nanoseconds(1000000000 / 120);
    }
    __real_osViSwapBuffer_recomp(rdram, ctx);
}
void __real_func_80043274(uint8_t*, recomp_context*);
void __wrap_func_80043274(uint8_t* rdram, recomp_context* ctx) {
    // 24FCC passes its current command cursor in a0 and stores v0 back to
    // ED84. On a presentation-only draw append no audio commands: identity
    // cursor result, leaving audio buffers, voice progression and DMA alone.
    if (extra && render_only()) { ++skipped_audio_build; ctx->r2 = ctx->r4; return; }
    __real_func_80043274(rdram, ctx);
}
void __real_func_80042FA8(uint8_t*, recomp_context*);
void __wrap_func_80042FA8(uint8_t* rdram, recomp_context* ctx) {
    if (extra && render_only()) { ++skipped_audio_retire; return; }
    __real_func_80042FA8(rdram, ctx);
}
void __real_func_8002A3A8(uint8_t*, recomp_context*);
void __wrap_func_8002A3A8(uint8_t* rdram, recomp_context* ctx) {
    if (!extra || !render_only()) { __real_func_8002A3A8(rdram, ctx); return; }
    // Game-thread lens-flare object, supplied as a0 by 10ECC. Four u32
    // screen-position history words +10..1C are shifted at 2A410..2A420 or
    // 2A5E4..2A5F0. Preserve them around extra emission; the emitted arena
    // packets and object's +6C/+70 render pointers retain their normal lifetime
    // (2A280/2A284 stores the freshly allocated packet address into both).
    // Read/write word-swapped aligned RDRAM; validate the complete object
    // before access. No scheduler yield occurs between return and restore.
    const auto object = uint32_t(ctx->r4);
    if (object < 0x80000000 || object > 0x807FFF84 || (object & 3)) std::_Exit(6);
    std::array<uint32_t, 4> history;
    for (unsigned i = 0; i < 4; ++i) history[i] = word(rdram, object + 0x10 + i * 4);
    __real_func_8002A3A8(rdram, ctx);
    for (unsigned i = 0; i < 4; ++i) MEM_W(0, int32_t(object + 0x10 + i * 4)) = history[i];
    ++preserved_flare_history;
}
void __real_wdc_projection_normalize(uint8_t*, recomp_context*);
void __wrap_wdc_projection_normalize(uint8_t* rdram, recomp_context* ctx) {
    __real_wdc_projection_normalize(rdram, ctx);
    if (race_frame < 60) return;
    // Game-thread read, after the projection builder: 16 float32 bit patterns
    // at fixed aligned RDRAM 800B2A80..800B2ABF, word-swapped via MEM_W.
    // Projection scratch belongs to this view pass; retain only host values.
    std::lock_guard lock(output_mutex);
    std::fprintf(stderr, "[prototype-projection] {\"frame\":%u,\"draw\":%u,\"matrix\":[", race_frame, draw_number);
    for (unsigned i = 0; i < 16; ++i) std::fprintf(stderr, "%s%u", i ? "," : "", word(rdram, 0x800B2A80 + i * 4));
    std::fprintf(stderr, "]}\n");
}
void __real_func_8000C560(uint8_t*, recomp_context*);
void __real_func_8001F9CC(uint8_t*, recomp_context*);
void __real_func_800214C8(uint8_t*, recomp_context*);
void func_8000A30C(uint8_t*, recomp_context*);
uint32_t __real_wdc_framerate_preswap();
uint32_t __wrap_wdc_framerate_preswap() {
    // Reuse only the existing one-VI presentation path. The input worker's
    // divider and fixed tick pair remain stock 30 Hz. One normal + one extra
    // draw now fit into two VIs; this is still bounded by the runtime's 60 Hz VI.
    if (one_vi) return 1;
    return __real_wdc_framerate_preswap();
}
void __real_osSpTaskStartGo_recomp(uint8_t*, recomp_context*);
void __wrap_osSpTaskStartGo_recomp(uint8_t* rdram, recomp_context* ctx) {
    {
        std::lock_guard lock(label_mutex);
        labels.push_back({race_frame, draw_number});
    }
    __real_osSpTaskStartGo_recomp(rdram, ctx);
}
void original_rsp(uint8_t*, const OSTask*, wdc::rsp::RdpSink, void*)
    asm("__real__ZN3wdc3rsp3runEPhPK6OSTaskPFvPvS1_jjES5_");
void wrapped_rsp(uint8_t*, const OSTask*, wdc::rsp::RdpSink, void*)
    asm("__wrap__ZN3wdc3rsp3runEPhPK6OSTaskPFvPvS1_jjES5_");
void wrapped_rsp(uint8_t* rdram, const OSTask* task, wdc::rsp::RdpSink sink, void* context) {
    Label label{};
    {
        std::lock_guard lock(label_mutex);
        if (labels.empty()) { std::fprintf(stderr, "[prototype-error] unlabelled task\n"); std::_Exit(4); }
        label = labels.front(); labels.pop_front();
    }
    struct Collector {
        wdc::rsp::RdpSink sink;
        void* context;
        std::vector<uint32_t> words;
    } capture{sink, context, {}};
    original_rsp(rdram, task, [](void* opaque, uint8_t* memory, uint32_t begin, uint32_t end) {
        auto& c = *static_cast<Collector*>(opaque);
        // The BOSS bridge validates these aligned word-swapped ranges. Copy
        // synchronously before the sink returns; no guest pointer survives.
        for (auto at = begin; at < end; at += 4) {
            uint32_t value;
            std::memcpy(&value, memory + at, 4);
            c.words.push_back(value);
        }
        c.sink(c.context, memory, begin, end);
    }, &capture);
    if (label.frame < 60) return;
    uint64_t hash = 14695981039346656037ull;
    unsigned triangles = 0;
    bool complete = true;
    std::vector<uint32_t> headers;
    for (size_t i = 0; i < capture.words.size();) {
        const unsigned op = (capture.words[i] >> 24) & 63;
        const bool triangle = op >= 8 && op <= 15;
        const unsigned length = triangle ? 8 + ((op & 4) ? 16 : 0) +
            ((op & 2) ? 16 : 0) + ((op & 1) ? 4 : 0) : (op == 0x24 || op == 0x25 ? 4 : 2);
        if (i + length > capture.words.size()) { complete = false; break; }
        if (triangle) {
            ++triangles;
            // Ordered triangle headers, including opcode/tile/level fields.
            // Ignore shading/texture coefficients and framebuffer addresses.
            for (unsigned j = 0; j < 8; ++j) {
                headers.push_back(capture.words[i + j]);
                hash ^= capture.words[i + j]; hash *= 1099511628211ull;
            }
        }
        i += length;
    }
    std::lock_guard output_lock(output_mutex);
    // Compare owned vectors, not just hashes. BOSS tasks execute serially on
    // the graphics worker; the mutex also protects this bounded reference.
    static unsigned normal_frame = 0;
    static std::vector<uint32_t> normal_headers;
    if (!label.draw) { normal_frame = label.frame; normal_headers = headers; }
    const bool same = normal_frame == label.frame && headers == normal_headers;
    std::fprintf(stderr, "[prototype-rdp] {\"frame\":%u,\"draw\":%u,\"extra\":%s,\"triangles\":%u,"
        "\"triangle_header_hash\":\"%016llx\",\"complete\":%s,\"same_headers_as_normal\":%s}\n", label.frame,
        label.draw, label.draw ? "true" : "false", triangles, (unsigned long long)hash, complete ? "true" : "false", same ? "true" : "false");
}
void __wrap_func_8001F9CC(uint8_t* rdram, recomp_context* ctx) {
    if (extra && gated()) { ++skipped_publish; return; }
    __real_func_8001F9CC(rdram, ctx);
}
void __wrap_func_800214C8(uint8_t* rdram, recomp_context* ctx) {
    if (extra && gated()) { ++skipped_consume; return; }
    __real_func_800214C8(rdram, ctx);
}

// Called on the guest dispatcher/game thread through the registered function
// table. Reads only bounded game-owned word-swapped RDRAM: dispatcher 8007D848,
// transition 8007DF44, scene 800923E4, car count 80080D90 (all u32), sample
// counter 8007E530 (u32 samples), and first car 800BA7F0..800BC090 (raw u32
// words, stride 18A0). Poll divider 8007FE1C is u32 VIs minus one; 8007FDF4
// is the low u32 of the fixed 46.875 MHz tick interval. Only scenes 0/8/9 are
// accepted, excluding authored demos.
// Sound list buffer selector 8007E844 is a byte; the other observed sound words
// are pointers/counts, not host pointers. Only the camera-offset experiment
// temporarily writes/restores guest state, as documented beside that access.
// The real draw call owns its writes and synchronizes with BOSS normally.
// Other workers can run during waits: reported differences are interval
// observations, not a complete attribution of writes to the draw function.
// Null/transition/unexpected scene fails closed. Host copies die after each
// observation. Remove the whole probe after the rendering seam is recovered.
void __wrap_func_8000C560(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx) return;
    static unsigned settled = 0, trials = 0;
    auto eligible = [&] {
        const auto scene = word(rdram, 0x800923E4);
        return word(rdram, 0x8007D848) == 0x8000C560 &&
            !word(rdram, 0x8007DF44) && word(rdram, 0x80080D90) != 0 &&
            (scene == 0 || scene == 8 || scene == 9);
    };
    one_vi = settled >= 59 && eligible() && std::strcmp(mode(), "split60") == 0;
    direct_present = settled >= 59 && trials < 6 && eligible() && std::strcmp(mode(), "present120") == 0;
    prototype_set_present(direct_present);
    const auto frame_start = std::chrono::steady_clock::now();
    ++race_frame;
    __real_func_8000C560(rdram, ctx);
    // One subsequent normal draw drains the last extra task through the usual
    // BOSS synchronization before exiting. Do not wait on the graphics worker
    // with the guest scheduler locked or consume its completion message twice.
    if (trials == 6) std::_Exit(0);
    if (!eligible()) {
        settled = 0;
        one_vi = false;
        return;
    }
    const auto scene = word(rdram, 0x800923E4);
    if (++settled < 60) return;
    std::vector<uint32_t> before(0x18A0 / 4);
    for (unsigned i = 0; i < before.size(); ++i)
        before[i] = word(rdram, 0x800BA7F0 + 4 * i);
    const auto samples = word(rdram, 0x8007E530);
    const auto divider = word(rdram, 0x8007FE1C), ticks = word(rdram, 0x8007FDF4);
    const auto list = MEM_BU(0, int32_t(0x8007E844));
    // Translation-only camera experiment. 800ABE40 is view zero; +2DC points
    // to the ROM-owned camera. 80010A5C passes camera+30 as the eye position to
    // CPU geometry/culling (8000DBA0). Three float32 world coordinates, aligned,
    // big-endian via MEM_W. Temporary writes live only across the extra draw;
    // the authoritative words are restored bit-exactly before simulation resumes.
    // No rotation interpolation, split-screen, or camera-cut handling is claimed.
    const auto camera = word(rdram, 0x800ABE40 + 0x2DC);
    if (camera < 0x80000000 || camera > 0x807FFF80 || (camera & 3)) std::_Exit(5);
    std::array<uint32_t, 3> eye;
    for (unsigned i = 0; i < 3; ++i) eye[i] = word(rdram, camera + 0x30 + 4 * i);
    const bool offset = std::strcmp(mode(), "camera-offset") == 0;
    if (offset) MEM_W(0x30, int32_t(camera)) = std::bit_cast<uint32_t>(std::bit_cast<float>(eye[0]) + 1.0f);
    recomp_context draw_context;
    std::array<uint8_t, sizeof(recomp_context)> caller_snapshot;
    std::memcpy(caller_snapshot.data(), ctx, sizeof(*ctx));
    // Immutable host observations, not a whole-RDRAM rewind. Four fixed view
    // records at 800ABE40 (stride 1418), the validated camera's 80 bytes, and
    // first-car words above. Raw u32 bits, MEM_W byte order, game thread only.
    // Also observe u32 arena cursors FE70/FE74 and buffer selector ED94; never
    // restore them while BOSS can own the preceding task. The normal ROM path
    // resets/recycles arenas and performs its SP_BREAK/completion handshakes.
    std::vector<uint32_t> views(0x1418 * 4 / 4);
    for (unsigned i = 0; i < views.size(); ++i) views[i] = word(rdram, 0x800ABE40 + 4 * i);
    std::array<uint32_t, 32> camera_snapshot;
    for (unsigned i = 0; i < camera_snapshot.size(); ++i) camera_snapshot[i] = word(rdram, camera + 4 * i);
    const auto start = std::chrono::steady_clock::now();
    extra = true;
    const unsigned repeats = direct_present ? 3 : (std::strcmp(mode(), "repeat") == 0 || render_only() ? 2 : 1);
    for (draw_number = 1; draw_number <= repeats; ++draw_number) {
        const auto history_before = preserved_flare_history;
        copy_draw_context(draw_context, *ctx);
        func_8000A30C(rdram, &draw_context);
        bool camera_same = word(rdram, 0x800ABE40 + 0x2DC) == camera;
        for (unsigned i = 0; i < camera_snapshot.size(); ++i)
            camera_same &= camera_snapshot[i] == word(rdram, camera + 4 * i);
        bool car_same = true;
        for (unsigned i = 0; i < before.size(); ++i) {
            // These four words are current-arena list heads/tail links, not
            // vehicle transforms (28314 -> 28C04/28FA0 -> 29D6C..29DD8).
            if (i * 4 >= 0xB90 && i * 4 <= 0xB9C) continue;
            car_same &= before[i] == word(rdram, 0x800BA7F0 + i * 4);
        }
        std::lock_guard lock(output_mutex);
        std::fprintf(stderr, "[prototype-snapshot] {\"frame\":%u,\"draw\":%u,\"context_unchanged\":%s,"
            "\"camera_unchanged\":%s,\"car_nonlink_words_unchanged\":%s,\"sample_unchanged\":%s,"
            "\"matrix_arena_top\":%u,\"geometry_arena_top\":%u,\"next_arena\":%u,\"preserved_flare_calls\":%u,\"view_word_changes\":[",
            race_frame, draw_number, std::memcmp(ctx, caller_snapshot.data(), sizeof(*ctx)) == 0 ? "true" : "false",
            camera_same ? "true" : "false", car_same ? "true" : "false",
            samples == word(rdram, 0x8007E530) ? "true" : "false", word(rdram, 0x8007FE70),
            word(rdram, 0x8007FE74), word(rdram, 0x8007ED94), preserved_flare_history - history_before);
        unsigned changed = 0;
        for (unsigned i = 0; i < views.size(); ++i) {
            auto after = word(rdram, 0x800ABE40 + 4 * i);
            if (after != views[i]) std::fprintf(stderr, "%s[%u,%u,%u]", changed++ ? "," : "", i * 4, views[i], after);
        }
        std::fprintf(stderr, "],\"car_render_links\":[");
        for (unsigned i = 0; i < 4; ++i) std::fprintf(stderr, "%s%u", i ? "," : "", word(rdram, 0x800BA7F0 + 0xB90 + i * 4));
        std::fprintf(stderr, "]}\n");
    }
    draw_number = 0;
    extra = false;
    one_vi = false;
    if (offset)
        for (unsigned i = 0; i < 3; ++i) MEM_W(int32_t(0x30 + 4 * i), int32_t(camera)) = eye[i];
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    const double frame_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - frame_start).count();
    std::lock_guard output_lock(output_mutex);
    std::fprintf(stderr, "[render-prototype] {\"trial\":%u,\"frame\":%u,\"gated\":%s,\"scene\":%u,"
        "\"draw_ms\":%.3f,\"frame_ms\":%.3f,\"sample_before\":%u,\"sample_after\":%u,"
        "\"sound_buffer_before\":%u,\"sound_buffer_after\":%u,"
        "\"divider_before\":%u,\"divider_after\":%u,\"ticks_before\":%u,\"ticks_after\":%u,"
        "\"skipped_publish\":%u,\"skipped_consume\":%u,\"skipped_audio_build\":%u,"
        "\"skipped_audio_retire\":%u,\"preserved_flare_history\":%u,\"skipped_vi_waits\":%u,\"car_word_changes\":[",
        ++trials, race_frame, gated() ? "true" : "false", scene, ms, frame_ms, samples,
        word(rdram, 0x8007E530), unsigned(list), unsigned(MEM_BU(0, int32_t(0x8007E844))),
        divider, word(rdram, 0x8007FE1C), ticks, word(rdram, 0x8007FDF4),
        skipped_publish, skipped_consume, skipped_audio_build, skipped_audio_retire, preserved_flare_history, skipped_vi_waits);
    unsigned changed = 0;
    for (unsigned i = 0; i < before.size(); ++i) {
        const auto after = word(rdram, 0x800BA7F0 + 4 * i);
        if (before[i] == after) continue;
        std::fprintf(stderr, "%s{\"offset\":%u,\"before\":%u,\"after\":%u}",
            changed++ ? "," : "", 4 * i, before[i], after);
    }
    std::fprintf(stderr, "]}\n");
    std::fflush(stderr);
}
}
