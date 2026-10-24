// Exercise the shipped SDL -> recompinput profiles -> WDC snapshot path with
// a virtual gamepad. No ROM, physical controller, window or GPU is required.
#include "ui/input.h"
#include "ui/frontend_settings.h"
#include "game_driving.h"
#include "rumble.h"
#include "graphics_config.h"
#include "recompinput/input_binding.h"
#include "recompinput/profiles.h"
#include "recompui/config.h"
#include "librecomp/game.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

SDL_Window* window = nullptr;
std::vector<recomp::GameEntry> supported_games;
static bool capture{};
static uint16_t rumble_low{}, rumble_high{};
static int rumble_calls{};
static int failed_rumble_calls{};
static bool fail_next_stop{};
static int failed_stop_calls{};
static int SDLCALL rumble_callback(void*, Uint16 low, Uint16 high) {
    rumble_low = low; rumble_high = high; ++rumble_calls;
    if (low == 0 && high == 0 && fail_next_stop) {
        fail_next_stop = false; ++failed_stop_calls;
        return -1;
    }
    return 0;
}
static int SDLCALL failed_rumble_callback(void*, Uint16, Uint16) {
    ++failed_rumble_calls;
    return -1;
}
namespace wdc::ui { bool captures_input() { return capture; } }
namespace wdc::ui { void request_settings(graphics::Settings) {} }
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void drain() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) wdc::input::handle_event(event);
    wdc::input::sample(capture);
}
int main() {
    using namespace recompinput;
    using G = GameInput;
    using F = InputField;
    const auto dir = std::filesystem::temp_directory_path() /
        ("wdc-input-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    try {
        SDL_SetMainReady();
        SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        require(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0, "SDL init failed");
        recomp::register_config_path(dir);
        wdc::input::configure();
        wdc::ui::create_driving_settings();
        recompui::config::create_general_tab({.has_rumble_strength = true});
        recompui::config::create_controls_tab();
        recompui::config::finalize();
        const int kb = profiles::get_sp_keyboard_profile_index();
        const int pad = profiles::get_sp_controller_profile_index();
        require(profiles::get_input_binding(kb, G::A, 0) == F::keyboard(SDL_SCANCODE_X) &&
            profiles::get_input_binding(kb, G::X_AXIS_POS, 0) == F::keyboard(SDL_SCANCODE_RIGHT) &&
            profiles::get_input_binding(kb, G::X_AXIS_POS, 1) == F::keyboard(SDL_SCANCODE_D), "keyboard defaults changed");
        SDL_VirtualJoystickDesc descriptor{};
        descriptor.version = SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
        descriptor.type = SDL_JOYSTICK_TYPE_GAMECONTROLLER;
        descriptor.naxes = SDL_CONTROLLER_AXIS_MAX;
        descriptor.nbuttons = SDL_CONTROLLER_BUTTON_MAX;
        descriptor.Rumble = rumble_callback;
        const int device = SDL_JoystickAttachVirtualEx(&descriptor);
        require(device >= 0 && SDL_IsGameController(device), "virtual gamepad unavailable");
        auto* joystick = SDL_JoystickOpen(device);
        const auto id = SDL_JoystickInstanceID(joystick);
        SDL_Event failed_open{};
        failed_open.type = SDL_CONTROLLERDEVICEADDED; failed_open.cdevice.which = -1;
        wdc::input::handle_event(failed_open); // Failed opens must not dereference an SDL handle.
        drain();
        require(get_controller_from_joystick_id(id) != nullptr, "hotplug not registered");
        uint16_t buttons{}; float x{}, y{};
        auto read = [&] { require(wdc::input::read(0, &buttons, &x, &y), "port 1 missing"); };
        auto button = [&](SDL_GameControllerButton b, bool down) {
            require(SDL_JoystickSetVirtualButton(joystick, b, down) == 0, "button injection failed");
        };
        auto axis = [&](SDL_GameControllerAxis a, int16_t value) {
            require(SDL_JoystickSetVirtualAxis(joystick, a, value) == 0, "axis injection failed");
        };
        // SDL virtual joystick trigger endpoints are signed; SDL maps these
        // to game-controller [0,32767]. Start both pedals at rest.
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768);
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768);
        drain();
        auto& rumble_config = recompui::config::get_config(recompui::config::general::id);
        auto strength = [&](double value) {
            rumble_config.update_option_value(recompui::config::general::options::rumble_strength, value);
        };
        strength(100);
        wdc::rumble::set_motor(0, true); drain();
        require(rumble_calls > 0 && rumble_low == 0 && rumble_high == 65535,
            "motor command did not reach virtual gamepad");
        strength(25); drain();
        require(rumble_high == 16383, "live rumble strength not applied");
        fail_next_stop = true;
        wdc::rumble::stop(); drain(); drain();
        require(failed_stop_calls == 1, "transient stop failure was not exercised exactly once");
        // SDL may cache the previous amplitude after a failed stop. Change the
        // next effect's strength so this assertion observes a real driver write.
        strength(50);
        wdc::rumble::set_motor(0, true); drain();
        require(rumble_high == 32767, "transient stop failure blacklisted a working controller");
        strength(25); drain();
        strength(0); drain();
        require(rumble_high == 0 && !wdc::rumble::active(), "zero strength did not stop rumble");
        strength(100); drain();
        require(rumble_high == 0, "unmuting replayed stale rumble");
        wdc::rumble::set_motor(0, true); drain();
        capture = true; drain();
        require(rumble_high == 0 && !wdc::rumble::active(), "overlay did not stop rumble");
        capture = false; drain();
        require(rumble_high == 0, "closing overlay replayed stale rumble");
        wdc::rumble::set_motor(0, true); drain();
        SDL_Event focus{};
        focus.type = SDL_WINDOWEVENT; focus.window.event = SDL_WINDOWEVENT_FOCUS_LOST;
        wdc::input::handle_event(focus); drain();
        require(rumble_high == 0 && !wdc::rumble::active(), "focus loss left rumble running");
        focus.window.event = SDL_WINDOWEVENT_FOCUS_GAINED;
        wdc::input::handle_event(focus); drain();
        require(rumble_high == 0, "focus regain replayed stale rumble");
        wdc::rumble::set_motor(0, true); drain();
        wdc::input::stop_rumble();
        require(rumble_high == 0, "shutdown did not stop rumble");
        button(SDL_CONTROLLER_BUTTON_A, true);
        axis(SDL_CONTROLLER_AXIS_LEFTX, 16384);
        axis(SDL_CONTROLLER_AXIS_LEFTY, -32768);
        drain(); read();
        require((buttons & 0x8000) && std::abs(x - 0.473684f) < 0.001f && y == 1.0f,
            "accelerate/analog steering/deadzone/Y inversion failed");
        button(SDL_CONTROLLER_BUTTON_B, true);
        button(SDL_CONTROLLER_BUTTON_DPAD_LEFT, true);
        axis(SDL_CONTROLLER_AXIS_RIGHTX, 32767);
        drain(); read();
        require((buttons & 0xC203) == 0xC201, "brake/D-pad/C-right mapping failed");
        capture = true;
        read(); require(buttons == 0 && x == 0 && y == 0, "pending overlay leaked input before sampling");
        drain();
        capture = false;
        drain(); read(); require(buttons == 0 && x == 0 && y == 0, "menu close leaked held gameplay input");
        button(SDL_CONTROLLER_BUTTON_A, false); button(SDL_CONTROLLER_BUTTON_B, false);
        button(SDL_CONTROLLER_BUTTON_DPAD_LEFT, false);
        axis(SDL_CONTROLLER_AXIS_LEFTX, 0); axis(SDL_CONTROLLER_AXIS_LEFTY, 0); axis(SDL_CONTROLLER_AXIS_RIGHTX, 0);
        drain();
        button(SDL_CONTROLLER_BUTTON_A, true);
        drain(); read(); require(buttons == 0x8000, "input did not resume after release");
        require(!wdc::input::read(1, &buttons, &x, &y) && buttons == 0 && x == 0 && y == 0,
            "unconnected port received player 1 input");
        require(!wdc::input::read(-1, &buttons, &x, &y), "invalid port accepted");

        // Opposing bindings can cancel in the final stick value while their
        // physical input is still held. They must not re-arm gameplay.
        button(SDL_CONTROLLER_BUTTON_A, false);
        profiles::set_input_binding(pad, G::X_AXIS_NEG, 0, F::controller_analog(SDL_CONTROLLER_AXIS_LEFTX));
        axis(SDL_CONTROLLER_AXIS_LEFTX, 16384);
        capture = true; drain(); capture = false; drain();
        profiles::reset_input_binding(pad, InputDevice::Controller, G::X_AXIS_NEG);
        drain(); read(); require(x == 0.0f, "cancelled stick bindings bypassed release gate");
        axis(SDL_CONTROLLER_AXIS_LEFTX, 0); drain();
        axis(SDL_CONTROLLER_AXIS_LEFTX, 16384); drain(); read();
        require(x > 0.4f, "stick did not resume after physical release");
        axis(SDL_CONTROLLER_AXIS_LEFTX, 0); drain();

        // Binding uses the same event entry point as gameplay, including
        // cancelling with the controller's own Toggle Menu binding.
        binding::start_scanning(0, G::A, 0, InputDevice::Controller);
        SDL_Event event{};
        event.type = SDL_CONTROLLERBUTTONDOWN; event.cbutton.which = id;
        event.cbutton.button = SDL_CONTROLLER_BUTTON_BACK;
        wdc::input::handle_event(event);
        require(!binding::is_binding() && profiles::get_input_binding(pad, G::A, 0) == F::controller_digital(SDL_CONTROLLER_BUTTON_A),
            "controller binding cancellation used the wrong profile");
        binding::stop_skipping_events();
        binding::start_scanning(0, G::A, 0, InputDevice::Controller);
        event.cbutton.button = SDL_CONTROLLER_BUTTON_X;
        wdc::input::handle_event(event);
        require(!binding::is_binding() && profiles::get_input_binding(pad, G::A, 0) == F::controller_digital(SDL_CONTROLLER_BUTTON_X),
            "native remapping failed");
        button(SDL_CONTROLLER_BUTTON_X, true);
        drain(); read(); require(buttons & 0x8000, "remapped gamepad did not reach guest");
        binding::start_scanning(0, G::B, 1, InputDevice::Keyboard);
        event = {}; event.type = SDL_KEYDOWN;
        event.key.keysym.scancode = SDL_SCANCODE_C; event.key.keysym.sym = SDLK_c;
        wdc::input::handle_event(event);
        require(!binding::is_binding() && profiles::get_input_binding(kb, G::B, 1) == F::keyboard(SDL_SCANCODE_C),
            "native keyboard remapping failed");
        const auto path = dir / "controls.json";
        require(profiles::save_controls_config(path), "controls save failed");
        profiles::reset_profile_bindings(pad, InputDevice::Controller);
        require(profiles::load_controls_config(path) &&
            profiles::get_input_binding(profiles::get_sp_controller_profile_index(), G::A, 0) == F::controller_digital(SDL_CONTROLLER_BUTTON_X) &&
            profiles::get_input_binding(profiles::get_sp_keyboard_profile_index(), G::B, 1) == F::keyboard(SDL_SCANCODE_C),
            "controls did not survive reload");
        auto& general = recompui::config::get_config("general");
        general.update_option_value("joystick_deadzone", 100.0);
        apply_joystick_deadzone(-1.0f, 1.0f, &x, &y);
        require(std::isfinite(x) && std::isfinite(y) && x == 0.0f && y == 0.0f,
            "100 percent native deadzone divided by zero");
        axis(SDL_CONTROLLER_AXIS_LEFTX, -32768);
        axis(SDL_CONTROLLER_AXIS_LEFTY, -32768);
        drain(); read(); require(std::isfinite(x) && std::isfinite(y) && x == 0.0f && y == 0.0f,
            "100 percent deadzone produced invalid axes");

        // Pedals are opt-in and independent of the steering deadzone. Remove
        // trigger N64 bindings to exercise sub-digital-threshold release gating.
        profiles::set_input_binding(profiles::get_sp_controller_profile_index(), G::Z, 0, {});
        profiles::set_input_binding(profiles::get_sp_controller_profile_index(), G::R, 0, {});
        axis(SDL_CONTROLLER_AXIS_LEFTX, 0); axis(SDL_CONTROLLER_AXIS_LEFTY, 0);
        button(SDL_CONTROLLER_BUTTON_X, false);
        auto& driving = recompui::config::get_config("driving-controls");
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0); drain();
        require(wdc::driving::read(0).throttle == 0, "digital default enabled analog throttle");
        driving.update_option_value("throttle_mode", 1u);
        driving.update_option_value("brake_mode", 1u);
        drain(); read();
        require(std::abs(wdc::driving::read(0).throttle - 0.473684f) < 0.001f && buttons == 0,
            "half throttle or menu isolation failed");
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767); drain();
        require(wdc::driving::read(0).brake > 0.999f, "full analog brake failed");
        capture = true;
        require(wdc::driving::read(0).throttle == 0 && wdc::driving::read(0).brake == 0,
            "pending overlay leaked pedals before sampling");
        drain(); capture = false; drain();
        require(wdc::driving::read(0).throttle == 0 && wdc::driving::read(0).brake == 0,
            "held pedals bypassed overlay release gate");
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, -32768); drain();
        require(wdc::driving::read(0).throttle == 0, "releasing brake rearmed held throttle");
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32768); drain();
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, -32000); drain();
        require(wdc::driving::read(0).throttle == 0, "pedal deadzone failed");
        axis(SDL_CONTROLLER_AXIS_TRIGGERRIGHT, 0); drain();
        require(wdc::driving::read(0).throttle > 0.47f, "pedals did not resume after release");
        driving.update_option_value("throttle_saturation", 0.5); drain();
        require(wdc::driving::read(0).throttle > 0.999f, "pedal saturation failed");
        driving.update_option_value("throttle_axis", 2u);
        driving.update_option_value("throttle_negative", true);
        axis(SDL_CONTROLLER_AXIS_LEFTY, -16384); drain();
        require(wdc::driving::read(0).throttle == 1, "negative stick half-axis failed");
        axis(SDL_CONTROLLER_AXIS_LEFTY, 16384); drain();
        require(wdc::driving::read(0).throttle == 0, "opposite stick half activated pedal");
        driving.update_option_value("throttle_deadzone", 0.95);
        driving.update_option_value("throttle_saturation", 0.05);
        axis(SDL_CONTROLLER_AXIS_LEFTY, -32768); drain();
        require(wdc::driving::read(0).throttle == 1, "reversed pedal range divided by zero");
        driving.update_option_value("throttle_axis", 0u); drain();
        require(wdc::driving::read(0).throttle == 0, "None axis activated pedal");
        driving.update_option_value("throttle_axis", 6u);
        driving.update_option_value("throttle_negative", false);
        driving.update_option_value("throttle_deadzone", 0.05);
        driving.update_option_value("throttle_saturation", 1.0);
        axis(SDL_CONTROLLER_AXIS_LEFTY, 0);
        axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 32767); drain();
        require(wdc::driving::read(0).throttle > 0 && wdc::driving::read(0).brake > 0,
            "simultaneous pedals failed");
        require(wdc::driving::read(1).throttle == 0 && wdc::driving::read(-1).brake == 0,
            "unconnected port received pedals");
        wdc::rumble::set_motor(0, true); drain();
        require(rumble_high != 0, "rumble did not resume before detach");
        SDL_JoystickClose(joystick);
        require(SDL_JoystickDetachVirtual(device) == 0, "virtual detach failed");
        drain(); read();
        require(get_controller_from_joystick_id(id) == nullptr && buttons == 0 && x == 0 && y == 0,
            "unplug left stale gamepad input");
        require(wdc::driving::read(0).throttle == 0 && wdc::driving::read(0).brake == 0,
            "unplug left stale pedal input");
        require(!wdc::rumble::active(), "unplug left stale rumble request");
        wdc::rumble::set_motor(0, true);
        descriptor.Rumble = failed_rumble_callback;
        const int unsupported = SDL_JoystickAttachVirtualEx(&descriptor);
        require(unsupported >= 0, "second virtual gamepad unavailable");
        drain();
        require(!wdc::rumble::active(), "new controller inherited pending rumble");
        wdc::rumble::set_motor(0, true); drain();
        const int failures_before = failed_rumble_calls;
        drain();
        require(failed_rumble_calls == failures_before,
            "unsupported controller was retried after its initial failure");
        require(failed_rumble_calls == 1, "unsupported controller was never probed");
        require(SDL_JoystickDetachVirtual(unsupported) == 0, "second virtual detach failed");
        drain();
        SDL_Quit();
        std::filesystem::remove_all(dir);
        std::cout << "native gamepad input tests passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << " (SDL: " << SDL_GetError() << ")\n";
        SDL_Quit();
        std::filesystem::remove_all(dir);
        return 1;
    }
}
