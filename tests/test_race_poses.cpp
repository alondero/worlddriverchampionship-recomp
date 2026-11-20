#undef NDEBUG
#include "race_poses.h"
#include <cassert>
#include <cstring>

using namespace wdc::render;
namespace {
constexpr uint32_t base = 0x80000000, view = 0x800ABE40, camera = 0x80200000, car = 0x800BA7F0;
void word(uint8_t* ram, uint32_t address, uint32_t value) {
    std::memcpy(ram + (address - base), &value, sizeof(value));
}
void position(uint8_t* ram, uint32_t address, float value) {
    word(ram, address, std::bit_cast<uint32_t>(value));
}
float read_float(const uint8_t* ram, uint32_t address) {
    float value;
    std::memcpy(&value, ram + (address - base), sizeof(value));
    return value;
}
void identity(uint8_t* ram, uint32_t address) {
    for (unsigned i = 0; i < 4; ++i) position(ram, address + i * 20, 1);
}
}

int main() {
    RenderBank bank;
    assert(!RacePoses::capture(bank, true, true));
    bank.initialize(false);
    std::vector<uint8_t> live(RenderBank::size);
    auto* ram = live.data();
    word(ram, 0x8007E580, 1);
    word(ram, view + 0x2DC, camera);
    word(ram, 0x80080D90, 1);
    word(ram, 0x8007E530, 0xFFFFFFFF);
    word(ram, 0x8007DF40, 1);
    word(ram, car + 0x2F4, 42);
    identity(ram, camera);
    identity(ram, car + 0x14);
    for (unsigned i = 0; i < 4; ++i) identity(ram, car + 0x94 + i * 0x84);
    // The contact path uses only XY of this position.
    word(ram, car + 0x374, 0x7FC00000);
    auto sample = [&] {
        bank.capture(ram);
        return RacePoses::capture(bank, true, true);
    };
    const auto previous = sample();
    assert(previous && previous->pose_count() == 6 && previous->shadow_count() == 5);
    word(ram, 0x8007E530, 0);
    word(ram, car + 0x2F4, 99);
    position(ram, camera + 48, 8);
    position(ram, car + 0x14 + 48, 4);
    position(ram, car + 0x94 + 48, 6);
    position(ram, car + 0xC6C, 10);
    const auto current = sample();
    assert(current && !current->compare_with(*previous).reset());
    assert(current->compare_with(*previous).sample_gap == 1);
    bank.restore();
    const auto changes = current->apply(bank, *previous, .5);
    assert(changes && changes->camera == 1 && changes->bodies == 1 &&
           changes->wheels == 1 && changes->shadows == 1);
    assert(read_float(bank.data(), camera + 48) == 4 && read_float(ram, camera + 48) == 8);
    assert(read_float(bank.data(), car + 0xC6C) == 5 && read_float(bank.snapshot(), car + 0xC6C) == 10);
    assert(current->apply(bank, *previous, 1));
    assert(std::memcmp(bank.data(), ram, live.size()) == 0);
    position(ram, camera + 48, 65);
    assert(sample()->compare_with(*previous).pose_jump);
    position(ram, camera + 48, 8);
    position(ram, car + 0xC6C, 65);
    assert(sample()->compare_with(*previous).shadow_jump);
    word(ram, 0x8007DF40, 0);
    position(ram, car + 0x360, 1);
    position(ram, car + 0x374, 0);
    const auto normal = sample();
    assert(normal && normal->shadow_count() == 2 && normal->compare_with(*current).identity_changed);
    word(ram, 0x8007E530, 1);
    position(ram, car + 0x360, -1);
    assert(sample()->compare_with(*normal).shadow_jump);
    position(ram, car + 0x360, 3);
    assert(!sample());
    position(ram, car + 0x360, 1);
    word(ram, 0x8007E580, 2);
    assert(!sample());
    word(ram, 0x8007E580, 1);
    word(ram, view + 0x2DC, 0x807FFFC0);
    assert(!sample());
    word(ram, view + 0x2DC, camera);
    word(ram, 0x80080D90, 0xFFFFFFFF);
    assert(!sample());
}
