#pragma once
// Windows-only prototype storage. Caller drains all users and holds the renderer
// memory mutex through restore(): no writers may overlap Get/ResetWriteWatch.
// Raw bytes preserve guest word swapping. Never restores authoritative memory.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace prototype {
class RenderBank {
    std::vector<uint8_t> ordinary_;
    uint8_t* watched_ = nullptr;
    std::vector<void*> dirty_pages_;
    DWORD page_size_ = 0;
    bool has_snapshot_ = false;
    [[noreturn]] static void fail(const char* operation) {
        std::fprintf(stderr, "[prototype-error] render bank: %s\n", operation);
        std::_Exit(25);
    }
public:
    static constexpr size_t size = 0x800000;
    struct Restore { size_t copied_bytes = 0, compared_bytes = 0; };
    RenderBank() = default;
    RenderBank(const RenderBank&) = delete;
    RenderBank& operator=(const RenderBank&) = delete;
    ~RenderBank() { if (watched_) VirtualFree(watched_, 0, MEM_RELEASE); }
    uint8_t* data() { return watched_ ? watched_ : ordinary_.data(); }
    bool empty() const { return !watched_ && ordinary_.empty(); }
    size_t page_size() const { return page_size_; }
    void initialize(bool watch) {
        if (!empty()) {
            if (watch != (watched_ != nullptr)) fail("allocation mode changed");
            return;
        }
        if (!watch) { ordinary_.resize(size); return; }
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        page_size_ = info.dwPageSize;
        if (!page_size_ || size % page_size_) fail("unsupported page size");
        dirty_pages_.resize(size / page_size_);
        watched_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, size,
            MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE));
        if (!watched_) fail("VirtualAlloc failed");
    }
    // Source supplies size bytes and must not overlap this allocation. Caller
    // sets new_snapshot whenever it changes: seed once, then compare all pages
    // for each new source. Later draws of that source restore recorded writes.
    Restore restore(const uint8_t* source, bool new_snapshot) {
        if (empty() || !source) fail("uninitialized restore");
        Restore result;
        if (!watched_ || !has_snapshot_) {
            std::memcpy(data(), source, size);
            result.copied_bytes = size;
        } else if (new_snapshot) {
            // The authoritative source can change pages not written by the
            // renderer. Compare the entire bank, never just its dirty list.
            for (size_t offset = 0; offset < size; offset += page_size_) {
                if (std::memcmp(watched_ + offset, source + offset, page_size_)) {
                    std::memcpy(watched_ + offset, source + offset, page_size_);
                    result.copied_bytes += page_size_;
                }
            }
            result.compared_bytes = size;
        } else {
            ULONG_PTR count = dirty_pages_.size();
            DWORD granularity = 0;
            if (GetWriteWatch(0, watched_, size, dirty_pages_.data(), &count, &granularity) ||
                granularity != page_size_ || count > dirty_pages_.size()) fail("GetWriteWatch failed");
            const auto base = reinterpret_cast<uintptr_t>(watched_);
            for (ULONG_PTR i = 0; i < count; ++i) {
                const auto address = reinterpret_cast<uintptr_t>(dirty_pages_[i]);
                if (address < base || address - base > size - page_size_ ||
                    (address - base) % page_size_) fail("invalid dirty page");
                const auto offset = address - base;
                std::memcpy(watched_ + offset, source + offset, page_size_);
            }
            result.copied_bytes = count * page_size_;
        }
        // Reset after restoring, otherwise our own copies would mark the pages
        // dirty again. All CPU/BOSS/renderer writers remain fenced by caller.
        if (watched_ && ResetWriteWatch(watched_, size)) fail("ResetWriteWatch failed");
        has_snapshot_ = true;
        return result;
    }
};
}
