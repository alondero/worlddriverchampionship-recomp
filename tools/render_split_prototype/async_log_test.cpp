#include "async_log.h"
#include <cassert>
#include <future>
#include <iostream>

int main() {
    using namespace std::chrono_literals;
    std::promise<void> entered, release;
    auto released = release.get_future().share();
    std::string output;
    unsigned calls = 0;
    prototype::AsyncLog writer(8, [&](std::string_view bytes) {
        if (!calls++) { entered.set_value(); released.wait(); }
        output += bytes;
        return true;
    });
    assert(writer.submit("1234567\n"));
    entered.get_future().wait();
    assert(writer.submit("abcdefg\n"));
    auto blocked = std::async(std::launch::async, [&] { return writer.submit("last\n"); });
    assert(blocked.wait_for(50ms) == std::future_status::timeout);
    release.set_value();
    assert(blocked.get());
    auto stats = writer.finish();
    assert(output == "1234567\nabcdefg\nlast\n");
    assert(stats.accepted == 3 && stats.written == 3);
    assert(stats.accepted_bytes == output.size() && stats.written_bytes == output.size());
    assert(stats.max_queued_bytes == 8 && stats.backpressure == 1 && !stats.failed);
    assert(!writer.submit("closed\n"));
    assert(writer.finish().written == 3);

    prototype::AsyncLog failed(8, [](std::string_view) { return false; });
    assert(failed.submit("failure\n"));
    auto bad = failed.finish();
    assert(bad.failed && bad.accepted == 1 && bad.written == 0 && bad.written_bytes == 0);
    assert(!failed.submit("x\n"));
    prototype::AsyncLog throwing(8, [](std::string_view) -> bool { throw 1; });
    assert(throwing.submit("x\n"));
    assert(throwing.finish().failed);

    prototype::AsyncLog bounded(8, [](std::string_view) { return true; });
    assert(!bounded.submit("oversized\n") && !bounded.submit(""));
    assert(bounded.submit("fits\n"));
    assert(bounded.finish().written == 1);
    std::cout << "async log ordering, bounds, backpressure, drain and failures passed\n";
}
