#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace wdc::render {

// One immutable 8 MiB sample and one writable word-swapped guest image.
// The game thread drains outstanding tasks and screen updates before taking
// mutex to capture/restore. Renderer memory access uses the same mutex.
class RenderBank {
public:
    static constexpr std::size_t size = 0x800000;
    struct Transfer { std::size_t copied_bytes = 0, compared_bytes = 0; };
    RenderBank() = default;
    RenderBank(const RenderBank&) = delete;
    RenderBank& operator=(const RenderBank&) = delete;
    ~RenderBank();

    std::mutex mutex;
    // Storage is allocated on first use. A platform without write tracking
    // uses full copies. The writable address is stable until destruction.
    void initialize(bool prefer_write_watch);
    Transfer capture(const uint8_t* source);
    Transfer restore();
    uint8_t* data() { return watched_ ? watched_ : ordinary_.data(); }
    const uint8_t* snapshot() const { return frozen_.data(); }
    bool empty() const { return !watched_ && ordinary_.empty(); }
    std::size_t page_size() const { return page_size_; }

private:
    std::vector<uint8_t> frozen_, ordinary_;
    uint8_t* watched_ = nullptr;
    std::vector<void*> dirty_pages_;
    std::vector<uint8_t> pending_pages_;
    std::size_t page_size_ = 0;
    bool has_restored_ = false;
};

// Process-lifetime storage for the race renderer. It allocates no guest images
// until the fixed-rate draw boundary requests its first sample.
RenderBank& race_render_bank();
}
