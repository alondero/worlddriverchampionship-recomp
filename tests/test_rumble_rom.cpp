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
#include "ultramodern/ultra64.h"
namespace {
unsigned si_completions = 0;
}
// Keep the production query implementation; only its SI completion delivery is
// synchronous in this bounded fixture, like the SDK queue stubs below.
namespace ultramodern {
void send_si_message() { ++si_completions; }
}
extern "C" {
void func_8004D450(uint8_t*, recomp_context*);
void func_8004D79C(uint8_t*, recomp_context*);
void func_8004D9B0(uint8_t*, recomp_context*);
void func_8004D854(uint8_t*, recomp_context*);
void func_8004D968(uint8_t*, recomp_context*);
void func_80001D70(uint8_t*, recomp_context*);
void wdc_motor_init(uint8_t*, recomp_context*);
void wdc_motor_start(uint8_t*, recomp_context*);
void wdc_original_motor_init(uint8_t*, recomp_context*);
void wdc_pfs_init(uint8_t*, recomp_context*);
void wdc_pfs_allocate_file(uint8_t*, recomp_context*);
void wdc_pfs_find_file(uint8_t*, recomp_context*);
void wdc_pfs_read_write_file(uint8_t*, recomp_context*);
void wdc_pfs_num_files(uint8_t*, recomp_context*);
void func_800742D0(uint8_t*, recomp_context*); // osPfsDeleteFile, reached from the Pak menu
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
    const gpr status = static_cast<int32_t>(0x800AAAD0);
    const gpr snapshot = static_cast<int32_t>(0x800DF480);
    const gpr name = static_cast<int32_t>(0x80400000), ext = name + 32, number = name + 64;
    const gpr buffer = name + 0x1000;
    constexpr int note_size = 0x100;
    wdc::pak::configure(path);
    using namespace ultramodern::input;
    callbacks_t callbacks{};
    callbacks.get_connected_device_info = wdc::pak::device_info;
    set_callbacks(callbacks);
    auto instruction = [&](uint32_t pc, uint32_t expected) {
        assert(uint32_t(MEM_W(0, static_cast<int32_t>(pc))) == expected);
    };
    auto jal = [&](uint32_t pc, uint32_t target) {
        instruction(pc, 0x0C000000u | ((target >> 2) & 0x03FFFFFFu));
    };
    // Pin the BSS clear's bounds and stores: both status and snapshot begin at
    // zero, but osContInit populates status before the first worker snapshot.
    instruction(0x80000400, 0x3C08800A); instruction(0x80000408, 0x25082BE0);
    instruction(0x80000404, 0x3C090004); instruction(0x8000040C, 0x35290C70);
    instruction(0x80000410, 0x2129FFF8); instruction(0x80000414, 0xAD000000);
    instruction(0x80000418, 0xAD000004); instruction(0x8000041C, 0x1520FFFC);
    instruction(0x80000420, 0x21080008);
    assert(MEM_W(0, status) == 0 && MEM_W(0, snapshot) == 0 && MEM_W(4, snapshot) == 0);
    // Main startup calls controller setup before dispatching 8004E8BC, whose
    // 8004DE04 call constructs the input worker via 8002CB0C.
    jal(0x80002788, 0x80001A9C); jal(0x800027A4, 0x80001D70);
    instruction(0x800027A0, 0x3C048005); instruction(0x800027A8, 0x2484E8BC);
    jal(0x8004E8E8, 0x8004DE04); jal(0x8004E128, 0x8002CB0C);
    instruction(0x80001C4C, 0x3C06800B); instruction(0x80001C50, 0x24C6AAD0);
    jal(0x80001C58, 0x8006F970); // osContInit(..., status=800AAAD0)
    uint8_t pattern = 0;
    assert(osContInit(rdram, int32_t(queue), &pattern, int32_t(status)) == 0 && pattern == 1);
    auto check_status = [&] {
        assert(MEM_HU(0, status) == 0x0005);
        assert(MEM_BU(0, status) == 0 && MEM_BU(1, status) == 5);
        assert(MEM_BU(2, status) == 1 && MEM_BU(3, status) == 0);
        for (int port = 1; port < 4; ++port) assert(MEM_BU(port * 4 + 3, status) == 8);
    };
    check_status();
    ctx.r4 = static_cast<int32_t>(0x8004E8BC);
    func_80001D70(rdram, &ctx); // Same startup callback installed at 800027A4.
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
    // Execute the boot/query sequence used by the live worker.
    // No native motor init or standalone 4DCA0 helper has run in this process.
    jal(0x8002CCB8, 0x8004D79C);
    jal(0x8002C99C, 0x8004D968); jal(0x8002C9A4, 0x80073ED0);
    jal(0x8002C9B4, 0x8006ED70); // osRecvMesg between query start and result
    jal(0x8002C9C0, 0x80073F54); jal(0x8002C9CC, 0x8004D854);
    jal(0x8002C9D4, 0x8004D450); jal(0x8004D904, 0x80073BEC);
    jal(0x8004D5C8, 0x80073BEC);
    // Detector queue construction: lui/addiu write s7, then move a0,s7.
    instruction(0x8004D884, 0x3C17800B);
    instruction(0x8004D8A0, 0x26F7AAB0); instruction(0x8004D8E0, 0x02E02025);
    assert(MEM_W(0, snapshot) == 0 && MEM_W(4, snapshot) == 0);
    ctx.r4 = status;
    func_8004D968(rdram, &ctx); // Captures present port 0, absent port 1.
    assert(MEM_W(0, snapshot) == 1 && MEM_W(4, snapshot) == 0);
    assert(wdc::pak::device_info(0).connected_pak == Pak::ControllerPak);
    assert(osContStartQuery(rdram, int32_t(queue)) == 0 && si_completions == 1);
    osContGetQuery(rdram, int32_t(status)); // Real runtime source, configured accessory callback.
    check_status();
    ctx.r4 = status;
    func_8004D854(rdram, &ctx); // No transition at boot: this must NOT initialize.
    assert(object_bytes() == filesystem); // Virtual detection does not run destructive physical probing.
    assert(MEM_BU(0, static_cast<int32_t>(0x800DF460)) == 0);
    assert(MEM_BU(0, static_cast<int32_t>(0x800DF461)) == 0);
    assert(wdc::pak::device_info(0).connected_pak == Pak::ControllerPak);
    // Before active gameplay, the timer's separate mode gate reaches Init at
    // 8004D5C8 even without a status transition. The real predicate compares
    // the current callback (8004E8BC at startup) with gameplay callback 8000C560.
    MEM_W(0, static_cast<int32_t>(0x8007D858)) = 0;
    MEM_W(0, static_cast<int32_t>(0x8007D794)) = 0;
    MEM_W(0, static_cast<int32_t>(0x8007DF44)) = 0;
    MEM_W(0, static_cast<int32_t>(0x800923E8)) = 0;
    assert(MEM_BU(0, static_cast<int32_t>(0x80092DE0)) == 0);
    assert(MEM_W(0, static_cast<int32_t>(0x80092DEC)) == 0);
    func_8004D450(rdram, &ctx);
    assert(MEM_BU(0, static_cast<int32_t>(0x800DF460)) == 1);
    assert(MEM_BU(0, static_cast<int32_t>(0x800DF461)) == 0);
    assert(MEM_W(0, static_cast<int32_t>(0x80092DEC)) == 1);
    assert(object_bytes() == filesystem && !wdc::rumble::active());
    ctx.r4 = static_cast<int32_t>(0x8000C560);
    func_80001D70(rdram, &ctx); // Enter active gameplay for the effect ticks.
    // ROM request API: player, initial delay, initial motor, on ticks, off ticks,
    // total ticks. Exercise the real timer rather than calling the host motor.
    ctx.r4 = 0; ctx.r5 = 0; ctx.r6 = 0; ctx.r7 = 1;
    MEM_W(0x10, ctx.r29) = 1; MEM_W(0x14, ctx.r29) = 6;
    func_8004D9B0(rdram, &ctx);
    func_8004D450(rdram, &ctx);
    assert(wdc::rumble::active()); // First successful start follows only reachable worker callers.
    assert((MEM_W(0, shared_pfs) & 8) == 0 && MEM_BU(0x65, shared_pfs) == 0);
    ctx.r5 = static_cast<int32_t>(0x800AAAF8); ctx.r6 = 0;
    wdc_motor_init(rdram, &ctx);
    assert(ctx.r2 == 11); // The auxiliary menu probe must still see a memory device.
    // Allocate and write a real note through the translated filesystem while
    // the effect is running. This exercises its status, channel, bank and ID readers.
    MEM_B(0, name) = 0x1A; MEM_B(0, ext) = 0x1B;
    ctx.r4 = shared_pfs; ctx.r5 = 0x3544; ctx.r6 = 0x4E574F45; ctx.r7 = name;
    MEM_W(0x10, stack) = int32_t(ext); MEM_W(0x14, stack) = note_size; MEM_W(0x18, stack) = int32_t(number);
    wdc_pfs_allocate_file(rdram, &ctx);
    assert(ctx.r2 == 0 && MEM_BU(0x65, shared_pfs) == 0);
    const int file_number = MEM_W(0, number);
    for (int i = 0; i < note_size; ++i) MEM_B(i, buffer) = (i + 7) & 255;
    ctx.r4 = shared_pfs; ctx.r5 = file_number; ctx.r6 = 1; ctx.r7 = 0;
    MEM_W(0x10, stack) = note_size; MEM_W(0x14, stack) = int32_t(buffer);
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
    MEM_W(0x10, stack) = int32_t(ext); MEM_W(0x14, stack) = int32_t(number);
    wdc_pfs_find_file(rdram, &ctx);
    assert(ctx.r2 == 0 && MEM_W(0, number) == file_number);
    for (int i = 0; i < note_size; ++i) MEM_B(i, buffer) = 0;
    ctx.r4 = shared_pfs; ctx.r5 = file_number; ctx.r6 = 0; ctx.r7 = 0;
    MEM_W(0x10, stack) = note_size; MEM_W(0x14, stack) = int32_t(buffer);
    wdc_pfs_read_write_file(rdram, &ctx);
    assert(ctx.r2 == 0);
    for (int i = 0; i < note_size; ++i) assert(MEM_BU(i, buffer) == ((i + 7) & 255));
    // Refresh after disk I/O; the next original stop edge must stop an active lease.
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
    // Exercise the menu's delete/count path with no synthetic motor status bit.
    ctx.r4 = shared_pfs; ctx.r5 = 0x3544; ctx.r6 = 0x4E574F45; ctx.r7 = name;
    MEM_W(0x10, stack) = int32_t(ext);
    func_800742D0(rdram, &ctx);
    assert(ctx.r2 == 0);
    ctx.r4 = shared_pfs; ctx.r5 = number; ctx.r6 = number + 4;
    wdc_pfs_num_files(rdram, &ctx);
    assert(ctx.r2 == 0 && MEM_W(4, number) == 0);
    assert((MEM_W(0, shared_pfs) & 8) == 0 && MEM_BU(0x65, shared_pfs) == 0);
    std::filesystem::remove(path);
}
