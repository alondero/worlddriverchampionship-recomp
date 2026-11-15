#include "render_bank.h"
#include <cassert>
#include <thread>
#include <set>

int main() {
    std::vector<uint8_t> snapshot(prototype::RenderBank::size);
    for (size_t i = 0; i < snapshot.size(); ++i) snapshot[i] = uint8_t(i * 17 + i / 257);
    prototype::RenderBank bank;
    assert(bank.empty());
    bank.initialize(true);
    const size_t page = bank.page_size();
    assert(bank.restore(snapshot.data(), true).copied_bytes == snapshot.size());
    assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
    assert(bank.restore(snapshot.data(), false).copied_bytes == 0);
    uint32_t random = 1234567;
    for (unsigned trial = 0; trial < 32; ++trial) {
        std::set<size_t> pages;
        auto write = [&](size_t offset) {
            volatile uint8_t* target = bank.data() + offset;
            *target = uint8_t(snapshot[offset] ^ trial); // Includes same-value writes.
            pages.insert(offset / page);
        };
        write(0); write(snapshot.size() - 1);
        write(page - 1); write(page);
        for (unsigned i = 0; i < 300; ++i) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            write(random % snapshot.size());
        }
        assert(bank.restore(snapshot.data(), false).copied_bytes == pages.size() * page);
        assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
        assert(bank.restore(snapshot.data(), false).copied_bytes == 0);
    }
    // A completed renderer-thread write must be tracked too.
    std::thread writer([&] { std::memset(bank.data() + page - 7, 42, 14); });
    writer.join();
    assert(bank.restore(snapshot.data(), false).copied_bytes == 2 * page);
    assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
    // A new source may change clean pages; it must force a full restore.
    for (auto& value : snapshot) value ^= 0x5a;
    assert(bank.restore(snapshot.data(), true).copied_bytes == snapshot.size());
    assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
    snapshot[10 * page] ^= 1;
    const auto changed_source = bank.restore(snapshot.data(), true);
    assert(changed_source.compared_bytes == snapshot.size());
    assert(changed_source.copied_bytes == page);
    assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
    prototype::RenderBank ordinary;
    ordinary.initialize(false);
    assert(ordinary.restore(snapshot.data(), true).copied_bytes == snapshot.size());
    ordinary.data()[page] ^= 1;
    assert(ordinary.restore(snapshot.data(), false).copied_bytes == snapshot.size());
    assert(std::memcmp(ordinary.data(), snapshot.data(), snapshot.size()) == 0);
    std::puts("PASS: write-watch restores exact bytes across pages, threads, equal stores and new snapshots");
}
