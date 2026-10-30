#include "boss_capture.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace wdc::boss_capture {
namespace {
// File layout, all integers big-endian u32:
//   "WDCBOSS1", task[16], sp_status, dp[4], DMEM, RDRAM,
//   status run count, {value, reads}..., range count,
//   {source, start, end, bytes[end - start]}...
// and nothing after the last range.
constexpr char kMagic[8] = {'W', 'D', 'C', 'B', 'O', 'S', 'S', '1'};

struct Closer { void operator()(std::FILE* f) const { std::fclose(f); } };
using File = std::unique_ptr<std::FILE, Closer>;

bool put_u32(std::FILE* f, uint32_t v) {
    const uint8_t b[4]{uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v)};
    return std::fwrite(b, 1, 4, f) == 4;
}
bool get_u32(std::FILE* f, uint32_t& v) {
    uint8_t b[4];
    if (std::fread(b, 1, 4, f) != 4) return false;
    v = uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
    return true;
}
// Host word-swapped storage <-> big-endian file bytes; sizes are multiples of 4.
bool put_image(std::FILE* f, const std::vector<uint8_t>& image) {
    std::vector<uint8_t> out(image.size());
    for (size_t i = 0; i < image.size(); ++i) out[i] = image[i ^ 3];
    return std::fwrite(out.data(), 1, out.size(), f) == out.size();
}
bool get_image(std::FILE* f, std::vector<uint8_t>& image, uint32_t bytes) {
    std::vector<uint8_t> in(bytes);
    if (std::fread(in.data(), 1, bytes, f) != bytes) return false;
    image.resize(bytes);
    for (size_t i = 0; i < bytes; ++i) image[i ^ 3] = in[i];
    return true;
}

const char* prefix() {
    static const char* value = std::getenv("WDC_BOSS_CAPTURE");
    return value && *value ? value : nullptr;
}
std::atomic<bool> armed{false};
unsigned captured = 0; // graphics worker
}

RdpRange copy_range(Source source, const uint8_t* memory, uint32_t start, uint32_t end) {
    RdpRange range{source, start, end, std::vector<uint8_t>(end - start)};
    for (uint32_t a = start; a < end; ++a) range.bytes[a - start] = memory[a ^ 3];
    return range;
}

void record_status(std::vector<StatusRun>& runs, uint32_t value) {
    if (!runs.empty() && runs.back().value == value) ++runs.back().reads;
    else runs.push_back({value, 1});
}

uint32_t StatusPlayback::next(uint32_t fallback) {
    if (exhausted()) { ++extra; return fallback; }
    const uint32_t value = (*runs)[index].value;
    if (++used == (*runs)[index].reads) { ++index; used = 0; }
    return value;
}
bool StatusPlayback::exhausted() const { return index == runs->size(); }

bool WriteJournal::before_write(const uint8_t* rdram, uint32_t address, uint32_t bytes) {
    if (address > kRdramBytes || bytes > kRdramBytes - address) return false;
    for (uint32_t a = address; a < address + bytes; ++a) {
        const uint32_t index = a ^ 3;
        if (written[index]) continue;
        written[index] = true;
        original.emplace_back(index, rdram[index]);
    }
    return true;
}
void WriteJournal::restore(std::vector<uint8_t>& image) const {
    for (const auto& [index, value] : original) image[index] = value;
}

