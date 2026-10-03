#pragma once
#include <filesystem>
#include <string>

namespace wdc::graphics {
// Host-owned values. Aspect also controls the game camera; VI/timing stay native.
struct Settings {
    int resolution = 0; // 0: automatic window integer scale; 1..8: native multiplier
    int aspect = 1; // 0: Original 4:3; 1: wider camera matching the window
    int rate = 0; // 0: original; -1: display; 30..360: presentation target
    int samples = 1; // 1, 2, 4, 8
    int filtering = 1; // nearest, linear, pixel scaling (presentation filter)
    int precision = 0; // automatic, standard, high
    bool fullscreen = false;
    bool operator==(const Settings&) const = default;
};
Settings normalize(Settings settings);
// Load/save on SDL main thread. Renderer takes a locked copy at a frame boundary.
// Invalid files are preserved and defaults used; later explicit changes may save.
// Load selects the save path; apply without one fails before staging any write.
void load(const std::filesystem::path& path);
Settings snapshot();
bool apply(Settings settings); // publishes even if saving fails; reports failure
std::string describe(const Settings& settings);
}
