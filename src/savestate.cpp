#include "savestate.h"
#include "recomp.h"
#include <array>
#include <atomic>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

extern "C" uint32_t osGetCount();
extern "C" bool ultramodern_prepare_thread_snapshot(uint8_t*, uint8_t*, uint32_t,
    uint32_t*, uint32_t*, bool);

namespace {
constexpr uint32_t ram_size = 0x800000;
constexpr uint32_t menu_callback = 0x8004E950, race_callback = 0x8000C560;
constexpr unsigned settle_frames = 90;
constexpr int32_t callback_addr = int32_t(0x8007D848), scene_addr = int32_t(0x800923E4);
std::atomic<unsigned> requests{};
std::atomic<uint32_t> clock_bias{};
std::filesystem::path slot, auto_save, auto_load;
unsigned save_delay = 0, load_delay = 0, stable_frames = 0;
uint32_t last_callback{}, last_scene{};

// Local debugging format: little-endian 64-bit hosts, native word-swapped RAM.
// Bump version after incompatible hook/runtime/layout changes. Native thread
// pointers are zeroed in the file and replaced only after validating the roster.
struct Header {
    char magic[8];
    uint32_t version, size, callback, scene, stack, count, clock, checksum;
    std::array<uint32_t, 32> threads;
};
static_assert(sizeof(Header) == 168);

uint32_t checksum(const uint8_t* data, size_t size, uint32_t result = 2166136261u) {
    for (size_t i = 0; i < size; ++i) result = (result ^ data[i]) * 16777619u;
    return result;
}
bool fail(const char* operation, const char* reason) {
    std::fprintf(stderr, "[wdc:state] %s failed: %s\n", operation, reason);
    return false;
}
bool delay(const char* name, unsigned& result) {
    const char* raw = std::getenv(name);
    if (!raw) return true;
    const std::string_view text(raw);
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || text.front() < '0' || text.front() > '9' || error != std::errc{} ||
        end != text.data() + text.size() || result > 1000000) {
        std::fprintf(stderr, "[wdc:state] invalid %s; expected frames 0..1000000\n", name);
        return false;
    }
    return true;
}
std::filesystem::path environment_path(const char* name, std::filesystem::path fallback = {}) {
    const char* value = std::getenv(name);
    return value && *value ? std::filesystem::path(value) : fallback;
}
bool save(uint8_t* rdram, recomp_context* ctx, const std::filesystem::path& path) {
    std::vector<uint8_t> image(rdram, rdram + ram_size);
    Header h{};
    std::memcpy(h.magic, "WDCSTATE", 8);
    h.version = 1; h.size = ram_size;
    h.callback = MEM_W(0, callback_addr); h.scene = MEM_W(0, scene_addr);
    h.stack = uint32_t(ctx->r29); h.clock = osGetCount() - clock_bias.load();
    h.count = uint32_t(h.threads.size());
    if (!ultramodern_prepare_thread_snapshot(rdram, image.data(), ram_size,
            h.threads.data(), &h.count, false)) return fail("save", "unsupported live thread roster");
    h.checksum = checksum(image.data(), image.size(), checksum(reinterpret_cast<const uint8_t*>(&h), sizeof(h)));
    auto temporary = path; temporary += ".tmp";
#ifdef _WIN32
    FILE* file = _wfopen(temporary.c_str(), L"wb");
#else
    FILE* file = std::fopen(temporary.c_str(), "wb");
#endif
    if (!file) return fail("save", "cannot open temporary file (check slot directory)");
    bool ok = std::fwrite(&h, 1, sizeof(h), file) == sizeof(h) &&
        std::fwrite(image.data(), 1, image.size(), file) == image.size();
    if (std::fflush(file) != 0) ok = false;
    if (std::fclose(file) != 0) ok = false;
    if (!ok) return fail("save", "write/close failed; previous slot retained");
#ifdef _WIN32
    ok = MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    ok = std::rename(temporary.c_str(), path.c_str()) == 0;
#endif
    if (!ok) return fail("save", "publish failed; previous slot retained, new image left in .tmp");
    std::fprintf(stderr, "[wdc:state] saved %u bytes: %s (scene=%u)\n", ram_size, path.string().c_str(), h.scene);
    return true;
}
bool load(uint8_t* rdram, recomp_context* ctx, const std::filesystem::path& path) {
#ifdef _WIN32
    FILE* file = _wfopen(path.c_str(), L"rb");
#else
    FILE* file = std::fopen(path.c_str(), "rb");
#endif
    if (!file) return fail("load", "cannot open slot");
    Header h{};
    bool ok = std::fread(&h, 1, sizeof(h), file) == sizeof(h) &&
        std::memcmp(h.magic, "WDCSTATE", 8) == 0 && h.version == 1 && h.size == ram_size &&
        h.count > 0 && h.count <= h.threads.size();
    std::vector<uint8_t> image;
    if (ok) {
        const uint32_t expected_checksum = h.checksum;
        h.checksum = 0;
        image.resize(ram_size);
        ok = std::fread(image.data(), 1, image.size(), file) == image.size() &&
            std::fgetc(file) == EOF && !std::ferror(file) &&
            checksum(image.data(), image.size(), checksum(reinterpret_cast<const uint8_t*>(&h), sizeof(h))) == expected_checksum;
    }
    std::fclose(file);
    if (!ok) return fail("load", "invalid, incompatible, corrupt or truncated snapshot; RAM unchanged");
    uint8_t* saved = image.data();
    if (h.callback != uint32_t(MEM_W(0, callback_addr)) || h.scene != uint32_t(MEM_W(0, scene_addr)) ||
        h.stack != uint32_t(ctx->r29) ||
        h.callback != uint32_t(*reinterpret_cast<int32_t*>(saved + 0x7D848)) ||
        h.scene != uint32_t(*reinterpret_cast<int32_t*>(saved + 0x923E4)))
        return fail("load", "enter the saved scene at the same dispatcher first; RAM unchanged");
    // Compare the loader identity, not the entire 0xA0-byte live race config:
    // +00..27 = participant counts/model grid; +70..87 = course/layout,
    // mirror, course variant, laps and mode (native constructor 80068AA8).
    // Other words change during racing, e.g. +68 at 8004CC00/14; requiring
    // equality there rejects ordinary F7/F8 recovery in the same race.
    if (h.callback == race_callback && (std::memcmp(saved + 0x92360, rdram + 0x92360, 0x28) ||
            std::memcmp(saved + 0x923D0, rdram + 0x923D0, 0x18)))
        return fail("load", "enter the same race setup first (WDC_WARP); RAM unchanged");
    // The worker and host frame-rate corrections latch their mode at startup.
    // Do not restore a different divider (u32) or poll interval (u64 high/low
    // words) under the live native worker. See game_framerate.cpp's contract.
    if (h.callback == race_callback && (std::memcmp(saved + 0x7FE1C, rdram + 0x7FE1C, 4) ||
            std::memcmp(saved + 0x7FDF0, rdram + 0x7FDF0, 8)))
        return fail("load", "start the race with the saved frame-rate mode first; RAM unchanged");
    if (!ultramodern_prepare_thread_snapshot(rdram, saved, ram_size,
            h.threads.data(), &h.count, true))
        return fail("load", "live thread/queue layout differs; retry at a settled frame; RAM unchanged");
    // No guest scheduling call between pointer repair, the copy and clock
    // publication. Other guest threads remain parked by the cooperative runtime.
    std::memcpy(rdram, saved, ram_size);
    clock_bias.store(osGetCount() - h.clock);
    stable_frames = 0;
    std::fprintf(stderr, "[wdc:state] loaded %u bytes: %s (scene=%u)\n", ram_size, path.string().c_str(), h.scene);
    return true;
}
}

