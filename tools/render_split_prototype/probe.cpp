// THROWAWAY native experiment. Linked only by run.py, never into the player.
// Question: can the race draw root run again without consuming simulation or
// sound events? Guest VI/audio clocks remain original; present120 experiments
// with independent framebuffer presentation in a copied runtime object.
#include "recomp.h"
#include "boss_rsp.h"
#include "context.h"
#include "pose.h"
#include "vectors.h"
#include "cadence.h"
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
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
bool early_pending = false, normal_endpoint = false;
unsigned early_trials = 0, early_settled = 0;
std::chrono::steady_clock::time_point early_frame_start;
unsigned normal_publish = 0, normal_consume = 0, normal_audio_build = 0, normal_audio_retire = 0;
unsigned skipped_vi_waits = 0;
std::atomic_uint prototype_vi_ticks{0}, accelerator_samples{0};
bool hold_accelerator() {
    static const bool enabled = std::getenv("WDC_RENDER_PROTOTYPE_ACCELERATOR") != nullptr;
    return enabled;
}
std::atomic_uint submitted_tasks{0}, completed_tasks{0};
std::atomic_uint requested_presents{0}, completed_presents{0};
std::atomic_uint64_t last_bound_present{0}, last_finished_present{0};
thread_local bool binding_screen = false;
std::atomic_bool capture_started{false};
void finish_present_audit() {
    // Final audit drain, also used per frame in capture-only builds.
    // Performance runs never use this as a per-frame pacing fence.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (last_finished_present.load() < last_bound_present.load()) {
        if (std::chrono::steady_clock::now() >= deadline) std::_Exit(22);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
std::mutex render_memory_mutex;
uint8_t* authoritative_memory = nullptr;
std::vector<uint8_t> frozen_ram, render_ram;
enum class PoseKind { Camera, Body, Wheel };
std::vector<prototype::Pose> previous_poses;
std::vector<prototype::Vector3> previous_shadow_inputs;
unsigned shadow_rebuilds = 0;
std::vector<uint32_t> previous_pose_identity;
uint32_t previous_pose_sample = 0;
unsigned shadow_receives = 0, shadow_sends = 0;
thread_local uint8_t* receive_destination_memory = nullptr;
unsigned skipped_publish = 0, skipped_consume = 0;
unsigned skipped_audio_build = 0, skipped_audio_retire = 0, preserved_flare_history = 0;
unsigned skipped_car_sound = 0;
unsigned race_frame = 0;
unsigned draw_number = 0;
std::array<uint32_t, 32> frozen_camera{};
uint32_t redirected_camera = 0;
unsigned camera_copies = 0, wheel_pose_reads = 0;
struct Label { unsigned frame; unsigned draw; uint8_t* memory; };
thread_local Label active_rsp_label{};
thread_local unsigned texture_calls = 0, texture_verified = 0;
thread_local std::vector<std::pair<const char*, double>> rt64_phase_records;
std::mutex label_mutex;
std::mutex output_mutex;
// Callers hold output_mutex across each complete record. Keep fragments in
// host memory until newline so an unbuffered stderr does not issue a separate
// file write for every matrix element or changed word. No evidence is dropped.
void log_record(const char* format, ...) __attribute__((format(gnu_printf, 1, 2)));
void log_record(const char* format, ...) {
    thread_local std::string line;
    char fragment[4096];
    va_list args;
    va_start(args, format);
    const int size = std::vsnprintf(fragment, sizeof(fragment), format, args);
    va_end(args);
    if (size < 0 || size >= int(sizeof(fragment))) std::_Exit(14);
    line.append(fragment, size_t(size));
    if (!line.empty() && line.back() == '\n') {
        std::fwrite(line.data(), 1, line.size(), stderr);
        line.clear();
    }
}
struct PhaseTimer {
    const char* name;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~PhaseTimer() {
        std::lock_guard lock(output_mutex);
        log_record("[prototype-phase] {\"frame\":%u,\"draw\":%u,\"phase\":\"%s\",\"ms\":%.3f}\n",
            race_frame, draw_number, name,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
};
std::deque<Label> labels;
std::deque<Label> presentation_labels;
const char* mode() {
    const char* value = std::getenv("WDC_RENDER_PROTOTYPE_MODE");
    return value ? value : "raw";
}
unsigned settle_frames() {
    const char* value = std::getenv("WDC_RENDER_PROTOTYPE_SETTLE");
    return value ? unsigned(std::strtoul(value, nullptr, 10)) : 60;
}
unsigned trial_count() {
    const char* value = std::getenv("WDC_RENDER_PROTOTYPE_TRIALS");
    return value ? unsigned(std::strtoul(value, nullptr, 10)) : 6;
}
bool gated() {
    return std::strcmp(mode(), "raw") != 0;
}
bool shadow_inputs() {
    static const bool enabled = std::getenv("WDC_RENDER_PROTOTYPE_SHADOW_INPUTS") != nullptr;
    return enabled;
}
bool shadow_only_control() {
    static const bool enabled = std::getenv("WDC_RENDER_PROTOTYPE_SHADOW_ONLY") != nullptr;
    return enabled;
}
bool wheel_poses() {
    static const bool enabled = std::getenv("WDC_RENDER_PROTOTYPE_WHEEL_POSES") != nullptr;
    return enabled;
}
bool wheel_only_control() {
    static const bool enabled = std::getenv("WDC_RENDER_PROTOTYPE_WHEEL_ONLY") != nullptr;
    return enabled;
}
bool camera_snapshot_mode() { return std::strcmp(mode(), "camera-snapshot") == 0; }
bool sample_motion_mode() { return std::strcmp(mode(), "motion-samples") == 0; }
bool skip_late_draws() {
    static const bool enabled = std::getenv("WDC_RENDER_PROTOTYPE_SKIP_LATE_DRAWS") != nullptr;
    return enabled && sample_motion_mode();
}
bool motion_rate_mode() { return std::strcmp(mode(), "motion-rate") == 0 || sample_motion_mode(); }
bool cadence_mode() { return std::strcmp(mode(), "shadow-rate") == 0 || motion_rate_mode(); }
bool frozen_present_mode() { return std::strcmp(mode(), "shadow120") == 0 || std::strcmp(mode(), "shadow-rate") == 0; }
unsigned present_hz() {
    static const unsigned hz = [] {
        const char* value = std::getenv("WDC_RENDER_PROTOTYPE_PRESENT_HZ");
        const unsigned rate = value ? unsigned(std::strtoul(value, nullptr, 10)) : 120;
        if (rate < 60 || rate > 240) std::_Exit(20);
        return rate;
    }();
    return hz;
}
bool endpoint_mode() { return std::strcmp(mode(), "endpoint120") == 0; }
bool motion_mode() { return std::strcmp(mode(), "motion120") == 0 || motion_rate_mode() || endpoint_mode(); }
bool pose_mode() { return std::strcmp(mode(), "pose-snapshot") == 0 || motion_mode(); }
bool shadow_mode() { return std::strcmp(mode(), "shadow-snapshot") == 0 || frozen_present_mode() || pose_mode(); }
bool render_only() { return std::strcmp(mode(), "render-only") == 0 || std::strcmp(mode(), "present120") == 0 || camera_snapshot_mode() || shadow_mode(); }
uint8_t* queue_memory(uint8_t* memory) {
    return !render_ram.empty() && memory == render_ram.data() ? authoritative_memory : memory;
}
void drain_renderer(uint8_t* memory) {
    // A queued screen update can still read RAM after its graphics task ends.
    // Retain the bank through that callback as well as BOSS/RT64 task cleanup.
    while (completed_tasks.load() < submitted_tasks.load() ||
           completed_presents.load() < requested_presents.load())
        ultramodern::wait_for_external_message_timed(memory, 1);
}
uint32_t word(uint8_t* rdram, uint32_t address) {
    return uint32_t(MEM_W(0, int32_t(address)));
}
unsigned rdp_words(unsigned op) {
    return op >= 8 && op <= 15 ? 8 + ((op & 4) ? 16 : 0) +
        ((op & 2) ? 16 : 0) + ((op & 1) ? 4 : 0) : (op == 0x24 || op == 0x25 ? 4 : 2);
}
}
extern "C" {
void __real_wdc_hud_normal_layout(uint8_t*, recomp_context*);
void __wrap_wdc_hud_normal_layout(uint8_t* rdram, recomp_context* ctx) {
    __real_wdc_hud_normal_layout(rdram, ctx);
    // USA HUD 2E13C, hook after 2E1A4 before needle smoothing: current-view
    // pointer u32 at 8007E570; single-view record ABE40, angle float32 radians
    // at +2D8 (2E3A4 selects +2C4, 2E440 reads +14). 2E4C0..2E508 advances
    // and stores it once per draw. MEM_W preserves N64 word-swapped bits.
    // Game-thread host snapshot, replaced each normal frame; no pointer retained.
    // Replaying the normal draw's input affects owned RAM only, before the ROM
    // re-applies its smoothing. Invalid/missing current-frame evidence aborts.
    // Endpoint mode runs extras before normal; it does not use this post-normal
    // replay. Replace this seam with an explicit fixed-rate HUD snapshot later.
    static unsigned frame = 0;
    static uint32_t angle = 0;
    const auto view = word(rdram, 0x8007E570);
    if (!extra) {
        if (view == 0x800ABE40) { frame = race_frame; angle = word(rdram, view+0x2D8); }
        return;
    }
    if (!shadow_mode() || endpoint_mode()) return;
    if (view != 0x800ABE40 || rdram != render_ram.data() || frame != race_frame) std::_Exit(24);
    const auto after_normal = word(rdram, view+0x2D8);
    MEM_W(0, int32_t(view+0x2D8)) = angle;
    std::lock_guard lock(output_mutex);
    log_record("[prototype-hud-needle] {\"frame\":%u,\"draw\":%u,\"input\":%u,\"after_normal\":%u}\n", race_frame,draw_number,angle,after_normal);
}
void __real_osContGetReadData_recomp(uint8_t*, recomp_context*);
void __wrap_osContGetReadData_recomp(uint8_t* rdram, recomp_context* ctx) {
    __real_osContGetReadData_recomp(rdram, ctx);
    if (!hold_accelerator()) return;
    // USA controller worker after runtime sampling: a0 points to four 6-byte
    // OSContPad records at 800AAAE0, owned until next sample by the game.
    // Port 0 button u16 (+0), signed sticks (+2/+3), error u8 (+4) use
    // word-swapped MEM helpers. A=8000 is held; steering is neutral N64 units.
    // Only the known 24-byte in-bounds array and live race callback (u32 at
    // 8007D848 == 8000C560) are eligible; replay selector u32 at 8007DF94
    // equal to 2, nonzero guest error flags and other pointers are
    // untouched. Guest threads are cooperatively scheduled; no pointer retained.
    // This is an opt-in input fixture, removable when scripted controller APIs
    // are available. It does not run at extra-draw or simulation-step boundaries.
    const uint32_t pad = uint32_t(ctx->r4);
    if (pad != 0x800AAAE0 || word(rdram, 0x8007D848) != 0x8000C560 ||
        word(rdram, 0x8007DF94) == 2 || MEM_BU(4, int32_t(pad))) return;
    MEM_H(0, int32_t(pad)) = uint16_t(MEM_H(0, int32_t(pad))) | 0x8000u;
    MEM_B(2, int32_t(pad)) = 0;
    MEM_B(3, int32_t(pad)) = 0;
    ++accelerator_samples;
}
bool prototype_disable_readback(uint8_t* memory) {
    if (!std::getenv("WDC_RENDER_PROTOTYPE_NO_READBACK")) return false;
    std::lock_guard label_lock(label_mutex);
    if (labels.empty() || labels.front().memory != memory) std::_Exit(17);
    const auto label = labels.front();
    const bool disabled = !render_ram.empty() && memory == render_ram.data();
    if (label.frame >= settle_frames()) {
        std::lock_guard output_lock(output_mutex);
        log_record("[prototype-readback] {\"frame\":%u,\"draw\":%u,\"disabled\":%s}\n",
            label.frame, label.draw, disabled ? "true" : "false");
    }
    return disabled;
}
void __real_func_8003062C(uint8_t*, recomp_context*);
void __wrap_func_8003062C(uint8_t* rdram, recomp_context* ctx) {
    // Observe the game-thread shadow-cache rebuild (car+2F4 guard, +9F8 bit0).
    // Guest u32 MEM_W reads, N64 order; exact aligned car starts within the
    // validated 8 MiB car array. Invalid candidates are ignored, never changed.
    // Original routine retains all writes/ownership. No cached output override.
    if (extra && shadow_inputs() && rdram == render_ram.data()) {
        const uint32_t car=uint32_t(ctx->r4), base=0x800BA7F0, count=word(rdram,0x80080D90);
        if(count <= (0x80800000u-base)/0x18A0 && car>=base && car<base+count*0x18A0 && (car-base)%0x18A0==0 &&
           word(rdram,car+0x2F4) && !(word(rdram,car+0x9F8)&1)) ++shadow_rebuilds;
    }
    __real_func_8003062C(rdram,ctx);
}
void __real_func_8001A97C(uint8_t*, recomp_context*);
void __wrap_func_8001A97C(uint8_t* rdram, recomp_context* ctx) {
    // 22254 copies 64 raw matrix bytes from a1 for wheels. Observe only:
    // game thread, guest u32 address, car base BA7F0/stride18A0, four matrices
    // at +94 + i*84. Validated car span is in 8 MiB; ignore other copy sources.
    // No guest writes here; original copy owns its destination/lifetime.
    if (extra && wheel_poses() && rdram == render_ram.data()) {
        const uint32_t address = uint32_t(ctx->r5), base = 0x800BA7F0;
        const uint32_t count = word(rdram, 0x80080D90);
        if (count <= (0x80800000u-base)/0x18A0 && address >= base && address < base+count*0x18A0) {
            const unsigned offset = (address-base)%0x18A0;
            if (offset >= 0x94 && offset <= 0x94+3*0x84 && (offset-0x94)%0x84 == 0) ++wheel_pose_reads;
        }
    }
    __real_func_8001A97C(rdram, ctx);
}
void prototype_texture_hash_result(bool verified) {
    ++texture_calls; texture_verified += verified;
}
void prototype_rt64_phase(const char* name, double ms) {
    if (!active_rsp_label.draw) return;
    // Names are static literals in the copied RT64 object. Defer file output
    // until BOSS returns, so nested wait logging cannot inflate full-sync time.
    rt64_phase_records.emplace_back(name, ms);
}
bool prototype_store_message(uint8_t* runtime_ram, int32_t destination, uint32_t value) {
    if (!receive_destination_memory) return false;
    const auto address = uint32_t(destination);
    if (runtime_ram != authoritative_memory || address < 0x80000000 ||
        address > 0x807FFFFC || (address & 3)) std::_Exit(11);
    std::memcpy(receive_destination_memory + (address - 0x80000000), &value, 4);
    return true;
}
std::mutex& prototype_memory_mutex() { return render_memory_mutex; }
uint8_t* prototype_task_memory() {
    std::lock_guard lock(label_mutex);
    if (labels.empty() || !labels.front().memory) std::_Exit(10);
    return labels.front().memory;
}
void prototype_renderer_finished() { ++completed_tasks; }
// Bounded diagnostic timings include scheduler/graphics waits, not CPU time.
#define TIME_DRAW_PHASE(name) \
void __real_##name(uint8_t*, recomp_context*); \
void __wrap_##name(uint8_t* rdram, recomp_context* ctx) { \
    const auto start = std::chrono::steady_clock::now(); \
    __real_##name(rdram, ctx); \
    if (extra) { \
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); \
        std::lock_guard lock(output_mutex); \
        log_record("[prototype-phase] {\"frame\":%u,\"draw\":%u,\"phase\":\"" #name "\",\"ms\":%.3f}\n", race_frame, draw_number, ms); \
    } \
}
TIME_DRAW_PHASE(func_80024038)
TIME_DRAW_PHASE(func_80010ECC)
TIME_DRAW_PHASE(func_80024FCC)
TIME_DRAW_PHASE(func_80025350)
#undef TIME_DRAW_PHASE
void __real_func_80010A5C(uint8_t*, recomp_context*);
void __wrap_func_80010A5C(uint8_t* rdram, recomp_context* ctx) {
    const auto start = std::chrono::steady_clock::now();
    if (extra && camera_snapshot_mode() && uint32_t(ctx->r4) == 0x800ABE40) {
        // Single-view camera snapshot, game thread only, after 24038 resets
        // the transform arena. Reserve 128 aligned bytes below FE70 using
        // the same downward allocation/collision rule as 2CF30. ED40 is the
        // upward command cursor. This is raw camera storage, not an encoded
        // matrix, so FE78's matrix count is not incremented. Fail before any
        // access on bad spans/collision. Do not rewind the reservation: normal
        // arena ownership and SP_BREAK handshakes govern reuse next draw.
        const auto top = word(rdram, 0x8007FE70);
        const auto commands = word(rdram, 0x8007ED40);
        if (top < 0x80000080 || top > 0x80800000 || (top & 15) ||
            commands < 0x80000000 || commands >= top - 128 || redirected_camera) std::_Exit(9);
        const auto owned = top - 128;
        redirected_camera = word(rdram, 0x800ABE40 + 0x2DC);
        MEM_W(0, int32_t(0x8007FE70)) = owned;
        for (unsigned i = 0; i < frozen_camera.size(); ++i)
            MEM_W(0, int32_t(owned + i * 4)) = frozen_camera[i];
        // Only the reference is redirected; original camera bytes remain
        // untouched. Keep this reference through both view passes, then
        // restore it after the draw has returned. No host pointer enters RAM.
        MEM_W(0, int32_t(0x800ABE40 + 0x2DC)) = owned;
        ++camera_copies;
    }
    __real_func_80010A5C(rdram, ctx);
    if (extra) {
        std::lock_guard lock(output_mutex);
        log_record("[prototype-phase] {\"frame\":%u,\"draw\":%u,\"phase\":\"func_80010A5C\",\"ms\":%.3f}\n", race_frame, draw_number,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
}
void __real_func_8001FC64(uint8_t*, recomp_context*);
void __wrap_func_8001FC64(uint8_t* rdram, recomp_context* ctx) {
    // 218F0's near-car branch (21994) publishes vehicle sound parameters.
    // This also ramps signed halfword car+A0E by up to 20 per invocation
    // (1FD38..1FD60), updates gear/event history, and calls 201BC/205F8.
    // It is void at this boundary; presentation extras must not advance it.
    if (extra && render_only()) { ++skipped_car_sound; return; }
    __real_func_8001FC64(rdram, ctx);
}
void __real_func_8001F7F8(uint8_t*, recomp_context*);
void __wrap_func_8001F7F8(uint8_t* rdram, recomp_context* ctx) {
    // Matching distant-car branch at 219A8 retires sound-slot ownership and
    // resets car+A08. Preserve the normal-frame audio lifecycle on extras.
    if (extra && render_only()) { ++skipped_car_sound; return; }
    __real_func_8001F7F8(rdram, ctx);
}
void prototype_vi_observed() { ++prototype_vi_ticks; }
// Host-only telemetry: graphics worker binds RT64's monotonic presentation ID
// to its queued label. RT64's present worker reports after swapChain->present.
// No guest access. Separate locks avoid holding label state during log writes.
void prototype_screen_begin(bool enabled) { binding_screen = enabled; if (enabled) capture_started = true; }
bool prototype_capture_enabled() { return capture_started.load(); }
bool prototype_explicit_present() { return binding_screen; }
void prototype_present_bound(uint64_t previous, uint64_t id) {
    if (!binding_screen) return;
    Label label;
    {
        std::lock_guard lock(label_mutex);
        if (presentation_labels.empty()) std::_Exit(22);
        label = presentation_labels.front();
    }
    {
        std::lock_guard lock(output_mutex);
        log_record("[prototype-present-id] {\"previous\":%llu,\"id\":%llu,\"frame\":%u,\"draw\":%u}\n",
            (unsigned long long)previous, (unsigned long long)id, label.frame, label.draw);
        last_bound_present = id;
    }
#ifdef WDC_PROTOTYPE_CAPTURE
    // Capture-only serialization: prevent RT64 dropping queued images while
    // file output stalls its present worker. Never used for timing acceptance.
    finish_present_audit();
#endif
}
void prototype_swap_observed(uint64_t id, uint32_t index, bool valid) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    std::lock_guard lock(output_mutex);
    log_record("[prototype-swap] {\"id\":%llu,\"index\":%u,\"valid\":%s,\"us\":%lld}\n",
        (unsigned long long)id, index, valid ? "true" : "false",
        (long long)std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}
void prototype_swap_batch_finished(uint64_t id) { last_finished_present = id; }
void prototype_present_observed(uint32_t origin) {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    Label label;
    {
        std::lock_guard lock(label_mutex);
        if (presentation_labels.empty()) std::_Exit(16);
        label = presentation_labels.front();
        presentation_labels.pop_front();
    }
    std::lock_guard lock(output_mutex);
    log_record("[prototype-present] {\"us\":%lld,\"origin\":%u,\"vi_ticks\":%u,\"frame\":%u,\"draw\":%u}\n",
        (long long)std::chrono::duration_cast<std::chrono::microseconds>(now).count(), origin, prototype_vi_ticks.load(), label.frame, label.draw);
    ++completed_presents;
}
void __real_func_800013DC(uint8_t*, recomp_context*);
void __wrap_func_800013DC(uint8_t* rdram, recomp_context* ctx) {
    const auto start = std::chrono::steady_clock::now();
    inside_swap = direct_present;
    __real_func_800013DC(rdram, ctx);
    inside_swap = false;
    if (extra) {
        std::lock_guard lock(output_mutex);
        log_record("[prototype-phase] {\"frame\":%u,\"draw\":%u,\"phase\":\"swap\",\"ms\":%.3f}\n", race_frame, draw_number,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
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
    const auto destination = uint32_t(ctx->r5);
    auto* runtime_ram = queue_memory(rdram);
    if (runtime_ram != rdram) ++shadow_receives;
    const auto queue = uint32_t(ctx->r4);
    if (runtime_ram != rdram && (queue < 0x80000000 || queue > 0x807FFFE8 || (queue & 3))) std::_Exit(11);
    if (runtime_ram != rdram && destination &&
        (destination < 0x80000000 || destination > 0x807FFFFC || (destination & 3))) std::_Exit(11);
    // Runtime queue/thread structures remain authoritative, but its result
    // store is redirected before it touches memory. Each guest thread has a
    // native thread: thread_local routing survives a scheduler yield without
    // redirecting another guest thread's receives.
    auto* previous_destination_memory = receive_destination_memory;
    if (runtime_ram != rdram) receive_destination_memory = rdram;
    __real_osRecvMesg_recomp(runtime_ram, ctx);
    receive_destination_memory = previous_destination_memory;
}
void __real_osSendMesg_recomp(uint8_t*, recomp_context*);
void __wrap_osSendMesg_recomp(uint8_t* rdram, recomp_context* ctx) {
    if (queue_memory(rdram) != rdram) {
        const auto queue = uint32_t(ctx->r4);
        if (queue < 0x80000000 || queue > 0x807FFFE8 || (queue & 3)) std::_Exit(11);
        ++shadow_sends;
    }
    __real_osSendMesg_recomp(queue_memory(rdram), ctx);
}
void osCreateMesgQueue_recomp(uint8_t*, recomp_context*);
void verify_shadow_receive(uint8_t* rdram, recomp_context* source) {
    static bool verified = false;
    if (verified) return;
    // Borrow a 256-byte inactive stack interval below the caller's SP for an
    // immediate, nonblocking private queue test; restore both banks afterward.
    // No external producer knows this queue. All addresses are aligned KSEG0
    // u32s in 8 MiB, game thread only. Runtime scheduling retains real queues.
    const auto sp = uint32_t(source->r29);
    if (sp < 0x80000400 || sp > 0x80800000 || (sp & 15)) std::_Exit(13);
    const auto base = sp - 0x200, physical = base - 0x80000000;
    std::array<uint8_t, 256> original_bytes, owned_bytes;
    std::memcpy(original_bytes.data(), rdram + physical, original_bytes.size());
    std::memcpy(owned_bytes.data(), render_ram.data() + physical, owned_bytes.size());
    recomp_context call;
    copy_draw_context(call, *source);
    call.r29 = int32_t(sp - 0x300);
    call.r4 = int32_t(base); call.r5 = int32_t(base + 64); call.r6 = 1;
    osCreateMesgQueue_recomp(rdram, &call);
    call.r4 = int32_t(base); call.r5 = 0x13579BDF; call.r6 = 0;
    __real_osSendMesg_recomp(rdram, &call);
    if (call.r2 != 0) std::_Exit(13);
    const uint32_t sentinel = 0xA1B2C3D4, owned_sentinel = 0x10203040;
    std::memcpy(rdram + physical + 128, &sentinel, 4);
    std::memcpy(render_ram.data() + physical + 128, &owned_sentinel, 4);
    call.r4 = int32_t(base); call.r5 = int32_t(base + 128); call.r6 = 0;
    __wrap_osRecvMesg_recomp(render_ram.data(), &call);
    const bool passed = call.r2 == 0 && word(rdram, base + 128) == sentinel &&
        word(render_ram.data(), base + 128) == 0x13579BDF;
    std::memcpy(rdram + physical, original_bytes.data(), original_bytes.size());
    std::memcpy(render_ram.data() + physical, owned_bytes.data(), owned_bytes.size());
    if (!passed) std::_Exit(13);
    verified = true;
    std::lock_guard lock(output_mutex);
    log_record("[prototype-queue-test] {\"authoritative_sentinel_preserved\":true,\"owned_result_received\":true}\n");
}
void __real_osViSwapBuffer_recomp(uint8_t*, recomp_context*);
void __wrap_osViSwapBuffer_recomp(uint8_t* rdram, recomp_context* ctx) {
    const auto start = std::chrono::steady_clock::now();
    // The normal draw supplies current-state evidence and audio. In motion
    // mode only the subsequent owned poses are presented, in increasing alpha.
    // Calling the real swap here would display the endpoint before its blends.
    if (inside_swap && motion_mode() && !extra && !normal_endpoint) return;
    if (inside_swap) {
        // Service external messages while pacing: this may schedule the
        // input worker, but never consumes simulation samples in this thread.
        static auto deadline = std::chrono::steady_clock::now();
        auto now = std::chrono::steady_clock::now();
        if (now > deadline + std::chrono::milliseconds(34)) deadline = now;
        while (now < deadline) {
            // The scheduler owns the live queue/thread records even when
            // this presentation was generated from the frozen render bank.
            ultramodern::wait_for_external_message_timed(queue_memory(rdram), 1);
            now = std::chrono::steady_clock::now();
        }
        static prototype::PresentationPeriod period(present_hz());
        deadline += std::chrono::nanoseconds(period.next_ns());
    }
    if (inside_swap) {
        std::lock_guard lock(label_mutex);
        presentation_labels.push_back({race_frame, draw_number, rdram});
        ++requested_presents;
    }
    __real_osViSwapBuffer_recomp(rdram, ctx);
    if (extra) {
        std::lock_guard lock(output_mutex);
        log_record("[prototype-phase] {\"frame\":%u,\"draw\":%u,\"phase\":\"pace_and_queue\",\"ms\":%.3f}\n", race_frame, draw_number,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
    }
}
void __real_func_80043274(uint8_t*, recomp_context*);
void __wrap_func_80043274(uint8_t* rdram, recomp_context* ctx) {
    // 24FCC passes its current command cursor in a0 and stores v0 back to
    // ED84. On a presentation-only draw append no audio commands: identity
    // cursor result, leaving audio buffers, voice progression and DMA alone.
    if (extra && render_only()) { ++skipped_audio_build; ctx->r2 = ctx->r4; return; }
    if (!extra) ++normal_audio_build;
    __real_func_80043274(rdram, ctx);
}
void __real_func_80042FA8(uint8_t*, recomp_context*);
void __wrap_func_80042FA8(uint8_t* rdram, recomp_context* ctx) {
    if (extra && render_only()) { ++skipped_audio_retire; return; }
    if (!extra) ++normal_audio_retire;
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
    log_record("[prototype-projection] {\"frame\":%u,\"draw\":%u,\"matrix\":[", race_frame, draw_number);
    for (unsigned i = 0; i < 16; ++i) log_record("%s%u", i ? "," : "", word(rdram, 0x800B2A80 + i * 4));
    log_record("]}\n");
}
void __real_func_8000C560(uint8_t*, recomp_context*);
void func_800252D0(uint8_t*, recomp_context*);
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
        labels.push_back({race_frame, draw_number, rdram});
        ++submitted_tasks;
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
        if (labels.empty()) { log_record("[prototype-error] unlabelled task\n"); std::_Exit(4); }
        label = labels.front(); labels.pop_front();
    }
    if (label.memory != rdram) std::_Exit(12);
    active_rsp_label = label;
    texture_calls = texture_verified = 0;
    rt64_phase_records.clear();
    rt64_phase_records.reserve(32);
    struct Collector {
        wdc::rsp::RdpSink sink;
        void* context;
        std::vector<uint32_t> words;
        double sink_ms = 0;
        double sync_sink_ms = 0;
        size_t parsed = 0;
    } capture{sink, context, {}, 0, 0, 0};
    const auto task_start = std::chrono::steady_clock::now();
    original_rsp(rdram, task, [](void* opaque, uint8_t* memory, uint32_t begin, uint32_t end) {
        auto& c = *static_cast<Collector*>(opaque);
        // The BOSS bridge validates these aligned word-swapped ranges. Copy
        // synchronously before the sink returns; no guest pointer survives.
        for (auto at = begin; at < end; at += 4) {
            uint32_t value;
            std::memcpy(&value, memory + at, 4);
            c.words.push_back(value);
        }
        bool full_sync = false;
        while (c.parsed < c.words.size()) {
            const unsigned op = (c.words[c.parsed] >> 24) & 63;
            const unsigned length = rdp_words(op);
            if (c.parsed + length > c.words.size()) break;
            full_sync |= op == 0x29;
            c.parsed += length;
        }
        const auto start = std::chrono::steady_clock::now();
        c.sink(c.context, memory, begin, end);
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        c.sink_ms += ms;
        if (full_sync) c.sync_sink_ms += ms;
    }, &capture);
    active_rsp_label = {};
    const auto task_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - task_start).count();
    if (label.frame < 60) return;
    uint64_t hash = 14695981039346656037ull;
    unsigned triangles = 0;
    bool complete = true;
    std::vector<uint32_t> headers;
    for (size_t i = 0; i < capture.words.size();) {
        const unsigned op = (capture.words[i] >> 24) & 63;
        const bool triangle = op >= 8 && op <= 15;
        const unsigned length = rdp_words(op);
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
    if (texture_calls) log_record("[prototype-texture-hash] {\"frame\":%u,\"draw\":%u,\"calls\":%u,\"verified\":%u}\n",
        label.frame, label.draw, texture_calls, texture_verified);
    for (const auto& [name, ms] : rt64_phase_records)
        log_record("[prototype-phase] {\"frame\":%u,\"draw\":%u,\"phase\":\"rt64_%s\",\"ms\":%.3f}\n",
            label.frame, label.draw, name, ms);
    // Compare owned vectors, not just hashes. BOSS tasks execute serially on
    // the graphics worker; the mutex also protects this bounded reference.
    static unsigned normal_frame = 0;
    static std::vector<uint32_t> normal_headers;
    static unsigned first_extra_frame = 0;
    static std::vector<uint32_t> first_extra_headers;
    if (!label.draw) { normal_frame = label.frame; normal_headers = headers; }
    if (label.draw == 1) { first_extra_frame = label.frame; first_extra_headers = headers; }
    const bool same = normal_frame == label.frame && headers == normal_headers;
    const bool same_extra = first_extra_frame == label.frame && headers == first_extra_headers;
    if (label.draw && normal_frame == label.frame && headers.size() == normal_headers.size() && !same) {
        unsigned changed = 0, first = unsigned(headers.size()/8), last = 0;
        for(unsigned i=0;i<headers.size();i+=8) {
            bool differs = false;
            for(unsigned j=0;j<8;++j) differs |= headers[i+j]!=normal_headers[i+j];
            if(differs) { ++changed; if(i/8<first) first=i/8; last=i/8; }
        }
        log_record("[prototype-header-diff] {\"frame\":%u,\"draw\":%u,\"changed_triangles\":%u,\"first\":%u,\"last\":%u,\"normal\":[", label.frame,label.draw,changed,first,last);
        for(unsigned j=0;j<8;++j) log_record("%s%u", j?",":"", normal_headers[first*8+j]);
        log_record("],\"extra\":[");
        for(unsigned j=0;j<8;++j) log_record("%s%u", j?",":"", headers[first*8+j]);
        log_record("]}\n");
    }

    log_record("[prototype-rdp] {\"frame\":%u,\"draw\":%u,\"extra\":%s,\"triangles\":%u,"
        "\"triangle_header_hash\":\"%016llx\",\"complete\":%s,\"same_headers_as_normal\":%s,\"task_ms\":%.3f,\"sink_ms\":%.3f,\"sync_chunk_sink_ms\":%.3f,\"owned_memory\":%s,\"same_headers_as_first_extra\":%s}\n", label.frame,
        label.draw, label.draw ? "true" : "false", triangles, (unsigned long long)hash, complete ? "true" : "false", same ? "true" : "false", task_ms, capture.sink_ms, capture.sync_sink_ms,
        !render_ram.empty() && rdram == render_ram.data() ? "true" : "false", same_extra ? "true" : "false");
}
void __wrap_func_8001F9CC(uint8_t* rdram, recomp_context* ctx) {
    if (extra && gated()) { ++skipped_publish; return; }
    if (!extra) ++normal_publish;
    __real_func_8001F9CC(rdram, ctx);
}
void __wrap_func_800214C8(uint8_t* rdram, recomp_context* ctx) {
    if (extra && gated()) { ++skipped_consume; return; }
    if (!extra) ++normal_consume;
    __real_func_800214C8(rdram, ctx);
}

// Called on the guest dispatcher/game thread through the registered function
// table. Reads only bounded game-owned word-swapped RDRAM: dispatcher 8007D848,
// transition 8007DF44, scene 800923E4, car count 80080D90 (all u32), sample
// counter 8007E530 (u32 samples), and first car 800BA7F0..800BC090 (raw u32
// words, stride 18A0). Poll divider 8007FE1C is u32 VIs minus one; 8007FDF4
// is the low u32 of the fixed 46.875 MHz tick interval. Only scenes 0/3/5/8/9 are
// accepted, excluding authored demos.
// Sound list buffer selector 8007E844 is a byte; the other observed sound words
// are pointers/counts, not host pointers. Camera-offset and flare-history
// preservation temporarily write/restore narrowly documented guest words.
// The real draw call owns its writes and synchronizes with BOSS normally.
// Other workers can run during waits: reported differences are interval
// observations, not a complete attribution of writes to the draw function.
// Null/transition/unexpected scene fails closed. Host copies die after each
// observation. Remove the whole probe after the rendering seam is recovered.
void run_render_trial(uint8_t*, recomp_context*, unsigned, std::chrono::steady_clock::time_point, unsigned&);
// Invoked only by the copied dispatcher's original 8000C870 call. The caller's
// stack and register context remain live until its one authoritative draw
// returns; all original post-draw calls stay in their original order.
void prototype_race_draw(uint8_t* rdram, recomp_context* ctx) {
    if (!endpoint_mode() || !early_pending) { func_8000A30C(rdram, ctx); return; }
    early_pending = false;
    if (word(rdram, 0x8007DF44)) std::_Exit(18);
    const auto samples = word(rdram, 0x8007E530);
    const auto publish = normal_publish, consume = normal_consume;
    const auto build = normal_audio_build, retire = normal_audio_retire;
    run_render_trial(rdram, ctx, word(rdram, 0x800923E4), early_frame_start, early_trials);
    normal_endpoint = true;
    func_8000A30C(rdram, ctx);
    recomp_context drain_context;
    copy_draw_context(drain_context, *ctx);
    func_800252D0(rdram, &drain_context);
    drain_renderer(rdram);
    normal_endpoint = false;
    std::lock_guard lock(output_mutex);
    log_record("[prototype-endpoint] {\"frame\":%u,\"sample_before\":%u,\"sample_after\":%u,\"publish\":%u,\"consume\":%u,\"audio_build\":%u,\"audio_retire\":%u,\"skipped_vi_waits\":%u}\n",
        race_frame, samples, word(rdram, 0x8007E530), normal_publish-publish, normal_consume-consume,
        normal_audio_build-build, normal_audio_retire-retire, skipped_vi_waits);
}
void __wrap_func_8000C560(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx) return;
    static unsigned settled = 0, trials = 0;
    auto eligible = [&] {
        const auto scene = word(rdram, 0x800923E4);
        return word(rdram, 0x8007D848) == 0x8000C560 &&
            !word(rdram, 0x8007DF44) && word(rdram, 0x80080D90) != 0 &&
            (scene == 0 || scene == 3 || scene == 5 || scene == 8 || scene == 9);
    };
    if (endpoint_mode()) {
        if (!eligible()) { early_settled = 0; previous_poses.clear(); previous_pose_identity.clear(); }
        early_pending = eligible() && ++early_settled >= settle_frames();
        direct_present = early_pending;
        prototype_set_present(direct_present);
        early_frame_start = std::chrono::steady_clock::now();
        ++race_frame;
        __real_func_8000C560(rdram, ctx);
        if (early_pending) std::_Exit(18); // Required dispatcher seam was not reached.
        if (early_trials == trial_count()) {
            finish_present_audit();
            std::lock_guard lock(output_mutex);
            std::fflush(stderr);
            std::_Exit(0);
        }
        return;
    }
    one_vi = settled + 1 >= settle_frames() && eligible() && std::strcmp(mode(), "split60") == 0;
    direct_present = settled + 1 >= settle_frames() && trials < trial_count() && eligible() &&
        (std::strcmp(mode(), "present120") == 0 || ((frozen_present_mode() || motion_mode()) && trials > 0));
    prototype_set_present(direct_present);
    const auto frame_start = std::chrono::steady_clock::now();
    ++race_frame;
    __real_func_8000C560(rdram, ctx);
    // One subsequent normal draw drains the last extra task through the usual
    // BOSS synchronization before exiting. Do not wait on the graphics worker
    // with the guest scheduler locked or consume its completion message twice.
    if (trials == trial_count()) {
        if (shadow_mode()) {
            recomp_context drain_context;
            copy_draw_context(drain_context, *ctx);
            func_800252D0(rdram, &drain_context);
            drain_renderer(rdram);
        }
        finish_present_audit();
        std::lock_guard lock(output_mutex);
        std::fflush(stderr);
        std::_Exit(0);
    }
    if (!eligible()) {
        previous_poses.clear();
        previous_pose_identity.clear();
        settled = 0;
        one_vi = false;
        return;
    }
    const auto scene = word(rdram, 0x800923E4);
    if (++settled < settle_frames()) return;
    if (frozen_present_mode() || motion_mode()) {
        // Activate after the first normal draw returns: its earlier swap
        // presents the preceding warm-up frame. shadow120 presents the current
        // normal frame in the drain below; motion120 suppresses that swap and
        // presents only the subsequent owned quarter steps.
        direct_present = true;
        prototype_set_present(true);
    }
    run_render_trial(rdram, ctx, scene, frame_start, trials);
}
void run_render_trial(uint8_t* rdram, recomp_context* ctx, unsigned scene,
                      std::chrono::steady_clock::time_point frame_start, unsigned& trials) {
    if (hold_accelerator()) {
        std::lock_guard lock(output_mutex);
        log_record("[prototype-input] {\"frame\":%u,\"accelerator_samples\":%u}\n", race_frame, accelerator_samples.load());
    }
    if (shadow_mode()) {
        authoritative_memory = rdram;
        recomp_context drain_context;
        copy_draw_context(drain_context, *ctx);
        func_800252D0(rdram, &drain_context);
        // 252D0 supplies an empty next transform batch, consumes completion
        // through the ROM's usual SP/DP/swap path and clears EDB8. Waiting
        // alone cannot finish a pipelined BOSS task. The renderer then signals
        // after BOSS, held RDP chunks and task cleanup.
        drain_renderer(rdram);
        std::lock_guard lock(render_memory_mutex);
        // Host-owned 8 MiB byte images preserve the native word-swapped
        // layout. Capture on the guest game thread after draining graphics;
        // guest threads are cooperatively scheduled. Only send/receive queue
        // operations use authoritative RAM during a shadow draw. CPU, BOSS
        // and renderer memory share render_ram until its task is drained.
        // Never restore this image into authoritative RAM. Refreshing it is
        // serialized with renderer send_dl/update_screen, including VI reads.
        frozen_ram.assign(rdram, rdram + 0x800000);
        render_ram.resize(0x800000);
    }
    if (shadow_mode()) verify_shadow_receive(rdram, ctx);
    // 11BB8 iterates u32 car count 80080D90 over BA7F0 with stride 18A0.
    // Read-only game-thread observations, raw word-swapped u32s; reject a
    // zero count or any span outside 8 MiB before allocating/reading. Other
    // workers may run during draws; no authoritative words are restored.
    const auto car_count = word(rdram, 0x80080D90);
    if (!car_count || car_count > (0x80800000u - 0x800BA7F0u) / 0x18A0) std::_Exit(8);
    std::vector<uint32_t> all_cars(car_count * (0x18A0 / 4));
    for (unsigned i = 0; i < all_cars.size(); ++i) all_cars[i] = word(rdram, 0x800BA7F0 + 4 * i);
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
    frozen_camera = camera_snapshot;
    // Experimental rigid inputs: camera 00..3C (11D54 -> 1A864), and
    // car+14..50 (218F0 at 21F78 -> 1A97C copies the body matrix).
    // 16 raw word-swapped u32s each; math touches only float basis/position.
    // Bounds follow the full camera/car-span checks above. Game thread owns
    // these host copies; only render_ram receives interpolated words. No
    // authoritative restoration. Optional wheel inputs at +94/+118/+19C/+220
    // are four 64-byte matrices copied by 218F0 at 22254; the full car-span
    // bounds above contain them. All finite rigid-pose checks apply. Invalid
    // poses abort rather than writing guesses. Shadows/effects remain separate.
    std::vector<prototype::Pose> current_poses;
    std::vector<uint32_t> pose_addresses, pose_identity;
    std::vector<PoseKind> pose_kind;
    struct ShadowField { uint32_t address; unsigned words; bool normal; };
    std::vector<ShadowField> shadow_fields;
    std::vector<prototype::Vector3> current_shadow_inputs;
    // 8007DF40 is a fixed in-bounds guest u32 path selector, read with MEM_W
    // on the game thread. Zero selects normal/position; nonzero selects contact
    // points. It is never modified and participates in snapshot identity.
    const bool contact_shadow = shadow_inputs() && word(rdram,0x8007DF40)!=0;
    bool pose_reset = false;
    if (pose_mode()) {
        pose_addresses.push_back(camera);
        pose_kind.push_back(PoseKind::Camera);
        pose_identity = {scene, camera, word(rdram, 0x800ABE40 + 0x74)};
        if (shadow_inputs()) pose_identity.push_back(word(rdram,0x8007DF40));
        for (unsigned i = 0; i < car_count; ++i) {
            const auto car = 0x800BA7F0 + i * 0x18A0;
            pose_addresses.push_back(car + 0x14);
            pose_kind.push_back(PoseKind::Body);
            if (wheel_poses()) for (unsigned wheel = 0; wheel < 4; ++wheel) {
                pose_addresses.push_back(car + 0x94 + wheel*0x84);
                pose_kind.push_back(PoseKind::Wheel);
            }
            pose_identity.push_back(word(rdram, car));
            if (shadow_inputs()) {
                const auto active=word(rdram,car+0x2F4);
                // 17500..17508 copies the current ground-contact reference
                // from +E40; it changes during ordinary driving. 30644..30648
                // only tests presence, so identity tracks availability, not the
                // raw reference. Geometric jump/normal guards remain below.
                pose_identity.push_back(active != 0);
                if(active) {
                    // Float32 ground inputs to 3062C, world-space coordinates/
                    // unit normal. 12 bytes each except alternate-path +36C
                    // uses only XY (8 bytes). Read MEM_W on game thread; write
                    // only the owned bank before the original rebuild. Full
                    // validated car span contains every address; invalid math
                    // aborts. Path/active changes reset history through identity.
                    if(!contact_shadow) shadow_fields.push_back({car+0x360,3,true});
                    shadow_fields.push_back({car+0x36C,contact_shadow?2u:3u,false});
                    if(contact_shadow) for(unsigned contact=0;contact<4;++contact)
                        shadow_fields.push_back({car+0xC6C+contact*12,3,false});
                }
            }
        }
        for (const auto address : pose_addresses) {
            prototype::Pose pose;
            for (unsigned j = 0; j < pose.size(); ++j) pose[j] = word(rdram, address + 4*j);
            prototype::Quaternion rotation;
            if (!prototype::rotation(pose, rotation)) std::_Exit(15);
            current_poses.push_back(pose);
        }
        for(const auto& field:shadow_fields) {
            prototype::Vector3 input{}, checked;
            for(unsigned j=0;j<field.words;++j) input[j]=word(rdram,field.address+4*j);
            if(!prototype::interpolate_vector(input,input,1,field.normal,checked)) std::_Exit(21);
            current_shadow_inputs.push_back(input);
        }
        // Bounded safety policy, not a recovered camera-cut flag: reset on
        // identity/scene changes, sample discontinuities or >64 world units.
        pose_reset = previous_pose_identity != pose_identity || previous_poses.size() != current_poses.size() || previous_shadow_inputs.size() != current_shadow_inputs.size() ||
            samples - previous_pose_sample == 0 || samples - previous_pose_sample > 4;
        const bool identity_changed = previous_pose_identity != pose_identity;
        bool pose_jump = false, shadow_jump = false;
        int identity_index = -1;
        for(unsigned i=0;i<pose_identity.size() && i<previous_pose_identity.size();++i)
            if(pose_identity[i]!=previous_pose_identity[i]) { identity_index=int(i); break; }
        if (!pose_reset) for (unsigned i = 0; i < current_poses.size(); ++i) {
            double distance2 = 0;
            for (unsigned j = 12; j < 15; ++j) {
                const double delta = prototype::component(current_poses[i], j) - prototype::component(previous_poses[i], j);
                distance2 += delta * delta;
            }
            pose_jump |= distance2 > 64*64;
            pose_reset |= pose_jump;
        }
        if (!pose_reset) for(unsigned i=0;i<shadow_fields.size();++i) {
            double distance2=0, dot=0;
            for(unsigned j=0;j<3;++j) {
                const double a=std::bit_cast<float>(previous_shadow_inputs[i][j]);
                const double b=std::bit_cast<float>(current_shadow_inputs[i][j]);
                distance2+=(a-b)*(a-b); dot+=a*b;
            }
            shadow_jump |= shadow_fields[i].normal ? dot<=0 : distance2>64*64;
            pose_reset |= shadow_jump;
        }
        {
            std::lock_guard lock(output_mutex);
            log_record("[prototype-history] {\"frame\":%u,\"reset\":%s,\"identity_changed\":%s,\"identity_index\":%d,\"previous_word\":%u,\"current_word\":%u,\"sample_gap\":%u,\"pose_count_changed\":%s,\"shadow_count_changed\":%s,\"pose_jump\":%s,\"shadow_jump\":%s}\n",
                race_frame, pose_reset?"true":"false", identity_changed?"true":"false", identity_index,
                identity_index<0?0:previous_pose_identity[identity_index], identity_index<0?0:pose_identity[identity_index], samples-previous_pose_sample,
                previous_poses.size()!=current_poses.size()?"true":"false", previous_shadow_inputs.size()!=current_shadow_inputs.size()?"true":"false",
                pose_jump?"true":"false", shadow_jump?"true":"false");
        }
        if (pose_reset) { previous_poses = current_poses; previous_shadow_inputs = current_shadow_inputs; }
    }
    // Reuse the observed 8007E530 consumed-sample u32 (MEM_W, N64 order),
    // measured in original 30 Hz samples on the guest game thread. Only host
    // timeline arithmetic changes; no counter or clock is written. The fixed
    // address lies within 8 MiB; pose_reset handles zero/>4 gaps and identity
    // discontinuities before scheduling. Unsigned subtraction handles wrap.
    prototype::SampleSchedule sample_schedule{};
    if (sample_motion_mode()) {
        static uint64_t sample_timeline = 0;
        if (pose_reset) sample_timeline = 0;
        const unsigned gap = pose_reset ? 1 : samples - previous_pose_sample;
        sample_schedule = prototype::schedule_samples(sample_timeline, sample_timeline + gap,
            present_hz(), (present_hz() + 29) / 30);
        sample_timeline += gap;
        std::lock_guard lock(output_mutex);
        log_record("[prototype-schedule] {\"frame\":%u,\"reset\":%s,\"begin\":%llu,\"end\":%llu,\"first_slot\":%llu,\"count\":%u,\"skipped\":%u}\n",
            race_frame, pose_reset ? "true" : "false", (unsigned long long)sample_schedule.begin,
            (unsigned long long)sample_schedule.end, (unsigned long long)sample_schedule.first_slot,
            sample_schedule.count, sample_schedule.skipped);
    }
    const auto start = std::chrono::steady_clock::now();
    extra = true;
    const unsigned repeats = sample_motion_mode() ? sample_schedule.count : cadence_mode() ? prototype::presentations_in_group(trials, present_hz()) - unsigned(!motion_rate_mode()) : endpoint_mode() ? 3 : motion_mode() ? 4 : direct_present || pose_mode() ? 3 : (std::strcmp(mode(), "repeat") == 0 || render_only() ? 2 : 1);
    unsigned selected_index = 0;
    for (draw_number = 1; draw_number <= repeats; ++draw_number) {
        if (skip_late_draws()) {
            const unsigned next = draw_number == 1 ? 0 : selected_index + 1;
            const uint64_t elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count();
            selected_index = prototype::due_slot(next, repeats, elapsed, present_hz());
            std::lock_guard lock(output_mutex);
            log_record("[prototype-selection] {\"frame\":%u,\"draw\":%u,\"elapsed_ns\":%llu,\"index\":%u,\"slot\":%llu,\"skipped\":%u}\n",
                race_frame, draw_number, (unsigned long long)elapsed, selected_index,
                (unsigned long long)(sample_schedule.first_slot + selected_index), selected_index - next);
        }
        const auto receives_before = shadow_receives, sends_before = shadow_sends;
        const auto wheel_reads_before = wheel_pose_reads;
        const auto shadow_rebuilds_before = shadow_rebuilds;
        unsigned changed_shadow_inputs = 0;
        const auto copies_before = camera_copies;
        const auto active_camera = word(rdram, 0x8007ED6C);
        const auto history_before = preserved_flare_history;
        const auto car_sound_before = skipped_car_sound;
        copy_draw_context(draw_context, *ctx);
        auto* drawing_ram = rdram;
        if (shadow_mode()) {
            PhaseTimer timer{"owned_reset"};
            std::lock_guard lock(render_memory_mutex);
            std::memcpy(render_ram.data(), frozen_ram.data(), frozen_ram.size());
            drawing_ram = render_ram.data();
            if (pose_mode()) {
                // pose-snapshot repeats a midpoint to validate determinism;
                // motion120 presents increasing quarter steps to the endpoint.
                const double alpha = sample_motion_mode() ? sample_schedule.alpha(skip_late_draws() ? selected_index + 1 : draw_number) : motion_rate_mode() ? prototype::presentation_alpha(trials, draw_number, present_hz()) : motion_mode() ? draw_number / 4.0 : draw_number == 3 ? 1.0 : 0.5;
                unsigned changed = 0, changed_bodies = 0, changed_wheels = 0;
                bool camera_changed = false;
                for (unsigned i = 0; i < current_poses.size(); ++i) {
                    prototype::Pose pose;
                    if (!prototype::interpolate_pose(previous_poses[i], current_poses[i],
                            (shadow_only_control() || (wheel_only_control() && pose_kind[i] != PoseKind::Wheel)) ? 1.0 : alpha, pose)) std::_Exit(15);
                    changed += pose != current_poses[i];
                    changed_bodies += pose_kind[i] == PoseKind::Body && pose != current_poses[i];
                    changed_wheels += pose_kind[i] == PoseKind::Wheel && pose != current_poses[i];
                    if (i == 0) camera_changed = pose != current_poses[i];
                    std::memcpy(drawing_ram + (pose_addresses[i] - 0x80000000), pose.data(), sizeof(pose));
                }
                for(unsigned i=0;i<shadow_fields.size();++i) {
                    prototype::Vector3 input;
                    if(!prototype::interpolate_vector(previous_shadow_inputs[i],current_shadow_inputs[i],alpha,shadow_fields[i].normal,input)) std::_Exit(21);
                    changed_shadow_inputs += input!=current_shadow_inputs[i];
                    std::memcpy(drawing_ram+(shadow_fields[i].address-0x80000000),input.data(),shadow_fields[i].words*4);
                }
                std::lock_guard lock(output_mutex);
                log_record("[prototype-pose] {\"frame\":%u,\"draw\":%u,\"alpha\":%.9f,\"reset\":%s,\"poses\":%u,\"changed_poses\":%u,\"changed_camera\":%s,\"changed_bodies\":%u,\"changed_wheels\":%u}\n",
                    race_frame, draw_number, alpha, pose_reset ? "true" : "false", unsigned(current_poses.size()), changed,
                    camera_changed ? "true" : "false", changed_bodies, changed_wheels);
            }
            const char* offset_text = std::getenv("WDC_RENDER_PROTOTYPE_SHADOW_OFFSET");
            const float eye_offset = offset_text ? std::strtof(offset_text, nullptr) : 0.0f;
            if (eye_offset != 0) {
                // Controlled input perturbation in the owned image only.
                // Camera pointer/range was validated above from this snapshot.
                const uint32_t eye_word = word(drawing_ram, camera + 0x30);
                const uint32_t shifted = std::bit_cast<uint32_t>(std::bit_cast<float>(eye_word) + eye_offset);
                std::memcpy(drawing_ram + (camera - 0x80000000) + 0x30, &shifted, 4);
            }
        }
        func_8000A30C(drawing_ram, &draw_context);
        if (shadow_mode()) {
            {
                PhaseTimer timer{"owned_drain"};
                func_800252D0(drawing_ram, &draw_context);
                drain_renderer(rdram);
            }
        }
        if (shadow_mode() && !std::getenv("WDC_RENDER_PROTOTYPE_NO_AUTHORITY_SCAN")) {
            // Observe all authoritative words, without restoring any of them.
            // Runtime queue/input workers legitimately advance during waits;
            // these ranges are attribution evidence, not an equivalence gate.
            PhaseTimer timer{"authority_scan"};
            std::string report = "[prototype-authority] {\"frame\":" + std::to_string(race_frame) +
                ",\"draw\":" + std::to_string(draw_number) + ",\"changed_ranges\":[";
            unsigned count = 0, ranges = 0;
            for (unsigned at = 0; at < 0x800000;) {
                // Preserve the exact word-range report while avoiding scalar
                // reads over unchanged pages. Ranges crossing page boundaries
                // still use the word loop below and remain contiguous.
                if ((at & 0xFFF) == 0 &&
                    std::memcmp(frozen_ram.data() + at, rdram + at, 0x1000) == 0) {
                    at += 0x1000;
                    continue;
                }
                uint32_t before_word, after_word;
                std::memcpy(&before_word, frozen_ram.data() + at, 4);
                std::memcpy(&after_word, rdram + at, 4);
                if (before_word == after_word) { at += 4; continue; }
                const unsigned first = at;
                do {
                    at += 4; ++count;
                    if (at == 0x800000) break;
                    std::memcpy(&before_word, frozen_ram.data() + at, 4);
                    std::memcpy(&after_word, rdram + at, 4);
                } while (before_word != after_word);
                if (ranges++) report += ',';
                report += '[' + std::to_string(0x80000000u + first) + ',' + std::to_string((at - first) / 4) + ']';
            }
            report += "],\"changed_words\":" + std::to_string(count) + "}\n";
            std::lock_guard lock(output_mutex);
            std::fwrite(report.data(), 1, report.size(), stderr);
        }
        if (redirected_camera) {
            // 10A5C publishes the current camera in ED6C for CPU culling.
            // Both u32 references return to their authoritative values after
            // the complete draw; copied camera/arena output stays allocated.
            MEM_W(0, int32_t(0x800ABE40 + 0x2DC)) = redirected_camera;
            MEM_W(0, int32_t(0x8007ED6C)) = active_camera;
            redirected_camera = 0;
        }
        if (shadow_inputs()) {
            std::lock_guard lock(output_mutex);
            log_record("[prototype-shadow-inputs] {\"frame\":%u,\"draw\":%u,\"contact_mode\":%s,\"vectors\":%u,\"changed\":%u,\"rebuilds\":%u}\n",
                race_frame,draw_number,contact_shadow?"true":"false",unsigned(shadow_fields.size()),changed_shadow_inputs,shadow_rebuilds-shadow_rebuilds_before);
        }
        if (wheel_poses()) {
            std::lock_guard lock(output_mutex);
            log_record("[prototype-wheel-reads] {\"frame\":%u,\"draw\":%u,\"copies\":%u}\n", race_frame, draw_number, wheel_pose_reads-wheel_reads_before);
        }
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
        log_record("[prototype-snapshot] {\"frame\":%u,\"draw\":%u,\"context_unchanged\":%s,"
            "\"camera_unchanged\":%s,\"car_nonlink_words_unchanged\":%s,\"sample_unchanged\":%s,"
            "\"matrix_arena_top\":%u,\"geometry_arena_top\":%u,\"next_arena\":%u,\"preserved_flare_calls\":%u,\"view_word_changes\":[",
            race_frame, draw_number, std::memcmp(ctx, caller_snapshot.data(), sizeof(*ctx)) == 0 ? "true" : "false",
            camera_same ? "true" : "false", car_same ? "true" : "false",
            samples == word(rdram, 0x8007E530) ? "true" : "false", word(rdram, 0x8007FE70),
            word(rdram, 0x8007FE74), word(rdram, 0x8007ED94), preserved_flare_history - history_before);
        unsigned changed = 0;
        for (unsigned i = 0; i < views.size(); ++i) {
            auto after = word(rdram, 0x800ABE40 + 4 * i);
            if (after != views[i]) log_record("%s[%u,%u,%u]", changed++ ? "," : "", i * 4, views[i], after);
        }
        log_record("],\"car_render_links\":[");
        for (unsigned i = 0; i < 4; ++i) log_record("%s%u", i ? "," : "", word(rdram, 0x800BA7F0 + 0xB90 + i * 4));
        log_record("],\"car_count\":%u,\"car_count_unchanged\":%s,\"skipped_car_sound_calls\":%u,\"camera_copies\":%u,\"shadow_receives\":%u,\"shadow_sends\":%u,\"all_car_nonlink_changes\":[",
            car_count, word(rdram, 0x80080D90) == car_count ? "true" : "false", skipped_car_sound - car_sound_before, camera_copies - copies_before,
            shadow_receives - receives_before, shadow_sends - sends_before);
        changed = 0;
        for (unsigned i = 0; i < all_cars.size(); ++i) {
            const unsigned offset = (i * 4) % 0x18A0;
            if (offset >= 0xB90 && offset <= 0xB9C) continue;
            const auto after = word(rdram, 0x800BA7F0 + i * 4);
            if (after != all_cars[i]) log_record("%s[%u,%u,%u,%u]",
                changed++ ? "," : "", (i * 4) / 0x18A0, offset, all_cars[i], after);
        }
        log_record("]}\n");
        if (skip_late_draws() && selected_index + 1 == repeats) break;
    }
    draw_number = 0;
    extra = false;
    if (pose_mode()) {
        previous_poses = std::move(current_poses);
        previous_shadow_inputs = std::move(current_shadow_inputs);
        previous_pose_identity = std::move(pose_identity);
        previous_pose_sample = samples;
    }
    one_vi = false;
    if (offset)
        for (unsigned i = 0; i < 3; ++i) MEM_W(int32_t(0x30 + 4 * i), int32_t(camera)) = eye[i];
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    const double frame_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - frame_start).count();
    std::lock_guard output_lock(output_mutex);
    log_record("[render-prototype] {\"trial\":%u,\"frame\":%u,\"gated\":%s,\"scene\":%u,"
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
        log_record("%s{\"offset\":%u,\"before\":%u,\"after\":%u}",
            changed++ ? "," : "", 4 * i, before[i], after);
    }
    log_record("]}\n");
    std::fflush(stderr);
}
}
