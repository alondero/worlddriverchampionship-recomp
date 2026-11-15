#pragma once
// Windows-only prototype storage. Caller drains all users and holds the renderer
// memory mutex through capture/restore: no writers may overlap snapshot capture
// or Get/ResetWriteWatch. All methods are called on that fenced game thread.
// Raw bytes preserve guest word swapping. Never restores authoritative memory.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace prototype {
class RenderBank {
    std::vector<uint8_t> frozen_;
    std::vector<uint8_t> ordinary_;
    uint8_t* watched_ = nullptr;
    std::vector<void*> dirty_pages_;
    std::vector<uint8_t> pending_pages_;
    DWORD page_size_ = 0;
    bool has_restored_ = false;
    [[noreturn]] static void fail(const char* operation) {
        std::fprintf(stderr, "[prototype-error] render bank: %s\n", operation);
        std::_Exit(25);
    }
public:
    static constexpr size_t size = 0x800000;
    struct Transfer { size_t copied_bytes = 0, compared_bytes = 0; };
    RenderBank() = default;
    RenderBank(const RenderBank&) = delete;
    RenderBank& operator=(const RenderBank&) = delete;
    ~RenderBank() { if (watched_) VirtualFree(watched_, 0, MEM_RELEASE); }
    uint8_t* data() { return watched_ ? watched_ : ordinary_.data(); }
    const uint8_t* snapshot() const { return frozen_.data(); }
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
        pending_pages_.resize(size / page_size_);
        watched_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, size,
            MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE));
        if (!watched_) fail("VirtualAlloc failed");
    }
    // Capture size bytes from the authoritative source at the fenced game-thread
    // boundary. Frozen storage is private and immutable between captures.
    Transfer capture(const uint8_t* source) {
        if (empty() || !source) fail("uninitialized capture");
        Transfer result;
        if (!watched_ || frozen_.empty()) {
            frozen_.assign(source, source + size);
            result.copied_bytes = size;
        } else {
            // The authoritative source can change pages not written by the
            // renderer. Compare every source page once, during capture.
            for (size_t offset = 0; offset < size; offset += page_size_) {
                if (std::memcmp(frozen_.data() + offset, source + offset, page_size_)) {
                    std::memcpy(frozen_.data() + offset, source + offset, page_size_);
                    pending_pages_[offset / page_size_] = 1;
                    result.copied_bytes += page_size_;
                }
            }
            result.compared_bytes = size;
        }
        return result;
    }
    Transfer restore() {
        if (empty() || frozen_.size() != size) fail("restore before capture");
        Transfer result;
        if (!watched_ || !has_restored_) {
            std::memcpy(data(), frozen_.data(), size);
            result.copied_bytes = size;
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
                pending_pages_[(address - base) / page_size_] = 1;
            }
            // Union source changes with renderer writes; copy each page once.
            for (size_t i = 0; i < pending_pages_.size(); ++i) {
                if (!pending_pages_[i]) continue;
                const auto offset = i * page_size_;
                std::memcpy(watched_ + offset, frozen_.data() + offset, page_size_);
                result.copied_bytes += page_size_;
            }
        }
        // Reset after restoring, otherwise our own copies would mark the pages
        // dirty again. All CPU/BOSS/renderer writers remain fenced by caller.
        if (watched_ && ResetWriteWatch(watched_, size)) fail("ResetWriteWatch failed");
        std::fill(pending_pages_.begin(), pending_pages_.end(), 0);
        has_restored_ = true;
        return result;
    }
};
}
