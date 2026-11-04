#pragma once
#include <string_view>

namespace wdc::warp {
enum class Mode { Arcade = 3, TimeAttack = 5, Practice = 8 };
struct Request {
    int track = 1; // 1..58, ROM course id + 1 (includes reverse layouts).
    int car = 0;   // 0..34, ROM model id.
    Mode mode = Mode::Practice;
    int laps = 0;  // 0 = course default; Arcade accepts 2..5.
};
bool parse(std::string_view text, Request& result);
bool request(Request value); // Thread-safe; newest pending request wins.
void repeat();               // Repeat the last requested setup.
bool configure_from_environment(); // Call on the main thread before starting the guest.
void vi_tick(unsigned vi);         // Host VI callback publishes the optional scripted repeat.
}
