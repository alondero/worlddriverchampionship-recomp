#pragma once
#include <atomic>

namespace wdc::ui {
// SDL publishes requests; the presentation thread consumes them. Capture stays
// active through a pending close and any Apply/Discard prompt. An opening
// request blocks guest input immediately, before the next presentation frame.
class OverlayState {
public:
    enum class Request { None, Open, Close };
    void open() {
        capture_.store(true, std::memory_order_release);
        pending_.store(Request::Open, std::memory_order_release);
    }
    void close() { pending_.store(Request::Close, std::memory_order_release); }
    Request take_request() {
        // The previous frame can publish "closed" after SDL requests Open.
        // Restore capture before removing the pending request's input gate.
        if (pending_.load(std::memory_order_acquire) == Request::Open)
            capture_.store(true, std::memory_order_release);
        return pending_.exchange(Request::None, std::memory_order_acq_rel);
    }
    void publish_capture(bool capture) { capture_.store(capture, std::memory_order_release); }
    bool captures_input() const {
        return pending_.load(std::memory_order_acquire) != Request::None ||
               capture_.load(std::memory_order_acquire);
    }
private:
    std::atomic<Request> pending_{Request::None};
    std::atomic<bool> capture_{false};
};
}
