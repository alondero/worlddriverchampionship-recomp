#pragma once

#include <array>
#include <cstddef>

namespace wdc::cheats {
enum class Cheat { Tracks, Gt1Cars, Gt2Cars, Count };
struct Description {
    Cheat cheat;
    const char* id;
    const char* name;
    const char* description;
};
// Session-only. The settings tab publishes these flags; menu accessors read them.
// Nothing here is stored in a config file or in the Controller Pak save.
inline constexpr std::array<Description, static_cast<std::size_t>(Cheat::Count)> catalog{{
    {Cheat::Tracks, "unlock_tracks", "Unlock all tracks",
     "Show every track as available the next time a track list is opened. Saved progress is not changed, and turning this off brings the saved locks back. Championship event locks stay as saved. Resets when the application closes."},
    {Cheat::Gt1Cars, "unlock_gt1_cars", "Unlock all GT1 cars",
     "Show every GT1 car and its teams as available the next time those lists are opened. Saved progress is not changed, and turning this off brings the saved locks back. The Falcon Interceptor stays on its own unlock. Resets when the application closes."},
    {Cheat::Gt2Cars, "unlock_gt2_cars", "Unlock all GT2 cars",
     "Show every GT2 car and its teams as available the next time those lists are opened. Saved progress is not changed, and turning this off brings the saved locks back. Resets when the application closes."},
}};

bool enabled(Cheat cheat);
void set_enabled(Cheat cheat, bool value);
}