bool save(const Capture& c, const std::string& path, std::string& error) {
    if (c.dmem.size() != kDmemBytes || c.rdram.size() != kRdramBytes) {
        error = "capture memory images have the wrong size";
        return false;
    }
    if (c.status_reads.size() > kMaxStatusRuns || c.output.size() > kMaxRdpRanges) {
        error = "capture has more status runs or RDP ranges than a load accepts";
        return false;
    }
    File f(std::fopen(path.c_str(), "wb"));
    if (!f) { error = "cannot create " + path; return false; }
    bool ok = std::fwrite(kMagic, 1, 8, f.get()) == 8;
    for (uint32_t word : c.task) ok = ok && put_u32(f.get(), word);
    ok = ok && put_u32(f.get(), c.sp_status);
    for (uint32_t word : c.dp) ok = ok && put_u32(f.get(), word);
    ok = ok && put_image(f.get(), c.dmem) && put_image(f.get(), c.rdram);
    ok = ok && put_u32(f.get(), uint32_t(c.status_reads.size()));
    for (const auto& run : c.status_reads) ok = ok && put_u32(f.get(), run.value) && put_u32(f.get(), run.reads);
    ok = ok && put_u32(f.get(), uint32_t(c.output.size()));
    for (const auto& r : c.output) {
        ok = ok && put_u32(f.get(), uint32_t(r.source)) && put_u32(f.get(), r.start) && put_u32(f.get(), r.end) &&
            std::fwrite(r.bytes.data(), 1, r.bytes.size(), f.get()) == r.bytes.size();
    }
    ok = std::fclose(f.release()) == 0 && ok;
    if (!ok) {
        std::remove(path.c_str());
        error = "failed writing " + path + "; removed";
    }
    return ok;
}

bool load(const std::string& path, Capture& out, std::string& error) {
    File f(std::fopen(path.c_str(), "rb"));
    if (!f) { error = "cannot open " + path; return false; }
    auto fail = [&](const char* why) { error = path + ": " + why; return false; };
    Capture c;
    char magic[8];
    if (std::fread(magic, 1, 8, f.get()) != 8 || std::memcmp(magic, kMagic, 8) != 0) return fail("not a WDCBOSS1 capture");
    bool ok = true;
    for (uint32_t& word : c.task) ok = ok && get_u32(f.get(), word);
    ok = ok && get_u32(f.get(), c.sp_status);
    for (uint32_t& word : c.dp) ok = ok && get_u32(f.get(), word);
    if (!ok || !get_image(f.get(), c.dmem, kDmemBytes) || !get_image(f.get(), c.rdram, kRdramBytes))
        return fail("truncated header or memory image");
    uint32_t count;
    if (!get_u32(f.get(), count) || count > kMaxStatusRuns) return fail("bad status run count");
    c.status_reads.resize(count);
    for (auto& run : c.status_reads) {
        if (!get_u32(f.get(), run.value) || !get_u32(f.get(), run.reads)) return fail("truncated status runs");
        if (!run.reads) return fail("status run with no reads");
    }
    if (!get_u32(f.get(), count) || count > kMaxRdpRanges) return fail("bad RDP range count");
    c.output.resize(count);
    for (auto& r : c.output) {
        uint32_t source;
        if (!get_u32(f.get(), source) || !get_u32(f.get(), r.start) || !get_u32(f.get(), r.end))
            return fail("truncated RDP range");
        if (source > uint32_t(Source::Dmem)) return fail("unknown RDP range source");
        r.source = Source(source);
        const uint32_t limit = r.source == Source::Dmem ? kDmemBytes : kRdramBytes;
        if (r.start > r.end || r.end > limit || ((r.start | r.end) & 7)) return fail("RDP range out of bounds");
        r.bytes.resize(r.end - r.start);
        if (std::fread(r.bytes.data(), 1, r.bytes.size(), f.get()) != r.bytes.size()) return fail("truncated RDP bytes");
    }
    if (std::fgetc(f.get()) != EOF) return fail("trailing bytes after the last RDP range");
    out = std::move(c);
    return true;
}

void request() {
    if (!prefix()) return;
    armed.store(true);
    std::fprintf(stderr, "[wdc] BOSS capture armed for the next graphics task\n");
}

bool take_request(std::string& path) {
    if (!prefix() || !armed.exchange(false)) return false;
    // Numbering restarts with each process; an existing file is replaced.
    path = std::string(prefix()) + "-" + std::to_string(captured++) + ".boss";
    return true;
}
}
