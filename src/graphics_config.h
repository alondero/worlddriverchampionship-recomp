#pragma once
#include <filesystem>
#include <optional>
#include <string>

namespace wdc::graphics {
// User-facing race frame-rate modes. The implementation keeps simulation,
// renderer-target, and pose-interpolation mechanisms separate behind this
// single choice.
enum class FrameRateMode {
    Original,
    Native60,
    InterpolateDisplay,
    InterpolateCustom,
    LegacyDisplay,
    LegacyCustom,
};

// Host-owned values. custom_rate is the remembered target used by the two
// custom frame-rate modes; legacy presentation rounds it to a whole-number
// target without erasing the remembered precision.
struct Settings {
    int resolution = 0; // 0: automatic window integer scale; 1..8: native multiplier
    int aspect = 1; // 0: Original 4:3; 1: wider camera matching the window
    FrameRateMode frame_rate = FrameRateMode::Original;
    double custom_rate = 60;
    int samples = 1; // 1, 2, 4, 8
    int filtering = 1; // nearest, linear, pixel scaling (presentation filter)
    int precision = 0; // automatic, standard, high
    bool fullscreen = false;
    int hud = 1; // 0: original; 1: clamp edge travel to 16:9; 2: full width
    bool full_car_detail = false;
    bool full_track_detail = false;
    int draw_distance = 1; // 1..8: far plane/section scale; 16: bounded Unlimited mode
    int quick_race_laps = 0; // 0: course default; else 1..10, 15, or 20. Next Arcade or Time Attack.
    bool operator==(const Settings&) const = default;
};
Settings normalize(Settings settings);
int legacy_custom_target(const Settings& settings);
// Load/save on SDL main thread. Renderer takes a locked copy at a frame boundary.
// Invalid files are preserved and defaults used; later explicit changes may save.
// Load selects the save path; apply without one fails before staging any write.
void load(const std::filesystem::path& path, std::optional<bool> initial_fullscreen = std::nullopt);
Settings snapshot();
bool windowed_with_saved_fullscreen();
void fallback_to_windowed();
bool apply(Settings settings, bool reset_window_mode = false); // publishes even if saving fails
std::string describe(const Settings& settings);
Settings cycle_frame_rate(Settings settings);
}