bool wdc::state::configure(const std::filesystem::path& default_slot) {
    slot = environment_path("WDC_STATE_FILE", default_slot);
    auto_save = environment_path("WDC_STATE_SAVE");
    auto_load = environment_path("WDC_STATE_LOAD");
    return delay("WDC_STATE_SAVE_DELAY", save_delay) && delay("WDC_STATE_LOAD_DELAY", load_delay);
}
void wdc::state::request_save() {
    requests.fetch_or(1);
    std::fprintf(stderr, "[wdc:state] save requested; waiting for a settled frame\n");
}
void wdc::state::request_load() {
    requests.fetch_or(2);
    std::fprintf(stderr, "[wdc:state] load requested; waiting for a settled frame\n");
}

// USA func_80001D88 before 80001D90 reloads the callback. Game dispatcher
// thread only; low 8 MiB RDRAM at 80000000..807FFFFF, raw word-swapped bytes.
// The u32 callback/scene and fade words are aligned N64 big-endian via MEM_W.
// Require 90 consecutive menu/race callbacks with no fade-out (8007DF44).
// This is a conservative heuristic, not a proof that native readers are idle.
// No guest pointer survives the hook. Bad/null contexts and loaders reset the
// settle gate without consuming requests. Replace with named scene checkpoints
// when decompilation exposes frame/worker state; see docs/debugging.md.
extern "C" void wdc_savestate_tick(uint8_t* rdram, recomp_context* ctx) {
    if (!rdram || !ctx) { stable_frames = 0; return; }
    const uint32_t callback = MEM_W(0, callback_addr), scene = MEM_W(0, scene_addr);
    if ((callback != menu_callback && callback != race_callback) ||
        MEM_W(0, int32_t(0x8007DF44)) ||
        uint32_t(ctx->r29) < 0x80000000 || uint32_t(ctx->r29) >= 0x80800000) {
        stable_frames = 0;
        return;
    }
    if (callback != last_callback || scene != last_scene) stable_frames = 0;
    last_callback = callback; last_scene = scene;
    if (stable_frames < settle_frames) ++stable_frames;
    if (stable_frames < settle_frames) return;
    try {
        if (!auto_load.empty() && callback == race_callback && !load_delay--) {
            auto path = std::move(auto_load); auto_load.clear();
            load(rdram, ctx, path);
            return;
        }
        if (!auto_save.empty() && callback == race_callback && !save_delay--) {
            auto path = std::move(auto_save); auto_save.clear();
            save(rdram, ctx, path);
            return;
        }
        const unsigned request = requests.exchange(0);
        if (request & 2) load(rdram, ctx, slot);
        else if (request & 1) save(rdram, ctx, slot);
    } catch (const std::exception& error) {
        fail("operation", error.what());
    }
}

// All translated osGetCount callers use the same virtual 32-bit count. Restore
// its epoch along with RAM so timing anchors do not age by the elapsed wall
// time or another process's epoch. No guest-memory access; guest threads only.
extern "C" void wdc_get_count(uint8_t*, recomp_context* ctx) {
    ctx->r2 = int32_t(osGetCount() - clock_bias.load());
}
