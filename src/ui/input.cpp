#include "input.h"
#include "frontend.h"
#include "game_driving.h"
#include "rumble.h"
#include "rumble_output.h"
#include "recompinput/recompinput.h"
#include "recompinput/input_events.h"
#include "recompinput/profiles.h"
#include "recompui/config.h"
#include <atomic>
#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_map>

namespace wdc::input {
namespace {
std::recursive_mutex input_mutex;
// One coherent, host-owned snapshot. Signed axes use 16-bit fixed point in
// [-32767,32767]; no guest RAM or SDL object crosses the runtime callback.
std::atomic<uint64_t> snapshot{};
bool wait_for_release{}; // SDL thread only.
bool focused = true;
std::unordered_map<SDL_JoystickID, RumbleOutput> rumble_outputs;
void apply_rumble(uint16_t strength) {
    // This is WDC's only SDL rumble sender. Do not call recompinput::update_rumble
    // alongside it: its smoothing and long duration conflict with the ROM pulses
    // and the bounded lease. General supplies amplitude and persistence; see the input reference.
    // Borrow the frontend's open handles. Single-player input merges these same
    // controllers; do not create a second device manager or open extra handles.
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        const auto id = SDL_JoystickGetDeviceInstanceID(i);
        auto* controller = recompinput::get_controller_from_joystick_id(id);
        if (!controller || !SDL_GameControllerGetAttached(controller)) continue;
        // Match the shared frontend's high-frequency motor routing. Short
        // leases also stop the device if the host pump stalls or exits abruptly.
        rumble_outputs[id].apply(strength, [&](uint16_t value) {
            return SDL_GameControllerRumble(controller, 0, value, value ? 150 : 0);
        });
    }
}
float pedal_value(const std::string& pedal) {
    using namespace recompinput;
    const auto& config = recompui::config::get_config("driving-controls");
    if (std::get<uint32_t>(config.get_option_value(pedal + "_mode")) != 1) return 0;
    const auto axis = std::get<uint32_t>(config.get_option_value(pedal + "_axis"));
    if (axis == 0 || axis > SDL_CONTROLLER_AXIS_MAX) return 0;
    const bool negative = std::get<bool>(config.get_option_value(pedal + "_negative"));
    // In single-player mode the pinned frontend shares connected controllers;
    // get_player(0).controller is unassigned. Its axis evaluator owns hotplug
    // and uses the strongest half-axis across those devices, just like N64 input.
    const float value = get_input_analog(0, InputField::controller_analog(
        SDL_GameControllerAxis(axis - 1), !negative));
    auto number = [&](const char* suffix, double low, double high, double fallback) {
        const double v = std::get<double>(config.get_option_value(pedal + suffix));
        return float(std::isfinite(v) ? std::clamp(v, low, high) : fallback);
    };
    const float deadzone = number("_deadzone", 0, 0.95, 0.05);
    const float saturation = std::max(deadzone + 0.01f, number("_saturation", 0.05, 1, 1));
    return std::clamp((value - deadzone) / (saturation - deadzone), 0.0f, 1.0f);
}
bool gameplay_held(driving::Pedals pedals) {
    using namespace recompinput;
    if (pedals.throttle > 0 || pedals.brake > 0) return true;
    const float deadzone = float(recompui::config::general::get_joystick_deadzone() / 100.0);
    for (const auto device : {InputDevice::Keyboard, InputDevice::Controller}) {
        const int profile = profiles::get_input_profile_for_player(0, device);
        for (int i = 0; i < int(GameInput::COUNT); ++i) {
            const auto input = GameInput(i);
            if (get_game_input_is_menu(input)) continue;
            const bool stick = input == GameInput::X_AXIS_POS || input == GameInput::X_AXIS_NEG ||
                input == GameInput::Y_AXIS_POS || input == GameInput::Y_AXIS_NEG;
            for (size_t binding = 0; binding < num_bindings_per_input; ++binding) {
                const auto field = profiles::get_input_binding(profile, input, binding);
                const float value = get_input_analog(0, field);
                if (stick ? value > (device == InputDevice::Keyboard ? 0.0f : deadzone) : value >= axis_digital_threshold)
                    return true;
            }
        }
    }
    return false;
}
}
std::recursive_mutex& mutex() { return input_mutex; }

