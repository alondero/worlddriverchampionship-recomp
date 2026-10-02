// One host block device; the translated SDK owns the Pak filesystem and saves.
// Adapted from AeroGauge's block-device seam, verified against WDC's USA SDK.
#include "controller_pak.h"
#include "recomp.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr int NoPak = 1;
constexpr int IoError = 4; // PFS_ERR_CONTRFAIL
constexpr unsigned BlockSize = 32;
using Image = std::array<uint8_t, 32768>;
std::mutex mutex;
Image image{};
std::filesystem::path image_path;
bool available = false;

bool guest_range_valid(uint64_t address, unsigned size, unsigned alignment = 1) {
    const auto base = static_cast<uint32_t>(address);
    return base >= 0x80000000u && uint64_t(base) + size <= 0x80800000u &&
           (base & (alignment - 1)) == 0;
}

// SDK one-bank layout: redundant IDs at blocks 1/3/4/6, inode tables at
// pages 1/2, directory at pages 3/4, and 123 free data pages. Checksums use
// big-endian halfwords, as validated by wdc_pfs_init and its checker.
void format() {
    image.fill(0);
    for (unsigned block : {1u, 3u, 4u, 6u}) {
        auto* id = image.data() + block * BlockSize;
        id[1] = 0x2A;
        id[5] = 5; id[6] = 0xA1; id[7] = 0xB0;
        id[8] = 5; id[9] = 7; id[10] = 0xC3; id[11] = 0x39;
        id[25] = 1; // memory device
        id[26] = 1; // one bank
        uint16_t sum = 0, inverse = 0;
        for (unsigned i = 0; i < 28; i += 2) {
            const uint16_t word = (id[i] << 8) | id[i + 1];
            sum += word;
            inverse += static_cast<uint16_t>(~word);
        }
        id[28] = sum >> 8; id[29] = sum;
        id[30] = inverse >> 8; id[31] = inverse;
    }
    for (unsigned page : {1u, 2u}) {
        for (unsigned slot = 0; slot < 128; ++slot) image[page * 256 + slot * 2 + 1] = 3;
        image[page * 256 + 1] = (123 * 3) & 255;
    }
}

bool publish(const Image& next) {
    std::error_code error;
    if (!image_path.parent_path().empty()) {
        std::filesystem::create_directories(image_path.parent_path(), error);
        if (error) {
            std::fprintf(stderr, "[wdc] Cannot create Controller Pak save directory.\n");
            return false;
        }
    }
    auto temporary = image_path;
    temporary += ".tmp";
    constexpr unsigned MaxAttempts = 5;
    for (unsigned attempt = 0; attempt < MaxAttempts; ++attempt) {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream.write(reinterpret_cast<const char*>(next.data()), next.size());
        stream.close();
        bool success = !stream.fail();
        bool transient = false;
        if (success) {
#ifdef _WIN32
            success = MoveFileExW(temporary.c_str(), image_path.c_str(),
                                  MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
            if (!success) {
                const auto code = GetLastError();
                transient = code == ERROR_SHARING_VIOLATION ||
                            code == ERROR_ACCESS_DENIED || code == ERROR_LOCK_VIOLATION;
            }
#else
            std::filesystem::rename(temporary, image_path, error);
            success = !error;
            transient = error == std::errc::device_or_resource_busy ||
                        error == std::errc::permission_denied;
#endif
        }
        if (success) return true;
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        if (!transient || attempt + 1 == MaxAttempts) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1u << (attempt + 1)));
    }
    std::fprintf(stderr, "[wdc] Controller Pak save failed; previous image retained.\n");
    return false;
}
}

void wdc::pak::configure(const std::filesystem::path& path, bool enabled) {
    std::lock_guard lock(mutex);
    image_path = path;
    available = false;
    if (!enabled) return;
    std::error_code error;
    const bool exists = std::filesystem::exists(path, error);
    if (!error && !exists) {
        format();
        available = true;
        return;
    }
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (error || !stream || stream.tellg() != static_cast<std::streamoff>(image.size())) {
        std::fprintf(stderr, "[wdc] Cannot load Controller Pak: expected a readable 32 KiB MPK; file preserved.\n");
        return;
    }
    stream.seekg(0);
    stream.read(reinterpret_cast<char*>(image.data()), image.size());
    available = !stream.fail() && stream.gcount() == static_cast<std::streamsize>(image.size());
}

