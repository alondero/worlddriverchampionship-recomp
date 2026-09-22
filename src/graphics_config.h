#pragma once
#include <filesystem>
#include <string>

namespace wdc::graphics {
// Host-owned values. No guest memory or VI/simulation timing is modified.
struct Settings {
    int resolution = 0; // 0: automatic window integer scale; 1..8: native multiplier
    int aspect = 0; // 0: original; 1: experimental expand; 2: experimental 16:9
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
void load(const std::filesystem::path& path);
Settings snapshot();
bool apply(Settings settings); // publishes even if saving fails; reports failure
std::string describe(const Settings& settings);
}
