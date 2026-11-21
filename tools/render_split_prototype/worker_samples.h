// Windows-only diagnostic sampling of the graphics worker during owned BOSS
// tasks. No guest access. The game thread stops this after renderer completion.
#pragma once
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <unordered_map>
#include <windows.h>

namespace prototype {
class WorkerSamples {
    HANDLE target_ = nullptr;
    std::thread thread_;
    std::atomic<bool> active_{false}, quit_{false};
    std::unordered_map<uint64_t, uint64_t> samples_;
    uint64_t outside_ = 0, failures_ = 0;

    void loop() {
        HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x2, TIMER_ALL_ACCESS);
        if (!timer) timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        if (!timer) std::_Exit(28);
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        const auto end = base + nt->OptionalHeader.SizeOfImage;
        while (!quit_) {
            LARGE_INTEGER due;
            due.QuadPart = -2500; // 250 us, requested; actual sampling varies.
            if (!SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE) ||
                    WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0) std::_Exit(28);
            if (!active_) continue;
            if (SuspendThread(target_) == DWORD(-1)) { ++failures_; continue; }
            CONTEXT context{};
            context.ContextFlags = CONTEXT_CONTROL;
            const bool valid = GetThreadContext(target_, &context);
            const bool owned = active_;
            // The worker may hold allocator/output locks. Resume it before
            // allocation, aggregation, logging, or taking any application lock.
            if (ResumeThread(target_) == DWORD(-1)) std::_Exit(28);
            if (!valid) { ++failures_; continue; }
            if (!owned) continue;
            if (context.Rip >= base && context.Rip < end) ++samples_[context.Rip - base];
            else ++outside_;
        }
        CloseHandle(timer);
    }
public:
    // Called only on the graphics worker; create a real handle to that worker.
    void begin() {
        if (!target_) {
            if (!DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(),
                    &target_, THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, 0)) std::_Exit(28);
            thread_ = std::thread([this] { loop(); });
        }
        active_ = true;
    }
    void end() { active_ = false; }
    // Called only after the game thread's renderer-completion fence, so begin
    // has finished and no further owned task can use this collector.
    void finish() {
        active_ = false;
        quit_ = true;
        if (thread_.joinable()) thread_.join();
        if (target_) CloseHandle(target_);
        target_ = nullptr;
        uint64_t total = 0;
        for (const auto& sample : samples_) total += sample.second;
        auto* file = std::fopen("worker.samples", "w");
        if (!file) std::_Exit(28);
        std::fprintf(file, "# owned graphics worker interval_us=250 in_image=%llu outside_image=%llu failures=%llu\n",
            (unsigned long long)total, (unsigned long long)outside_, (unsigned long long)failures_);
        for (const auto& [rva, count] : samples_)
            std::fprintf(file, "%llx %llu\n", (unsigned long long)rva, (unsigned long long)count);
        if (std::fclose(file) != 0 || !total || failures_) std::_Exit(28);
    }
};
}
