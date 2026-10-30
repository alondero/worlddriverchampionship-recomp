#include "boss_capture.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>

// BOSS task captures without a ROM: synthetic word-swapped memories, the
// status timeline a replay must reproduce, the write journal that rebuilds
// the task's input view, and the on-disk format's byte order and validation.
using namespace wdc::boss_capture;

static void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "%s\n", message);
        std::exit(1);
    }
}

static Capture sample() {
    Capture c;
    for (unsigned i = 0; i < 16; ++i) c.task[i] = 0x80000000u + i * 0x111u;
    c.sp_status = 0x80;
    c.dp = {0x100, 0x200, 0x180, 1};
    c.dmem.assign(kDmemBytes, 0);
    c.rdram.assign(kRdramBytes, 0);
    for (uint32_t a = 0; a < kDmemBytes; ++a) c.dmem[a ^ 3] = uint8_t(a * 7);
    for (uint32_t a = 0; a < 64; ++a) c.rdram[(0x1000 + a) ^ 3] = uint8_t(a + 1);
    c.status_reads = {{0x80, 3}, {0, 41}, {0x80, 1}};
    c.output.push_back(copy_range(Source::Rdram, c.rdram.data(), 0x1000, 0x1010));
    c.output.push_back(copy_range(Source::Dmem, c.dmem.data(), 0x8, 0x10));
    return c;
}

static std::vector<uint8_t> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}
static void write_file(const std::filesystem::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
}

