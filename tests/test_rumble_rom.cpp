// Real translated detection and pulse timing, with controlled game-mode gates.
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>
#include <array>
#include "controller_pak.h"
#include "rumble.h"
#include "recomp.h"
extern "C" {
void func_8004D450(uint8_t*, recomp_context*);
void func_8004D79C(uint8_t*, recomp_context*);
void func_8004D9B0(uint8_t*, recomp_context*);
void func_8004DCA0(uint8_t*, recomp_context*);
void wdc_motor_init(uint8_t*, recomp_context*);
void wdc_motor_start(uint8_t*, recomp_context*);
void wdc_original_motor_init(uint8_t*, recomp_context*);
void wdc_pfs_init(uint8_t*, recomp_context*);
void wdc_pfs_allocate_file(uint8_t*, recomp_context*);
void wdc_pfs_find_file(uint8_t*, recomp_context*);
void wdc_pfs_read_write_file(uint8_t*, recomp_context*);
void func_80001D4C(uint8_t*, recomp_context* ctx) { ctx->r2 = 1; }
void osCreateMesgQueue_recomp(uint8_t*, recomp_context*) {}
void osRecvMesg_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 0; }
void osSendMesg_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 0; }
void osGetCount_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 123456; }
// A Controller Pak echoes the FE bank probe; the original initializer must
// reject it as a memory accessory after performing its observable OSPfs writes.
void wdc_reference_probe_write(uint8_t* rdram, recomp_context* ctx) {
    assert(ctx->r6 == 0x400 && MEM_BU(0, ctx->r7) == 0xFE);
    ctx->r2 = 0;
}
void wdc_reference_probe_read(uint8_t* rdram, recomp_context* ctx) {
    assert(ctx->r6 == 0x400);
    for (int i = 0; i < 32; ++i) MEM_B(i, ctx->r7) = 0xFE;
    ctx->r2 = 0;
}
void wdc_reference_pack_motor(uint8_t*, recomp_context*) {
    assert(false && "Controller Pak rejection must not reach Rumble Pak transport setup");
}
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
    const gpr stack = ctx.r29;
    const gpr shared_pfs = static_cast<int32_t>(0x800AAC98);
    const gpr queue = static_cast<int32_t>(0x800AAAB0);
    const gpr name = static_cast<int32_t>(0x80400000), ext = name + 32, number = name + 64;
    const gpr buffer = name + 0x1000;
    constexpr int note_size = 0x100;
    wdc::pak::configure(path);
    using namespace ultramodern::input;
    // The real game uses this SAME OSPfs for file calls and motor requests.
    ctx.r4 = queue; ctx.r5 = shared_pfs; ctx.r6 = 0;
    wdc_pfs_init(rdram, &ctx);
    assert(ctx.r2 == 0 && (MEM_W(0, shared_pfs) & 1) != 0);
    auto object_bytes = [&] {
        std::array<uint8_t, 104> bytes{};
        for (int i = 0; i < 104; ++i) bytes[i] = MEM_BU(i, shared_pfs);
        return bytes;
    };
    const auto filesystem = object_bytes();
    // Differential reference: execute the unpatched ROM initializer. Expected
    // writes come from ROM stores 80073C08/18/20/28, not the native adapter.
    ctx.r4 = queue; ctx.r5 = shared_pfs; ctx.r6 = 0;
    wdc_original_motor_init(rdram, &ctx);
    assert(ctx.r2 == 11);
    auto physical_probe = filesystem;
    for (int i = 0; i < 4; ++i) {
        physical_probe[i] = 0;
        physical_probe[4 + i] = (uint32_t(queue) >> (24 - i * 8)) & 255;
        physical_probe[8 + i] = 0;
    }
    physical_probe[0x65] = 0x80;
    assert(object_bytes() == physical_probe);
    // The physical probe invalidated memory-Pak state. Restore it through the
    // actual SDK initializer before exercising the virtual combined accessory.
    ctx.r4 = queue; ctx.r5 = shared_pfs; ctx.r6 = 0;
    wdc_pfs_init(rdram, &ctx);
    assert(ctx.r2 == 0 && object_bytes() == filesystem);
    func_8004D79C(rdram, &ctx); // Original effect initializer enables the worker.
    // Pin the helper's own queue load, rather than depending on leftover a0.
    assert(uint32_t(MEM_W(0, static_cast<int32_t>(0x8004DCC8))) == 0x3C12800Bu);
    assert(uint32_t(MEM_W(0, static_cast<int32_t>(0x8004DCCC))) == 0x2652AAB0u);
    assert(uint32_t(MEM_W(0, static_cast<int32_t>(0x8004DCE4))) == 0x02402025u);
    ctx.r4 = 0x12345678;
    func_8004DCA0(rdram, &ctx);
    assert(object_bytes() == filesystem); // Virtual detection does not run destructive physical probing.
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
    // Allocate and write a real note through the translated filesystem while
    // the effect is running. This exercises its status, channel, bank and ID readers.
    MEM_B(0, name) = 0x1A; MEM_B(0, ext) = 0x1B;
    ctx.r4 = shared_pfs; ctx.r5 = 0x3544; ctx.r6 = 0x4E574F45; ctx.r7 = name;
    MEM_W(0x10, stack) = ext; MEM_W(0x14, stack) = note_size; MEM_W(0x18, stack) = number;
    wdc_pfs_allocate_file(rdram, &ctx);
    assert(ctx.r2 == 0 && MEM_BU(0x65, shared_pfs) == 0);
    const int file_number = MEM_W(0, number);
    for (int i = 0; i < note_size; ++i) MEM_B(i, buffer) = (i + 7) & 255;
    ctx.r4 = shared_pfs; ctx.r5 = file_number; ctx.r6 = 1; ctx.r7 = 0;
    MEM_W(0x10, stack) = note_size; MEM_W(0x14, stack) = buffer;
    wdc_pfs_read_write_file(rdram, &ctx);
    // Storage may outlast the short host lease (e.g. antivirus on CI). Guest
    // effect state is the invariant here; the next worker tick renews feedback.
    assert(ctx.r2 == 0 && MEM_H(0, static_cast<int32_t>(0x800DF468)) == 1);
    wdc::pak::configure(path);
    ctx.r4 = queue; ctx.r5 = shared_pfs; ctx.r6 = 0;
    wdc_pfs_init(rdram, &ctx);
    assert(ctx.r2 == 0 && (MEM_W(0, shared_pfs) & 8) == 0);
    // Filesystem reinitialization does not control the host-only motor state.
    ctx.r4 = shared_pfs;
    wdc_motor_start(rdram, &ctx);
    assert(ctx.r2 == 0 && wdc::rumble::active());
    ctx.r4 = queue; ctx.r5 = shared_pfs; ctx.r6 = 0;
    wdc_motor_init(rdram, &ctx);
    assert(ctx.r2 == 0 && object_bytes() == filesystem);
    ctx.r4 = shared_pfs; ctx.r5 = 0x3544; ctx.r6 = 0x4E574F45; ctx.r7 = name;
    MEM_W(0x10, stack) = ext; MEM_W(0x14, stack) = number;
    wdc_pfs_find_file(rdram, &ctx);
    assert(ctx.r2 == 0 && MEM_W(0, number) == file_number);
    for (int i = 0; i < note_size; ++i) MEM_B(i, buffer) = 0;
    ctx.r4 = shared_pfs; ctx.r5 = file_number; ctx.r6 = 0; ctx.r7 = 0;
    MEM_W(0x10, stack) = note_size; MEM_W(0x14, stack) = buffer;
    wdc_pfs_read_write_file(rdram, &ctx);
    assert(ctx.r2 == 0);
    for (int i = 0; i < note_size; ++i) assert(MEM_BU(i, buffer) == ((i + 7) & 255));
    // Native motor initialization explicitly stopped the host lease. Restore its still-on guest state
    // so the next original stop edge is tested against an active host lease.
    ctx.r4 = shared_pfs;
    wdc_motor_start(rdram, &ctx);
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
