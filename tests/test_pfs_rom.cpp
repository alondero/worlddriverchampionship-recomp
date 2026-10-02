// Exercise WDC's translated SDK filesystem and reset all RAM/cache state to
// simulate a fresh launch. No filesystem function is mocked.
#undef NDEBUG
#include <algorithm>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
#include "controller_pak.h"
#include "recomp.h"

extern "C" {
void wdc_pfs_init(uint8_t*, recomp_context*);
void wdc_pfs_allocate_file(uint8_t*, recomp_context*);
void wdc_pfs_find_file(uint8_t*, recomp_context*);
void wdc_pfs_read_write_file(uint8_t*, recomp_context*);
void wdc_pfs_free_blocks(uint8_t*, recomp_context*);
void wdc_pfs_num_files(uint8_t*, recomp_context*);
void func_800720B0(uint8_t*, recomp_context*); // osPfsFileState
// The single-threaded harness replaces the SDK's SI access lock queues and
// supplies a deterministic timer count used by ID generation/repair.
void osCreateMesgQueue_recomp(uint8_t*, recomp_context*) {}
void osRecvMesg_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 0; }
void osSendMesg_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 0; }
void osGetCount_recomp(uint8_t*, recomp_context* ctx) { ctx->r2 = 123456; }
}

int main(int argc, char** argv) {
    assert(argc == 2);
    const auto directory = std::filesystem::temp_directory_path() /
        ("wdc-pfs-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path = directory / "controller.mpk";
    constexpr gpr stack = static_cast<int32_t>(0x807FF000);
    constexpr gpr pfs = static_cast<int32_t>(0x80400000);
    constexpr gpr name = pfs + 0x100, ext = name + 32, number = ext + 32;
    constexpr gpr buffer = pfs + 0x1000, state = pfs + 0x300;
    constexpr int size = 0xB00; // WDC's save menu requires 11 pages per game note
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    recomp_context ctx{};
    for (int run = 0; run < 2; ++run) {
        std::fill(memory.begin(), memory.end(), 0);
        std::ifstream rom(argv[1], std::ios::binary);
        assert(rom);
        rom.seekg(0x1000);
        std::vector<char> data(0x100000);
        rom.read(data.data(), data.size());
        assert(rom);
        for (size_t i = 0; i < data.size(); ++i)
            MEM_B(i, static_cast<int32_t>(0x80000400)) = data[i];
        wdc::pak::configure(path);
        ctx = {};
        ctx.r29 = stack;
        ctx.r4 = pfs + 0x200; ctx.r5 = pfs; ctx.r6 = 0;
        wdc_pfs_init(rdram, &ctx);
        std::cout << "InitPak run " << run << ": " << int(ctx.r2) << '\n';
        assert(ctx.r2 == 0);
        // WDC's own company/game codes as seen at the game-side find call.
        MEM_B(0, name) = 0x1A;
        MEM_B(0, ext) = 0x1B;
        ctx.r4 = pfs; ctx.r5 = 0x3544; ctx.r6 = 0x4E574F45; ctx.r7 = name;
        MEM_W(0x10, stack) = static_cast<int32_t>(ext);
        if (run == 0) {
            MEM_W(0x14, stack) = size;
            MEM_W(0x18, stack) = static_cast<int32_t>(number);
            wdc_pfs_allocate_file(rdram, &ctx);
        } else {
            MEM_W(0x14, stack) = static_cast<int32_t>(number);
            wdc_pfs_find_file(rdram, &ctx);
        }
        assert(ctx.r2 == 0);
        const int file_number = MEM_W(0, number);
        for (int i = 0; i < size; ++i) MEM_B(i, buffer) = run == 0 ? (i * 37 + 11) & 255 : 0;
        ctx.r4 = pfs; ctx.r5 = file_number; ctx.r6 = run == 0 ? 1 : 0; ctx.r7 = 0;
        MEM_W(0x10, stack) = size;
        MEM_W(0x14, stack) = static_cast<int32_t>(buffer);
        wdc_pfs_read_write_file(rdram, &ctx);
        assert(ctx.r2 == 0);
        for (int i = 0; i < size; ++i) assert(MEM_BU(i, buffer) == ((i * 37 + 11) & 255));
        ctx.r4 = pfs; ctx.r5 = number;
        wdc_pfs_free_blocks(rdram, &ctx);
        assert(ctx.r2 == 0 && MEM_W(0, number) == (123 - size / 256) * 256);
        ctx.r4 = pfs; ctx.r5 = number; ctx.r6 = number + 4;
        wdc_pfs_num_files(rdram, &ctx);
        assert(ctx.r2 == 0 && MEM_W(0, number) == 16 && MEM_W(4, number) == 1);
        ctx.r4 = pfs; ctx.r5 = file_number; ctx.r6 = state;
        func_800720B0(rdram, &ctx);
        assert(ctx.r2 == 0 && MEM_W(0, state) == size);
    }
    // Real InitPak must still report an absent device on an unconnected port.
    ctx.r4 = pfs + 0x200; ctx.r5 = pfs; ctx.r6 = 1;
    wdc_pfs_init(rdram, &ctx);
    assert(ctx.r2 == 1);
    std::filesystem::remove_all(directory);
    std::cout << "PASS: ROM filesystem initialized, allocated, wrote, reopened and read a note\n";
}
