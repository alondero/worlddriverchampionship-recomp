#undef NDEBUG
#include "race_render_sample.h"
#include <cstring>
#include <cstdio>
#include <cassert>
#include <thread>
#include <set>

namespace {
bool guest_retired = false;
void finish_renderer(uint8_t* rdram) {
    assert(guest_retired);
    rdram[0] = 42;
}
}
extern "C" void func_800252D0(uint8_t* rdram, recomp_context* ctx) {
    assert(ctx->f_odd == &ctx->f0.u32h);
    ctx->f_odd[0] = 777;
    ctx->r29 = 0;
    rdram[1] = 43;
    guest_retired = true;
}

void test_page_tracking_or_full_copy_fallback(std::vector<uint8_t>& snapshot) {
    wdc::render::RenderBank bank;
    assert(bank.empty());
    bank.initialize(true);
    const size_t page = bank.page_size();
    if (!page) {
        bank.capture(snapshot.data());
        assert(bank.restore().copied_bytes == snapshot.size());
        bank.data()[0] ^= 1;
        assert(bank.restore().copied_bytes == snapshot.size());
        assert(std::memcmp(bank.data(), snapshot.data(), snapshot.size()) == 0);
        return;
    }
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
}

int main() {
    std::vector<uint8_t> snapshot(wdc::render::RenderBank::size);
    for (size_t i = 0; i < snapshot.size(); ++i) snapshot[i] = uint8_t(i * 17 + i / 257);
    test_page_tracking_or_full_copy_fallback(snapshot);
    constexpr size_t page = 4096;
    wdc::render::RenderBank ordinary;
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
    recomp_context caller{};
    caller.f_odd = &caller.f0.u32h;
    caller.r29 = 123;
    wdc::render::capture_race_sample(snapshot.data(), caller, false, finish_renderer);
    auto& race = wdc::render::race_render_bank();
    assert(caller.r29 == 123 && *caller.f_odd == 0);
    assert(race.snapshot()[0] == 42 && race.snapshot()[1] == 43);
    assert(race.restore().copied_bytes == race.size);
    race.data()[0] = 0;
    assert(snapshot[0] == 42 && race.snapshot()[0] == 42);
    std::puts("PASS: race capture drains guest and renderer before freezing the snapshot");
}
