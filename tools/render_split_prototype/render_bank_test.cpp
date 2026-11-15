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
    bank.capture(snapshot.data());
    assert(bank.restore().copied_bytes == snapshot.size());
    assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
    assert(bank.restore().copied_bytes == 0);
    const auto unchanged = bank.capture(snapshot.data());
    assert(unchanged.compared_bytes == snapshot.size() && unchanged.copied_bytes == 0);
    assert(bank.restore().copied_bytes == 0);
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
        assert(bank.restore().copied_bytes == pages.size() * page);
        assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
        assert(bank.restore().copied_bytes == 0);
    }
    // A completed renderer-thread write must be tracked too.
    std::thread writer([&] { std::memset(bank.data() + page - 7, 42, 14); });
    writer.join();
    assert(bank.restore().copied_bytes == 2 * page);
    assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
    // A new source can change every page, including clean ones.
    for (auto& value : snapshot) value ^= 0x5a;
    bank.capture(snapshot.data());
    assert(bank.restore().copied_bytes == snapshot.size());
    assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
    snapshot[10 * page] ^= 1;
    const auto changed_source = bank.capture(snapshot.data());
    assert(changed_source.compared_bytes == snapshot.size());
    assert(changed_source.copied_bytes == page);
    assert(bank.restore().copied_bytes == page);
    assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
    // Source changes and render writes can overlap or be disjoint.
    bank.data()[10 * page] ^= 2;
    bank.data()[11 * page] ^= 3;
    snapshot[10 * page] ^= 4;
    snapshot[12 * page] ^= 5;
    assert(bank.capture(snapshot.data()).copied_bytes == 2 * page);
    assert(bank.restore().copied_bytes == 3 * page);
    assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
    // Several captures before a restore must accumulate source changes.
    snapshot[20 * page] ^= 1;
    bank.capture(snapshot.data());
    snapshot[20 * page] ^= 1;
    snapshot[21 * page] ^= 1;
    bank.capture(snapshot.data());
    assert(bank.restore().copied_bytes == 2 * page);
    assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
    assert(std::memcmp(bank.snapshot(), snapshot.data(), snapshot.size()) == 0);
    prototype::RenderBank ordinary;
    ordinary.initialize(false);
    ordinary.capture(snapshot.data());
    assert(ordinary.restore().copied_bytes == snapshot.size());
    ordinary.data()[page] ^= 1;
    assert(ordinary.restore().copied_bytes == snapshot.size());
    assert(std::memcmp(ordinary.data(), snapshot.data(), snapshot.size()) == 0);
    snapshot[25 * page] ^= 1;
    assert(ordinary.capture(snapshot.data()).copied_bytes == snapshot.size());
    assert(ordinary.restore().copied_bytes == snapshot.size());
    assert(std::memcmp(ordinary.data(), snapshot.data(), snapshot.size()) == 0);
    std::puts("PASS: write-watch restores exact bytes across pages, threads, equal stores and new snapshots");
}
