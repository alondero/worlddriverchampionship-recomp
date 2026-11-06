// Real slot I/O and runtime registry over synthetic word-swapped RDRAM.
#include "savestate.h"
#include "recomp.h"
#include "ultramodern/thread_snapshot.hpp"
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" void wdc_savestate_tick(uint8_t*, recomp_context*);
extern "C" void wdc_get_count(uint8_t*, recomp_context*);
static ultramodern::ThreadSnapshotRegistry registry;
static uint32_t host_count = 1000;
extern "C" uint32_t osGetCount() { return host_count; }
extern "C" bool ultramodern_prepare_thread_snapshot(uint8_t* ram, uint8_t* image,
        uint32_t size, uint32_t* addresses, uint32_t* count, bool restore) {
    return registry.prepare(ram, image, size, addresses, count, restore);
}
static void require(bool ok, const char* message) {
    if (!ok) throw std::runtime_error(message);
}
static void env(const char* name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    if (value.empty()) unsetenv(name); else setenv(name, value.c_str(), 1);
#endif
}
static std::vector<uint8_t> read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
static void write(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
int main(int argc, char** argv) {
    auto directory = std::filesystem::temp_directory_path() / ("wdc-state-test-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    try {
        for (auto name : {"WDC_STATE_FILE", "WDC_STATE_SAVE", "WDC_STATE_LOAD",
                "WDC_STATE_SAVE_DELAY", "WDC_STATE_LOAD_DELAY"}) env(name, "");
        const auto slot = directory / "slot.wstate";
        if (argc > 1 && std::string(argv[1]) == "invalid") {
            for (const auto value : {"-1", "", "4x", "1000001", "4294967296", "+1"}) {
                if (!*value) continue; // Windows removes empty environment values.
                env("WDC_STATE_SAVE_DELAY", value);
                require(!wdc::state::configure(slot), "accepted invalid save delay");
            }
            env("WDC_STATE_SAVE_DELAY", "0"); env("WDC_STATE_LOAD_DELAY", "bad");
            require(!wdc::state::configure(slot), "accepted invalid load delay");
            std::filesystem::remove_all(directory);
            return 0;
        }
        const bool automatic = argc > 1 && std::string(argv[1]) == "automatic";
        const bool automatic_load = argc > 1 && std::string(argv[1]) == "load";
        if (automatic_load) {
            env("WDC_STATE_LOAD", slot.string());
            env("WDC_STATE_LOAD_DELAY", "2");
        } else if (automatic) {
            env("WDC_STATE_SAVE", slot.string());
            env("WDC_STATE_SAVE_DELAY", "2");
        }
        require(wdc::state::configure(slot), "configuration failed");
        std::vector<uint8_t> memory(0x800000);
        uint8_t* rdram = memory.data();
        recomp_context ctx{}; ctx.r29 = int32_t(0x800FF000); ctx.r16 = 42;
        auto* thread = reinterpret_cast<OSThread*>(rdram + 0x1000);
        thread->id = 9; thread->sp = int32_t(0x80100000);
        // These stand-ins are never dereferenced by the registry.
        auto* first = reinterpret_cast<UltraThreadContext*>(uintptr_t(0x12340000));
        auto* second = reinterpret_cast<UltraThreadContext*>(uintptr_t(0x98760000));
        thread->context = first;
        registry.add(0x80001000, first);
        const auto tick = [&](int frames) { while (frames--) wdc_savestate_tick(rdram, &ctx); };
        if (!automatic) wdc::state::request_save();
        tick(120);
        require(!std::filesystem::exists(slot), "saved during boot");
        MEM_W(0, int32_t(0x8007D848)) = int32_t(0x8000C560);
        MEM_W(0, int32_t(0x800923E4)) = 8;
        memory[0x2000] = 73;
        tick(89);
        require(!std::filesystem::exists(slot), "saved before settle window");
        tick(automatic ? 3 : 1);
        require(std::filesystem::exists(slot), "settled save not written");
        const auto good = read(slot);
        require(good.size() == 168 + memory.size(), "wrong snapshot length");
        const auto pointer_offset = 168 + 0x1000 + offsetof(OSThread, context);
        for (size_t i = 0; i < sizeof(void*); ++i)
            require(good[pointer_offset + i] == 0, "serialized native pointer");
        require(thread->context == first, "save touched live pointer");
        memory[0x2000] = 99;
        if (automatic_load) {
            tick(2);
            require(memory[0x2000] == 73, "automatic load not applied");
            memory[0x2000] = 99; tick(150);
            require(memory[0x2000] == 99, "automatic load repeated");
        }
        if (automatic) {
            tick(150);
            require(read(slot) == good, "automatic save repeated");
        }
        // Model another process: replacement context at the same guest address.
        registry.add(0x80001000, second); thread->context = second;
        registry.remove(0x80001000, first); // stale cleanup must not erase replacement
        host_count = 0xFFFFFF00;
        // Native racing changes +68 after a settled save (8004CC00/14).
        // Full-config comparison falsely rejected F8 three seconds after F7.
        memory[0x923C8] = 1;
        memory[0x923EC] = 2;
        const auto registers = ctx;
        wdc::state::request_load(); tick(90);
        require(memory[0x2000] == 73, "load did not restore world");
        require(memory[0x923C8] == 0 && memory[0x923EC] == 0, "live race fields blocked restoration");
        require(thread->context == second, "load restored stale native context");
        require(std::memcmp(&registers, &ctx, sizeof(ctx)) == 0, "hook changed registers");
        auto clock = ctx; wdc_get_count(nullptr, &clock);
        require(uint32_t(clock.r2) == 1000, "count epoch not restored");
        host_count += 500; wdc_get_count(nullptr, &clock);
        require(uint32_t(clock.r2) == 1500, "count wrap or post-restore rate broken");
        // Both bits set: load wins and must not overwrite the good checkpoint.
        memory[0x2000] = 13;
        wdc::state::request_save(); wdc::state::request_load(); tick(90);
        require(memory[0x2000] == 73 && read(slot) == good, "save beat load");
        const auto reject = [&] {
            const auto before = memory;
            auto old_clock = ctx; wdc_get_count(nullptr, &old_clock);
            wdc::state::request_load(); tick(90);
            require(memory == before, "rejected load changed RAM");
            auto new_clock = ctx; wdc_get_count(nullptr, &new_clock);
            require(new_clock.r2 == old_clock.r2, "rejected load changed clock");
        };
        auto bad = good; bad[0] ^= 1; write(slot, bad); reject();
        bad = good; bad[8] = 99; write(slot, bad); reject();
        bad = good; bad[32] ^= 1; write(slot, bad); reject();
        bad = good; bad.back() ^= 1; write(slot, bad); reject();
        bad = good; bad.resize(200); write(slot, bad); reject();
        bad = good; bad.push_back(0); write(slot, bad); reject();
        std::filesystem::remove(slot); reject();
        write(slot, good);
        MEM_W(0, int32_t(0x800923E4)) = 9; reject();
        MEM_W(0, int32_t(0x800923E4)) = 8;
        for (const unsigned offset : {0u, 4u, 8u, 0x24u, 0x70u, 0x74u, 0x78u, 0x7Cu, 0x80u, 0x84u}) {
            const auto original = memory[0x92360 + offset];
            memory[0x92360 + offset] ^= 1; reject(); memory[0x92360 + offset] = original;
        }
        memory[0x7FE1C] = 1; reject(); memory[0x7FE1C] = 0;
        memory[0x7FDF0] = 1; reject(); memory[0x7FDF0] = 0;
        thread->queue = int32_t(0x80003000); reject(); thread->queue = 0;
        registry.remove(0x80001000, second); reject(); registry.add(0x80001000, second);
        // A failed temporary-file open must preserve the last working slot.
        std::filesystem::create_directory(slot.string() + ".tmp");
        wdc::state::request_save(); tick(90);
        require(read(slot) == good, "failed save damaged slot");
        std::filesystem::remove(slot.string() + ".tmp");
        // Pending requests survive fade and the settle interval resets.
        memory[0x2000] = 88; MEM_W(0, int32_t(0x8007DF44)) = 1;
        wdc::state::request_load(); tick(120);
        require(memory[0x2000] == 88, "load fired during fade");
        MEM_W(0, int32_t(0x8007DF44)) = 0; tick(89);
        require(memory[0x2000] == 88, "fade did not reset settle window");
        tick(1); require(memory[0x2000] == 73, "pending load lost");
        std::filesystem::remove_all(directory);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "savestate test: %s\n", error.what());
        std::filesystem::remove_all(directory);
        return 1;
    }
    return 0;
}