void configure() {
    using namespace recompinput;
    using G = GameInput;
    using F = InputField;
    players::set_single_player_mode(true);
    // Preserve the port's existing keyboard layout. Move C buttons away from
    // arrow-key steering; every N64 input remains visible and rebindable.
    for (auto [input, key] : {
        std::pair{G::A, SDL_SCANCODE_X}, {G::B, SDL_SCANCODE_Z},
        {G::Z, SDL_SCANCODE_SPACE}, {G::R, SDL_SCANCODE_RSHIFT},
        {G::L, SDL_SCANCODE_LSHIFT}, {G::START, SDL_SCANCODE_RETURN},
        {G::C_UP, SDL_SCANCODE_I}, {G::C_DOWN, SDL_SCANCODE_K},
        {G::C_LEFT, SDL_SCANCODE_J}, {G::C_RIGHT, SDL_SCANCODE_L},
        {G::DPAD_UP, SDL_SCANCODE_T}, {G::DPAD_DOWN, SDL_SCANCODE_G},
        {G::DPAD_LEFT, SDL_SCANCODE_F}, {G::DPAD_RIGHT, SDL_SCANCODE_H}}) {
        set_default_mapping_for_keyboard(input, {F::keyboard(key)});
    }
    set_default_mapping_for_keyboard(G::X_AXIS_NEG, {F::keyboard(SDL_SCANCODE_LEFT), F::keyboard(SDL_SCANCODE_A)});
    set_default_mapping_for_keyboard(G::X_AXIS_POS, {F::keyboard(SDL_SCANCODE_RIGHT), F::keyboard(SDL_SCANCODE_D)});
    set_default_mapping_for_keyboard(G::Y_AXIS_POS, {F::keyboard(SDL_SCANCODE_UP), F::keyboard(SDL_SCANCODE_W)});
    set_default_mapping_for_keyboard(G::Y_AXIS_NEG, {F::keyboard(SDL_SCANCODE_DOWN), F::keyboard(SDL_SCANCODE_S)});
    // Adjacent face buttons for accelerate/brake. Keep the right stick for C.
    set_default_mapping_for_controller(G::B, {F::controller_digital(SDL_CONTROLLER_BUTTON_B)});
    set_default_mapping_for_controller(G::BACK_MENU, {F::controller_digital(SDL_CONTROLLER_BUTTON_B)});
    set_default_mapping_for_controller(G::C_RIGHT, {F::controller_analog(SDL_CONTROLLER_AXIS_RIGHTX)});
    set_game_input_name(G::A, "A / Accelerate / Select");
    set_game_input_name(G::B, "B / Brake / Back");
    set_game_input_name(G::START, "Start / Pause");
    set_game_input_description(G::TOGGLE_MENU,
        "Open or close settings. Pause the race with Start first; settings do not pause the game. Escape or F1 also opens settings.");
}

void stop_rumble() {
    std::lock_guard lock(input_mutex);
    rumble::stop();
    apply_rumble(0);
}

void handle_event(SDL_Event& event) {
    std::lock_guard lock(input_mutex);
    if (event.type == SDL_WINDOWEVENT) {
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) { focused = false; stop_rumble(); }
        if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) focused = true;
    }
    if (event.type == SDL_CONTROLLERDEVICEADDED || event.type == SDL_CONTROLLERDEVICEREMOVED)
        stop_rumble(); // Clear pending effects before a handle is closed/reassigned.
    if (event.type == SDL_CONTROLLERDEVICEREMOVED) rumble_outputs.erase(event.cdevice.which);
    recompinput::process_event(event);
}

void sample(bool captured) {
    std::lock_guard lock(input_mutex);
    recompinput::finish_events();
    recompinput::poll_inputs();
    uint16_t buttons{};
    float x{}, y{};
    recompinput::profiles::get_n64_input(0, &buttons, &x, &y);
    driving::Pedals pedals{pedal_value("throttle"), pedal_value("brake")};
    // Native reads are neutral while the UI captures input. Once it closes,
    // keep blocking until the mapped buttons and deadzoned stick are neutral.
    captured |= ui::captures_input();
    if (captured) wait_for_release = true;
    else if (wait_for_release && !gameplay_held(pedals)) wait_for_release = false;
    if (captured || wait_for_release) { buttons = 0; x = y = 0; pedals = {}; }
    const double configured_strength = recompui::config::general::get_rumble_strength();
    const double strength = std::isfinite(configured_strength) ? std::clamp(configured_strength, 0.0, 100.0) : 0;
    const bool muted = captured || wait_for_release || !focused || strength == 0;
    if (muted) rumble::stop();
    // A concurrent guest Start must not override the SDL thread's capture gate.
    apply_rumble(!muted && rumble::active() ? uint16_t(strength * 65535.0 / 100.0) : 0);
    driving::publish(0, pedals);
    const auto sx = static_cast<int16_t>(std::lround(x * 32767.0f));
    const auto sy = static_cast<int16_t>(std::lround(y * 32767.0f));
    snapshot.store(uint64_t(buttons) | (uint64_t(std::bit_cast<uint16_t>(sx)) << 16) |
        (uint64_t(std::bit_cast<uint16_t>(sy)) << 32), std::memory_order_release);
}

bool read(int port, uint16_t* buttons, float* x, float* y) {
    *buttons = 0; *x = *y = 0;
    if (port != 0) return false;
    if (ui::captures_input()) return true;
    const uint64_t value = snapshot.load(std::memory_order_acquire);
    *buttons = uint16_t(value);
    *x = std::bit_cast<int16_t>(uint16_t(value >> 16)) / 32767.0f;
    *y = std::bit_cast<int16_t>(uint16_t(value >> 32)) / 32767.0f;
    return true;
}
}
