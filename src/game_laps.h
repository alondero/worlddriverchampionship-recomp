#pragma once

namespace wdc::laps {
// Counts the Quick Race setup can apply. 0 in settings means the course count.
inline bool supported(int laps) {
    return (laps >= 1 && laps <= 10) || laps == 15 || laps == 20;
}
// SDL thread publishes the saved choice. The game thread reads it when the
// next Arcade Race or Time Attack is built.
void configure(int quick_race_laps);
}
