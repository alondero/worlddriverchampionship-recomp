// Offline benchmark and regression check for one captured BOSS graphics task
// (WDC_BOSS_CAPTURE). Re-runs the translated microcode against the capture's
// RDRAM view and status-read timeline, requires byte-identical RDP output on
// every iteration, and reports per-task time. See tools/boss/README.md.
#include "boss_capture.h"
#include "boss_rsp.h"
#include "librecomp/rsp.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <numeric>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#endif

extern "C" [[noreturn]] void wdc_unimplemented(const char* name) {
    std::fprintf(stderr, "wdc_boss_bench: unsupported BOSS operation: %s\n", name);
    std::exit(3);
}
// The bench installs a sink that fails loudly: a replay must never reach the
// game, so a game event during one is a defect in the device, not something
// to pass over in silence.
void reject_game_event(unsigned) { wdc_unimplemented("game event during replay"); }

namespace {
namespace capture = wdc::boss_capture;
// DMA addresses are masked to 24 bits, so give translated BOSS 16 MiB of
// host RDRAM; the capture fills the first 8 MiB.
constexpr uint32_t kHostRdram = 0x1000000;

// The recorded output in host layout, so the sink can compare each range in
// place with memcmp and add no copies to the measured time. Ranges start on
// eight-byte boundaries, so guest byte start + k is host byte k ^ 3.
struct Expected { capture::Source source; uint32_t start, end; std::vector<uint8_t> host; };
struct Check {
    const std::vector<Expected>* want;
    size_t next = 0;
    std::string failure;
};

void compare(void* context, uint8_t* memory, uint32_t start, uint32_t end) {
    auto& check = *static_cast<Check*>(context);
    if (!check.failure.empty()) return;
    const auto source = memory == dmem ? capture::Source::Dmem : capture::Source::Rdram;
    char text[160];
    if (check.next == check.want->size()) {
        std::snprintf(text, sizeof(text), "extra range %u:%06x..%06x after %zu recorded", unsigned(source), start, end, check.next);
        check.failure = text;
        return;
    }
    const auto& w = (*check.want)[check.next++];
    if (w.source != source || w.start != start || w.end != end) {
        std::snprintf(text, sizeof(text), "range %zu: recorded %u:%06x..%06x, replayed %u:%06x..%06x",
            check.next - 1, unsigned(w.source), w.start, w.end, unsigned(source), start, end);
        check.failure = text;
    } else if (std::memcmp(memory + start, w.host.data(), w.host.size()) != 0) {
        uint32_t at = start;
        while (memory[at ^ 3] == w.host[(at - start) ^ 3]) ++at;
        std::snprintf(text, sizeof(text), "range %zu (%06x..%06x): first byte difference at %06x",
            check.next - 1, start, end, at);
        check.failure = text;
    }
}

#ifdef _WIN32
// Samples the benchmark thread's instruction pointer every ~250 us while a
// replay is running. Offsets are relative to the executable's load address;
// tools/boss/profile_report.py maps them to RSP labels in src/aspMain.cpp.
class Sampler {
public:
    explicit Sampler(HANDLE target) : target(target), thread([this] { loop(); }) {}
    ~Sampler() { stop(); }
    std::atomic<bool> active{false};
    void stop() {
        quit = true;
        if (thread.joinable()) thread.join();
    }
    // Call after stop().
    bool write(const char* path, const char* exe) const {
        std::FILE* f = std::fopen(path, "w");
        if (!f) return false;
        uint64_t total = 0;
        for (const auto& [rva, n] : samples) total += n;
        std::fprintf(f, "# wdc_boss_bench samples exe=%s interval_us=250 samples=%llu\n", exe, (unsigned long long)total);
        for (const auto& [rva, n] : samples) std::fprintf(f, "%llx %llu\n", (unsigned long long)rva, (unsigned long long)n);
        return std::fclose(f) == 0;
    }
private:
    void loop() {
        constexpr DWORD kHighResolution = 0x2; // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, kHighResolution, TIMER_ALL_ACCESS);
        if (!timer) timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        // Samples are offsets into the executable's image. Code in a DLL
        // (the C runtime, for one) is outside that image and would resolve
        // to a meaningless offset, so it is dropped.
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto image_end = base + nt->OptionalHeader.SizeOfImage;
        while (!quit) {
            LARGE_INTEGER due;
            due.QuadPart = -2500; // 100 ns units, relative
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer, INFINITE);
            if (!active || SuspendThread(target) == DWORD(-1)) continue;
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            const bool sampled = GetThreadContext(target, &context) && active;
            // Resume before touching the map: the suspended thread may hold
            // the heap lock that an insertion needs.
            ResumeThread(target);
            if (sampled && context.Rip >= base && context.Rip < image_end) ++samples[context.Rip - base];
        }
        CloseHandle(timer);
    }
    HANDLE target;
    std::atomic<bool> quit{false};
    std::unordered_map<uint64_t, uint64_t> samples;
    std::thread thread;
};
#endif

