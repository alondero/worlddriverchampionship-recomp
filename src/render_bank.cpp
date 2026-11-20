#include "render_bank.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace wdc::render {
RenderBank& race_render_bank() {
    static RenderBank bank;
    return bank;
}

RenderBank::~RenderBank() {
#ifdef _WIN32
    if (watched_) VirtualFree(watched_, 0, MEM_RELEASE);
#endif
}

void RenderBank::initialize(bool prefer_write_watch) {
    if (!empty()) return;
#ifdef _WIN32
    SYSTEM_INFO info{};
    GetSystemInfo(&info);
    if (prefer_write_watch && info.dwPageSize && size % info.dwPageSize == 0) {
        dirty_pages_.resize(size / info.dwPageSize);
        pending_pages_.resize(dirty_pages_.size());
        watched_ = static_cast<uint8_t*>(VirtualAlloc(nullptr, size,
            MEM_RESERVE | MEM_COMMIT | MEM_WRITE_WATCH, PAGE_READWRITE));
        if (watched_) {
            page_size_ = info.dwPageSize;
            return;
        }
    }
#else
    (void)prefer_write_watch;
#endif
    ordinary_.resize(size);
}

RenderBank::Transfer RenderBank::capture(const uint8_t* source) {
    if (empty() || !source) throw std::logic_error("render bank capture requires initialized storage and source");
    if (!page_size_ || frozen_.empty()) {
        frozen_.assign(source, source + size);
        return {size, 0};
    }
    Transfer result{0, size};
    // Renderer writes alone cannot identify pages changed by the simulation.
    for (std::size_t at = 0; at < size; at += page_size_) {
        if (std::memcmp(frozen_.data() + at, source + at, page_size_)) {
            std::memcpy(frozen_.data() + at, source + at, page_size_);
            pending_pages_[at / page_size_] = 1;
            result.copied_bytes += page_size_;
        }
    }
    return result;
}

RenderBank::Transfer RenderBank::restore() {
    if (frozen_.size() != size) throw std::logic_error("render bank restore requires a captured sample");
    Transfer result;
#ifdef _WIN32
    if (page_size_ && has_restored_) {
        ULONG_PTR count = dirty_pages_.size();
        DWORD granularity = 0;
        if (GetWriteWatch(0, watched_, size, dirty_pages_.data(), &count, &granularity) ||
            granularity != page_size_ || count > dirty_pages_.size()) {
            page_size_ = 0;
        } else {
            for (ULONG_PTR i = 0; i < count; ++i) {
                const auto offset = reinterpret_cast<uintptr_t>(dirty_pages_[i]) - reinterpret_cast<uintptr_t>(watched_);
                if (offset >= size || offset % page_size_) {
                    page_size_ = 0;
                    break;
                }
                pending_pages_[offset / page_size_] = 1;
            }
        }
    }
#endif
    if (page_size_ && has_restored_) {
        for (std::size_t i = 0; i < pending_pages_.size(); ++i) {
            if (!pending_pages_[i]) continue;
            const auto offset = i * page_size_;
            std::memcpy(data() + offset, frozen_.data() + offset, page_size_);
            result.copied_bytes += page_size_;
        }
    } else {
        std::memcpy(data(), frozen_.data(), size);
        result.copied_bytes = size;
    }
#ifdef _WIN32
    // Clear tracking after our own copies. If tracking fails, keep the address
    // already bound by RT64 and restore the full image on every later call.
    if (page_size_ && ResetWriteWatch(watched_, size)) page_size_ = 0;
#endif
    std::fill(pending_pages_.begin(), pending_pages_.end(), 0);
    has_restored_ = true;
    return result;
}
}
