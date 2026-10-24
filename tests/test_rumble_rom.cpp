// Real translated detection and pulse timing, with controlled game-mode gates.
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include "controller_pak.h"
#include "rumble.h"
#include "recomp.h"
extern "C" {
void func_8004D450(uint8_t*, recomp_context*);
void func_8004D79C(uint8_t*, recomp_context*);
void func_8004D9B0(uint8_t*, recomp_context*);
void func_8004DCA0(uint8_t*, recomp_context*);
void wdc_motor_init(uint8_t*, recomp_context*);
void wdc_pak_read(uint8_t*, recomp_context*);
void wdc_pak_write(uint8_t*, recomp_context*);
void func_80001D4C(uint8_t*, recomp_context* ctx) { ctx->r2 = 1; }
}
int main(int argc, char** argv) {
    assert(argc == 2);
    const auto path = std::filesystem::temp_directory_path() /
        ("wdc-rumble-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".mpk");
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    std::ifstream rom(argv[1], std::ios::binary);
    assert(rom);
    rom.seekg(0x1000);
    // Entrypoint 80000400 clears BSS from 800A2BE0 for 0x40C70 bytes.
    // Load only the initialized image so absent-port flags start as at boot.
    std::vector<char> data(0xA2BE0 - 0x400);
    rom.read(data.data(), data.size());
    assert(rom);
    for (size_t i = 0; i < data.size(); ++i)
        MEM_B(i, static_cast<int32_t>(0x80000400)) = data[i];
    recomp_context ctx{};
    ctx.r29 = static_cast<int32_t>(0x807FF000);
    wdc::pak::configure(path);
    using namespace ultramodern::input;
    func_8004D79C(rdram, &ctx); // Original effect initializer enables the worker.
    func_8004DCA0(rdram, &ctx); // Original detection initializes motor-only PFS.
    assert(MEM_BU(0, static_cast<int32_t>(0x800DF460)) == 1);
    assert(MEM_BU(0, static_cast<int32_t>(0x800DF461)) == 0);
    assert(wdc::pak::device_info(0).connected_pak == Pak::ControllerPak);
    ctx.r5 = static_cast<int32_t>(0x800AAAF8); ctx.r6 = 0;
    wdc_motor_init(rdram, &ctx);
    assert(ctx.r2 == 11); // The save/menu probe must still see a memory device.
    // Active race predicates, independent of the actual save availability.
    MEM_W(0, static_cast<int32_t>(0x8007D858)) = 0;
    MEM_W(0, static_cast<int32_t>(0x8007D794)) = 0;
    MEM_W(0, static_cast<int32_t>(0x8007DF44)) = 0;
    MEM_W(0, static_cast<int32_t>(0x800923E8)) = 0;
    // ROM request API: player, initial delay, initial motor, on ticks, off ticks,
    // total ticks. Exercise the real timer rather than calling the host motor.
    ctx.r4 = 0; ctx.r5 = 0; ctx.r6 = 0; ctx.r7 = 1;
    MEM_W(0x10, ctx.r29) = 1; MEM_W(0x14, ctx.r29) = 6;
    func_8004D9B0(rdram, &ctx);
    func_8004D450(rdram, &ctx);
    assert(wdc::rumble::active());
    // Store a block while the original effect is running, then reopen it.
    ctx.r5 = 0; ctx.r6 = 0x3FF; ctx.r7 = static_cast<int32_t>(0x80400000);
    for (int i = 0; i < 32; ++i) MEM_B(i, ctx.r7) = i + 7;
    wdc_pak_write(rdram, &ctx);
    // Storage may outlast the short host lease (e.g. antivirus on CI). Guest
    // effect state is the invariant here; the next worker tick renews feedback.
    assert(ctx.r2 == 0 && MEM_H(0, static_cast<int32_t>(0x800DF468)) == 1);
    wdc::pak::configure(path);
    wdc_pak_read(rdram, &ctx);
    assert(ctx.r2 == 0);
    for (int i = 0; i < 32; ++i) assert(MEM_BU(i, ctx.r7) == i + 7);
    func_8004D450(rdram, &ctx); // on countdown
    assert(wdc::rumble::active());
    func_8004D450(rdram, &ctx); // stop edge
    assert(!wdc::rumble::active());
    for (int i = 0; i < 12; ++i) func_8004D450(rdram, &ctx);
    assert(!wdc::rumble::active() && MEM_H(0, static_cast<int32_t>(0x800DF464)) == 0);
    ctx.r4 = 0; ctx.r5 = 0; ctx.r6 = 0; ctx.r7 = 2;
    MEM_W(0x10, ctx.r29) = 1; MEM_W(0x14, ctx.r29) = 100;
    func_8004D9B0(rdram, &ctx);
    func_8004D450(rdram, &ctx);
    assert(wdc::rumble::active());
    MEM_W(0, static_cast<int32_t>(0x8007D858)) = 1; // original early-out gate
    std::this_thread::sleep_for(std::chrono::milliseconds(180));
    func_8004D450(rdram, &ctx);
    assert(!wdc::rumble::active()); // Gated workers cannot renew a held motor.
    MEM_W(0, static_cast<int32_t>(0x8007D858)) = 0;
    func_8004D450(rdram, &ctx);
    assert(wdc::rumble::active());
    func_8004D79C(rdram, &ctx); // Resetting an effect must cancel host feedback.
    func_8004D450(rdram, &ctx);
    assert(!wdc::rumble::active());
    std::filesystem::remove(path);
}