int usage() {
    std::fprintf(stderr, "usage: wdc_boss_bench <capture.boss> [--iterations N] [--profile samples.txt]\n");
    return 2;
}
}

int main(int argc, char** argv) {
    const char* path = nullptr;
    const char* profile = nullptr;
    unsigned iterations = 200;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--iterations" && i + 1 < argc) {
            // Only a plain count is accepted: strtoul would read "-1" as a
            // huge number of iterations.
            const std::string value = argv[++i];
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) return usage();
            iterations = unsigned(std::strtoul(value.c_str(), nullptr, 10));
        }
        else if (arg == "--profile" && i + 1 < argc) profile = argv[++i];
        else if (!path && arg[0] != '-') path = argv[i];
        else return usage();
    }
    if (!path || !iterations) return usage();
#ifndef _WIN32
    if (profile) { std::fprintf(stderr, "wdc_boss_bench: --profile is implemented on Windows only\n"); return 2; }
#endif

    capture::Capture task;
    std::string error;
    if (!capture::load(path, task, error)) {
        std::fprintf(stderr, "wdc_boss_bench: %s\n", error.c_str());
        return 2;
    }
    uint64_t reads = 0, output_bytes = 0;
    for (const auto& run : task.status_reads) reads += run.reads;
    for (const auto& range : task.output) output_bytes += range.bytes.size();
    std::printf("capture %s: ucode=%08x data=%08x+%x, %llu status reads in %zu runs, %zu RDP ranges (%llu bytes)\n",
        path, task.task[4], task.task[12], task.task[13], (unsigned long long)reads, task.status_reads.size(),
        task.output.size(), (unsigned long long)output_bytes);

    recomp::rsp::constants_init();
    std::vector<Expected> expected;
    for (const auto& range : task.output) {
        Expected e{range.source, range.start, range.end, std::vector<uint8_t>(range.bytes.size())};
        for (size_t k = 0; k < range.bytes.size(); ++k) e.host[k ^ 3] = range.bytes[k];
        expected.push_back(std::move(e));
    }
    std::vector<uint8_t> rdram(kHostRdram);
    std::vector<double> ms;
    // A replay must not reach the game. The sink fails loudly if one does.
    wdc::rsp::set_game_event_sink(&reject_game_event);
#ifdef _WIN32
    HANDLE self = nullptr;
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &self, 0, FALSE, DUPLICATE_SAME_ACCESS);
    std::unique_ptr<Sampler> sampler = profile ? std::make_unique<Sampler>(self) : nullptr;
#endif
    // One untimed warm-up iteration, then `iterations` timed ones; every
    // iteration starts from the captured RDRAM and must match exactly.
    for (unsigned i = 0; i <= iterations; ++i) {
        std::fill(rdram.begin() + capture::kRdramBytes, rdram.end(), 0);
        std::memcpy(rdram.data(), task.rdram.data(), capture::kRdramBytes);
        Check check{&expected};
#ifdef _WIN32
        if (sampler && i) sampler->active = true;
#endif
        const auto start = std::chrono::steady_clock::now();
        const auto result = wdc::rsp::replay(task, rdram.data(), compare, &check);
        const auto stop = std::chrono::steady_clock::now();
#ifdef _WIN32
        if (sampler) sampler->active = false;
#endif
        if (!result.finished || !result.reads_consumed || result.extra_reads) {
            std::printf("FAIL iteration %u: finished=%d all_status_reads=%d extra_status_reads=%llu\n",
                i, result.finished, result.reads_consumed, (unsigned long long)result.extra_reads);
            return 1;
        }
        if (check.failure.empty() && check.next != expected.size())
            check.failure = "replayed " + std::to_string(check.next) + " of " + std::to_string(expected.size()) + " ranges";
        if (!check.failure.empty()) {
            std::printf("FAIL iteration %u: RDP output differs: %s\n", i, check.failure.c_str());
            return 1;
        }
        if (i) ms.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
    }
    std::sort(ms.begin(), ms.end());
    const double mean = std::accumulate(ms.begin(), ms.end(), 0.0) / double(ms.size());
    std::printf("check: %u iterations byte-identical to the capture\n", iterations);
    std::printf("time ms/task: min %.3f  median %.3f  mean %.3f  p90 %.3f  max %.3f\n",
        ms.front(), ms[ms.size() / 2], mean, ms[ms.size() * 9 / 10], ms.back());
#ifdef _WIN32
    if (sampler) {
        sampler->stop();
        if (!sampler->write(profile, argv[0])) {
            std::fprintf(stderr, "wdc_boss_bench: cannot write %s\n", profile);
            return 2;
        }
        std::printf("profile: samples written to %s\n", profile);
    }
#endif
    return 0;
}
