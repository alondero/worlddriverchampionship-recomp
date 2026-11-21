// Host-only prototype evidence writer. No guest memory or renderer locks.
#pragma once
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace prototype {
class AsyncLog {
public:
    struct Stats {
        uint64_t accepted = 0, written = 0, accepted_bytes = 0, written_bytes = 0;
        uint64_t backpressure = 0, wait_us = 0, max_write_us = 0, max_queued_bytes = 0;
        bool failed = false;
    };
    using Sink = std::function<bool(std::string_view)>;
    explicit AsyncLog(size_t limit, Sink sink) : limit_(limit), sink_(std::move(sink)),
        worker_([this] { run(); }) {}
    ~AsyncLog() { finish(); }
    AsyncLog(const AsyncLog&) = delete;
    AsyncLog& operator=(const AsyncLog&) = delete;

    bool submit(std::string_view record) {
        std::unique_lock lock(mutex_);
        if (closed_ || stats_.failed || record.empty() || record.size() > limit_) return false;
        if (queued_.size() + record.size() > limit_) {
            ++stats_.backpressure;
            auto start = Clock::now();
            changed_.wait(lock, [&] { return closed_ || stats_.failed || queued_.size() + record.size() <= limit_; });
            stats_.wait_us += micros(Clock::now() - start);
        }
        if (closed_ || stats_.failed) return false;
        queued_.append(record);
        ++queued_records_;
        ++stats_.accepted;
        stats_.accepted_bytes += record.size();
        stats_.max_queued_bytes = std::max<uint64_t>(stats_.max_queued_bytes, queued_.size());
        changed_.notify_all();
        return true;
    }
    // Caller serializes finish calls. No producer/application lock is needed.
    Stats finish() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
            changed_.notify_all();
        }
        if (worker_.joinable()) worker_.join();
        std::lock_guard lock(mutex_);
        return stats_;
    }
private:
    using Clock = std::chrono::steady_clock;
    static uint64_t micros(Clock::duration time) {
        return std::chrono::duration_cast<std::chrono::microseconds>(time).count();
    }
    void run() {
        std::string batch;
        for (;;) {
            uint64_t records;
            {
                std::unique_lock lock(mutex_);
                changed_.wait(lock, [&] { return closed_ || !queued_.empty(); });
                if (queued_.empty()) return;
                batch.swap(queued_);
                records = queued_records_;
                queued_records_ = 0;
                changed_.notify_all();
            }
            auto start = Clock::now();
            bool ok = false;
            try { ok = sink_(batch); } catch (...) { /* Report failed evidence. */ }
            auto elapsed = micros(Clock::now() - start);
            {
                std::lock_guard lock(mutex_);
                stats_.max_write_us = std::max(stats_.max_write_us, elapsed);
                if (!ok) {
                    stats_.failed = true;
                    changed_.notify_all();
                    return;
                }
                stats_.written += records;
                stats_.written_bytes += batch.size();
            }
            batch.clear();
        }
    }
    const size_t limit_;
    Sink sink_;
    std::mutex mutex_;
    std::condition_variable changed_;
    std::string queued_;
    uint64_t queued_records_ = 0;
    bool closed_ = false;
    Stats stats_;
    std::thread worker_;
};
} // namespace prototype
