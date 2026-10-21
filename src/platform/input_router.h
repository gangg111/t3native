#pragma once
// remake/platform/input_router.gd: turns keyboards and gamepads into one pad word per player
// (autoload `Pads`).
//
// Devices are assigned to players on their first button press and released when unplugged. Each
// physics frame produces the physical buttons (`held`, like the game's 0x800AE230) and the newly
// pressed ones; the game applies its key configuration itself (GameFlow.key_tables).
//
// Godot's Input singleton is replaced by the state the platform feeds in (`key_event`,
// `joy_button`, `joy_axis`, `joy_connection`); keys are Godot key codes (platform/keys.h).
#include <functional>
#include <map>
#include <set>
#include <string>

#include "gd/gd.h"
#include "platform/app.h"

namespace t3 {

class InputRouter {
public:
    static constexpr gd::Int PLAYERS = 2;
    static constexpr gd::Int KEYBOARD = -1;     // device id of the keyboard's left layout (WASD, player 1)
    static constexpr gd::Int KEYBOARD_2 = -3;   // device id of the keyboard's right layout (arrows, player 2)
    static constexpr gd::Int TOUCH = -4;        // device id of the on-screen touch controls (player 1)
    static constexpr gd::Int NO_DEVICE = -2;
    static constexpr float STICK_DEADZONE = 0.5f;
    static constexpr float TRIGGER_THRESHOLD = 0.5f;
    static constexpr float AXIS_FULL = 0.95f;   // an axis pushed this far joins its pad without having been seen at rest
    enum Axis : gd::Int { AXIS_LEFT_X = 0, AXIS_LEFT_Y = 1, AXIS_TRIGGER_LEFT = 4, AXIS_TRIGGER_RIGHT = 5 };
    static constexpr gd::Int BACK_FRAMES = 6;

    // A pad button as the platform reports it -> the pad bit (JOY_BUTTONS).
    static gd::Int joy_bit(PadButton b);
    // The keyboard layouts' defaults: the left player W A S D + R T / F G, the right player the
    // arrows + U I / J K (DEFAULT_LAYOUTS).
    static std::map<gd::Int, gd::Int> default_layout(int index);
    static const std::map<std::string, gd::Int>& BINDABLE();
    static bool reserved(gd::Int key);   // RESERVED_KEYS: Escape, Tab, F3, F11

    gd::PI32 devices{NO_DEVICE, NO_DEVICE};
    std::map<gd::Int, gd::Int> layouts[2] = {default_layout(0), default_layout(1)};   // key -> pad bit
    bool keyboard_seen = false;
    gd::PI32 held{0, 0};
    gd::PI32 pressed{0, 0};
    gd::Int escape_buttons = 0x0800;      // PadState.START
    gd::Int escape_player = -1;
    // Returns (escape_buttons, escape_player) for the screen showing now (the game sets it).
    std::function<gd::Vector2i()> escape_rules;
    bool escape_pressed = false;
    bool back_allowed = true;
    gd::Int touch_bits = 0;
    gd::Int touch_latch = 0;

    // Platform input.
    void key_event(gd::Int key, bool down);
    void joy_button(gd::Int device, PadButton button, bool down);
    void joy_axis(gd::Int device, gd::Int axis, float value);
    void joy_connection(gd::Int device, bool connected);

    // Once per physics frame, before the game reads `held` / `pressed`.
    void _physics_process();
    void back();
    gd::Int assign(gd::Int device);
    gd::Int assign_touch();
    gd::Int read_device(gd::Int device) const;
    static gd::Int axis_bits(gd::Int axis, float value);
    bool has_keyboard() const { return keyboard_seen; }
    gd::Int escape_owner() const;
    gd::Int escape_latch(bool down);
    std::string device_name(gd::Int player) const;

private:
    void _axis_moved(gd::Int device, gd::Int axis, float value);

    std::set<gd::Int> keys_down_;
    std::map<gd::Int, std::set<PadButton>> buttons_down_;
    std::map<std::pair<gd::Int, gd::Int>, float> axes_;
    std::set<std::pair<gd::Int, gd::Int>> _axes_at_rest;
    gd::Int _escape_to = 0;
    gd::Int _escape_held = 0;
    bool _escape_down = false;
    gd::Int _back_frames = 0;
};

}  // namespace t3