ultramodern::input::connected_device_info_t wdc::pak::device_info(int port) {
    std::lock_guard lock(mutex);
    using namespace ultramodern::input;
    return {port == 0 ? Device::Controller : Device::None,
            port == 0 && available ? Pak::ControllerPak : Pak::None};
}

// Guest bridges run synchronously on the calling translated guest thread.
// Queue a0 remains SDK-owned and unused by the synchronous device. Channel a1
// is zero-based. Buffers are runtime-owned KSEG0 RDRAM, 8 MiB, word-swapped;
// MEM_B/MEM_BU convert individual N64 bytes. No guest pointer is retained.
// Invalid ranges/blocks return 4 without accessing memory; absent ports return 1.
// Replace these hardware adapters if the pinned runtime gains block-device I/O.

// osPfsIsPlug(queue, u8* bitmap), USA 0x800748F0; one output byte at a1.
extern "C" void wdc_pfs_is_plug(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard lock(mutex);
    ctx->r2 = IoError;
    if (!guest_range_valid(ctx->r5, 1)) return;
    MEM_B(0, ctx->r5) = available ? 1 : 0;
    ctx->r2 = 0;
}

// __osPfsGetStatus(queue, channel), USA 0x800779A0; no guest-memory access.
extern "C" void wdc_pak_status(uint8_t*, recomp_context* ctx) {
    std::lock_guard lock(mutex);
    ctx->r2 = available && ctx->r5 == 0 ? 0 : NoPak;
}

// __osContRamRead(queue, channel, u16 block, u8* buffer), USA 0x80078940.
// a2 uses 32-byte units, a3 receives exactly 32 bytes, valid blocks 0..0x3FF.
extern "C" void wdc_pak_read(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard lock(mutex);
    ctx->r2 = NoPak;
    if (!available || ctx->r5 != 0) return;
    ctx->r2 = IoError;
    const unsigned block = static_cast<uint16_t>(ctx->r6);
    if (block >= image.size() / BlockSize || !guest_range_valid(ctx->r7, BlockSize)) return;
    for (unsigned i = 0; i < BlockSize; ++i) MEM_B(i, ctx->r7) = image[block * BlockSize + i];
    ctx->r2 = 0;
}

// __osContRamWrite(..., s32 force), USA 0x80079700. Same buffer/units as read.
// Fifth arg is the aligned, big-endian u32 at guest sp+0x10. ID blocks 1..6
// are ignored unless force == 1; block 0x400 selects bank 0 without storing.
extern "C" void wdc_pak_write(uint8_t* rdram, recomp_context* ctx) {
    std::lock_guard lock(mutex);
    ctx->r2 = NoPak;
    if (!available || ctx->r5 != 0) return;
    ctx->r2 = IoError;
    const unsigned block = static_cast<uint16_t>(ctx->r6);
    if (block > 0 && block < 7) {
        if (!guest_range_valid(ctx->r29, 0x14, 4)) return;
        if (MEM_W(0x10, ctx->r29) != 1) {
            ctx->r2 = 0;
            return;
        }
    }
    if (!guest_range_valid(ctx->r7, BlockSize)) return;
    if (block == 0x400) {
        ctx->r2 = MEM_BU(0, ctx->r7) == 0 ? 0 : IoError;
        return;
    }
    if (block >= image.size() / BlockSize) return;
    Image next = image;
    for (unsigned i = 0; i < BlockSize; ++i) next[block * BlockSize + i] = MEM_BU(i, ctx->r7);
    // Publish before acknowledging: Escape/diagnostic exits use _Exit. Failed
    // writes change neither disk nor RAM. The mutex serializes guest callers.
    if (publish(next)) {
        image = next;
        ctx->r2 = 0;
    }
}
