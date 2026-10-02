#undef NDEBUG
#include <cassert>
#include <chrono>
#include <fstream>
#include <vector>
#include "controller_pak.h"
#include "recomp.h"

extern "C" {
void wdc_pfs_is_plug(uint8_t*, recomp_context*);
void wdc_pak_status(uint8_t*, recomp_context*);
void wdc_pak_read(uint8_t*, recomp_context*);
void wdc_pak_write(uint8_t*, recomp_context*);
}

int main() {
    const auto directory = std::filesystem::temp_directory_path() /
        ("wdc-pak-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto path = directory / "saves" / "controller.mpk";
    std::vector<uint8_t> memory(8 * 1024 * 1024);
    auto* rdram = memory.data();
    recomp_context ctx{};
    ctx.r29 = static_cast<int32_t>(0x807FF000);
    ctx.r7 = static_cast<int32_t>(0x80001001); // byte buffers need no alignment
    wdc::pak::configure(path);
    using namespace ultramodern::input;
    assert(wdc::pak::device_info(0).connected_pak == Pak::ControllerPak);
    assert(wdc::pak::device_info(1).connected_device == Device::None);
    ctx.r5 = ctx.r7;
    wdc_pfs_is_plug(rdram, &ctx);
    assert(ctx.r2 == 0 && MEM_BU(0, ctx.r7) == 1);
    ctx.r5 = 0;
    ctx.r6 = 0x3FF;
    for (unsigned i = 0; i < 32; ++i) MEM_B(i, ctx.r7) = i * 7;
    wdc_pak_write(rdram, &ctx);
    assert(ctx.r2 == 0 && std::filesystem::file_size(path) == 32768);
    wdc::pak::configure(path);
    wdc_pak_read(rdram, &ctx);
    for (unsigned i = 0; i < 32; ++i) assert(MEM_BU(i, ctx.r7) == i * 7);
    // Raw images use N64 byte order, independently of RDRAM word swapping.
    std::ifstream file(path, std::ios::binary);
    file.seekg(0x7FE0);
    for (unsigned i = 0; i < 32; ++i) assert(file.get() == int(i * 7));
    file.close();

    // A publication failure preserves the acknowledged image in RAM and on disk.
    std::filesystem::create_directory(path.string() + ".tmp");
    MEM_B(0, ctx.r7) = 99;
    wdc_pak_write(rdram, &ctx);
    assert(ctx.r2 == 4);
    wdc_pak_read(rdram, &ctx);
    assert(MEM_BU(0, ctx.r7) == 0);
    wdc::pak::configure(path);
    wdc_pak_read(rdram, &ctx);
    assert(MEM_BU(0, ctx.r7) == 0);

    for (uint32_t bad : {0u, 0x807FFFE1u, 0x80800000u}) {
        ctx.r7 = static_cast<int32_t>(bad);
        wdc_pak_read(rdram, &ctx); assert(ctx.r2 == 4);
        wdc_pak_write(rdram, &ctx); assert(ctx.r2 == 4);
    }
    ctx.r7 = static_cast<int32_t>(0x80001001);
    ctx.r6 = 1;
    wdc_pak_read(rdram, &ctx);
    const auto id = MEM_BU(1, ctx.r7);
    MEM_B(1, ctx.r7) = 99;
    wdc_pak_write(rdram, &ctx); assert(ctx.r2 == 0);
    wdc_pak_read(rdram, &ctx); assert(MEM_BU(1, ctx.r7) == id);
    ctx.r29 = static_cast<int32_t>(0x807FFFF0);
    wdc_pak_write(rdram, &ctx); assert(ctx.r2 == 4);
    ctx.r29 = static_cast<int32_t>(0x807FF000);
    MEM_W(0x10, ctx.r29) = 1;
    MEM_B(1, ctx.r7) = 99;
    wdc_pak_write(rdram, &ctx); assert(ctx.r2 == 0);
    wdc_pak_read(rdram, &ctx); assert(MEM_BU(1, ctx.r7) == 99);
    ctx.r6 = 0x400;
    MEM_B(0, ctx.r7) = 0;
    wdc_pak_write(rdram, &ctx); assert(ctx.r2 == 0);
    MEM_B(0, ctx.r7) = 1;
    wdc_pak_write(rdram, &ctx); assert(ctx.r2 == 4);
    wdc_pak_read(rdram, &ctx); assert(ctx.r2 == 4);
    ctx.r6 = 0x401;
    wdc_pak_write(rdram, &ctx); assert(ctx.r2 == 4);
    ctx.r5 = 1;
    wdc_pak_status(rdram, &ctx); assert(ctx.r2 == 1);
    wdc_pak_read(rdram, &ctx); assert(ctx.r2 == 1);
    wdc_pak_write(rdram, &ctx); assert(ctx.r2 == 1);

    wdc::pak::configure(path, false);
    assert(wdc::pak::device_info(0).connected_pak == Pak::None);
    ctx.r5 = ctx.r7;
    wdc_pfs_is_plug(rdram, &ctx);
    assert(ctx.r2 == 0 && MEM_BU(0, ctx.r7) == 0);
    ctx.r5 = 0;
    wdc_pak_status(rdram, &ctx); assert(ctx.r2 == 1);
    const auto bad = directory / "truncated.mpk";
    { std::ofstream stream(bad); stream << "keep me"; }
    wdc::pak::configure(bad);
    assert(wdc::pak::device_info(0).connected_pak == Pak::None);
    wdc_pak_write(rdram, &ctx);
    assert(ctx.r2 == 1 && std::filesystem::file_size(bad) == 7);
    std::filesystem::remove_all(directory);
}