int main() {
    // Ranges are copied out of word-swapped storage in N64 byte order.
    {
        uint8_t memory[16]{};
        for (uint32_t a = 0; a < 16; ++a) memory[a ^ 3] = uint8_t(0xA0 + a);
        const auto range = copy_range(Source::Rdram, memory, 8, 16);
        require(range.bytes.size() == 8 && range.bytes[0] == 0xA8 && range.bytes[7] == 0xAF,
            "copy_range did not return guest byte order");
    }

    // Identical consecutive status reads collapse into runs; playback returns
    // them in order and then reports every further read as an overrun. The
    // values differ, so a playback that reordered the runs would be caught.
    {
        std::vector<StatusRun> runs;
        for (uint32_t v : {0x11u, 0x11u, 0x22u, 0x22u, 0x22u, 0x33u}) record_status(runs, v);
        require(runs == std::vector<StatusRun>{{0x11, 2}, {0x22, 3}, {0x33, 1}}, "status runs were not collapsed");
        StatusPlayback playback(runs);
        for (uint32_t v : {0x11u, 0x11u, 0x22u, 0x22u, 0x22u}) require(playback.next(0x55) == v, "status playback out of order");
        require(!playback.exhausted(), "status playback ended early");
        require(playback.next(0x55) == 0x33 && playback.exhausted(), "status playback missed the last read");
        require(playback.overruns() == 0, "status playback counted a recorded read as an overrun");
        require(playback.next(0x55) == 0x55 && playback.next(0x56) == 0x56, "exhausted playback ignored fallback");
        require(playback.overruns() == 2, "status playback did not count overruns");
        const std::vector<StatusRun> none;
        StatusPlayback empty(none);
        require(empty.exhausted() && empty.next(7) == 7 && empty.overruns() == 1, "empty playback misbehaved");
    }

    // The journal keeps the value from before BOSS's first write, so a CPU
    // write made between two BOSS writes does not leak into the input view,
    // while CPU writes to other bytes (sublists) are preserved.
    {
        std::vector<uint8_t> rdram(kRdramBytes, 0x11);
        WriteJournal journal;
        require(journal.before_write(rdram.data(), 0x2000, 8), "journal rejected a valid range");
        for (uint32_t a = 0x2000; a < 0x2008; ++a) rdram[a ^ 3] = 0xBB;  // BOSS output
        rdram[0x2003 ^ 3] = 0xCC;                                         // later CPU write
        require(journal.before_write(rdram.data(), 0x2000, 16), "journal rejected a rewrite");
        for (uint32_t a = 0x2000; a < 0x2010; ++a) rdram[a ^ 3] = 0xDD;   // second BOSS write
        rdram[0x3000 ^ 3] = 0xEE;                                         // CPU sublist
        require(journal.size() == 16, "journal recorded a byte twice");
        journal.restore(rdram);
        for (uint32_t a = 0x2000; a < 0x2010; ++a) require(rdram[a ^ 3] == 0x11, "journal did not restore BOSS bytes");
        require(rdram[0x3000 ^ 3] == 0xEE, "journal overwrote a CPU-written byte");
        require(!journal.before_write(rdram.data(), kRdramBytes - 8, 16), "journal accepted a range past RDRAM");
        require(journal.before_write(rdram.data(), kRdramBytes - 8, 8), "journal rejected the last RDRAM word");
    }

    const auto dir = std::filesystem::temp_directory_path() / "wdc_boss_capture_test";
    std::filesystem::create_directories(dir);
    const auto path = dir / "task.boss";
    std::string error;

    // Round trip, and the file stores memories in N64 big-endian order.
    const Capture original = sample();
    require(save(original, path.string(), error), error.c_str());
    {
        Capture loaded;
        require(load(path.string(), loaded, error), error.c_str());
        require(loaded.task == original.task && loaded.sp_status == original.sp_status &&
            loaded.dp == original.dp && loaded.dmem == original.dmem && loaded.rdram == original.rdram &&
            loaded.status_reads == original.status_reads && loaded.output == original.output,
            "capture changed across save/load");
    }
    const auto bytes = read_file(path);
    {
        require(bytes.size() > 8 + 21 * 4 + kDmemBytes + kRdramBytes, "capture file too short");
        require(std::string(bytes.begin(), bytes.begin() + 8) == "WDCBOSS1", "capture magic missing");
        require(bytes[8] == 0x80 && bytes[11] == 0x00 && bytes[15] == 0x11, "task words not big-endian");
        const size_t dmem_at = 8 + 21 * 4, rdram_at = dmem_at + kDmemBytes;
        for (uint32_t a = 0; a < 8; ++a) require(bytes[dmem_at + a] == uint8_t(a * 7), "DMEM not stored big-endian");
        for (uint32_t a = 0; a < 8; ++a)
            require(bytes[rdram_at + 0x1000 + a] == uint8_t(a + 1), "RDRAM not stored big-endian");
    }

    // Malformed files are rejected with a reason, never partially accepted.
    auto rejects = [&](std::vector<uint8_t> corrupt, const char* reason_part, const char* what) {
        write_file(path, corrupt);
        Capture loaded;
        std::string reason;
        require(!load(path.string(), loaded, reason), what);
        require(reason.find(reason_part) != std::string::npos, reason.c_str());
    };
    {
        auto bad = bytes;
        bad[0] = 'X';
        rejects(bad, "not a WDCBOSS1", "accepted a bad magic");
        rejects({bytes.begin(), bytes.end() - 1}, "truncated RDP bytes", "accepted a truncated capture");
        bad = bytes;
        bad.push_back(0);
        rejects(bad, "trailing bytes", "accepted trailing bytes");
        // The status run count, then {value, reads} per run.
        const size_t runs_at = 8 + 21 * 4 + kDmemBytes + kRdramBytes;
        bad = bytes;
        bad[runs_at] = 0xFF;  // count far above kMaxStatusRuns
        rejects(bad, "bad status run count", "accepted an over-limit status run count");
        bad = bytes;
        for (int i = 0; i < 4; ++i) bad[runs_at + 8 + i] = 0;
        rejects(bad, "no reads", "accepted a zero-read status run");
        // The range count, then {source, start, end, bytes} per range. The
        // first range is RDRAM 0x1000..0x1010.
        const size_t ranges_at = runs_at + 4 + 3 * 8 + 4;
        bad = bytes;
        bad[ranges_at - 4] = 0xFF;  // count far above kMaxRdpRanges
        rejects(bad, "bad RDP range count", "accepted an over-limit RDP range count");
        bad = bytes;
        bad[ranges_at + 4 + 1] = 0x7F;  // start 0x7F1000 > end
        rejects(bad, "out of bounds", "accepted an inverted RDP range");
        bad = bytes;
        bad[ranges_at + 3] = 1;  // as DMEM, end 0x1010 is past its 4 KiB
        rejects(bad, "out of bounds", "accepted an RDP range past DMEM");
        bad = bytes;
        bad[ranges_at + 4 + 3] = 0x04;  // start 0x1004 is not 8-byte aligned
        rejects(bad, "out of bounds", "accepted a misaligned RDP range");
        bad = bytes;
        bad[ranges_at + 3] = 9;
        rejects(bad, "unknown RDP range source", "accepted an unknown RDP source");
    }
    // save() refuses what load() would reject rather than writing it.
    {
        Capture big = original;
        big.output.resize(size_t(kMaxRdpRanges) + 1);
        std::string reason;
        require(!save(big, path.string(), reason) && !reason.empty(), "saved an over-limit capture");
    }
    std::string missing;
    Capture unused;
    require(!load((dir / "absent.boss").string(), unused, missing) && !missing.empty(), "loaded a missing file");
    std::filesystem::remove_all(dir);
    std::puts("BOSS capture format and journal passed");
}
