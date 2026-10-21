#include "platform/input_router.h"

#include "platform/keys.h"
#include "platform/pad_state.h"

namespace t3 {

gd::Int InputRouter::joy_bit(PadButton b) {
    switch (b) {
        case PadButton::A: return PadState::CROSS;
        case PadButton::B: return PadState::CIRCLE;
        case PadButton::X: return PadState::SQUARE;
        case PadButton::Y: return PadState::TRIANGLE;
        case PadButton::L1: return PadState::L1;
        case PadButton::R1: return PadState::R1;
        case PadButton::SELECT: return PadState::SELECT;
        case PadButton::START: return PadState::START;
        case PadButton::UP: return PadState::UP;
        case PadButton::RIGHT: return PadState::RIGHT;
        case PadButton::DOWN: return PadState::DOWN;
        case PadButton::LEFT: return PadState::LEFT;
        default: return 0;   // L2 / R2 come from the trigger axes (AXES), L3 / R3 press nothing
    }
}

std::map<gd::Int, gd::Int> InputRouter::default_layout(int index) {
    using namespace Keys;
    if (index == 0)
        return {{letter('W'), PadState::UP}, {letter('D'), PadState::RIGHT}, {letter('S'), PadState::DOWN},
                {letter('A'), PadState::LEFT}, {letter('R'), PadState::SQUARE}, {letter('T'), PadState::TRIANGLE},
                {letter('F'), PadState::CROSS}, {letter('G'), PadState::CIRCLE}, {letter('Z'), PadState::L1},
                {letter('X'), PadState::R1}, {letter('C'), PadState::L2}, {letter('V'), PadState::R2},
                {SPACE, PadState::START}, {letter('Q'), PadState::SELECT}};
    return {{UP, PadState::UP}, {RIGHT, PadState::RIGHT}, {DOWN, PadState::DOWN}, {LEFT, PadState::LEFT},
            {letter('U'), PadState::SQUARE}, {letter('I'), PadState::TRIANGLE}, {letter('J'), PadState::CROSS},
            {letter('K'), PadState::CIRCLE}, {letter('O'), PadState::L1}, {letter('P'), PadState::R1},
            {letter('L'), PadState::L2}, {SEMICOLON, PadState::R2}, {ENTER, PadState::START}, {BACKSPACE, PadState::SELECT}};
}

const std::map<std::string, gd::Int>& InputRouter::BINDABLE() {
    static const std::map<std::string, gd::Int> m = {
        {"up", PadState::UP}, {"right", PadState::RIGHT}, {"down", PadState::DOWN}, {"left", PadState::LEFT},
        {"square", PadState::SQUARE}, {"triangle", PadState::TRIANGLE}, {"cross", PadState::CROSS}, {"circle", PadState::CIRCLE},
        {"l1", PadState::L1}, {"r1", PadState::R1}, {"l2", PadState::L2}, {"r2", PadState::R2},
        {"start", PadState::START}, {"select", PadState::SELECT}};
    return m;
}

bool InputRouter::reserved(gd::Int key) { return key == Keys::ESCAPE || key == Keys::TAB || key == Keys::F3 || key == Keys::F11; }

// _unhandled_input: a key or pad press assigns its device; the state is kept for read_device.
void InputRouter::key_event(gd::Int key, bool down) {
    if (key == 0) return;
    if (!down) {
        keys_down_.erase(key);
        return;
    }
    bool repeat = keys_down_.count(key) != 0;
    keys_down_.insert(key);
    if (repeat) return;   // is_echo()
    keyboard_seen = true;
    gd::Int device = NO_DEVICE;
    for (int layout = 0; layout < 2; ++layout) {
        if (layouts[layout].count(key)) {
            device = layout == 0 ? KEYBOARD : KEYBOARD_2;
            break;
        }
    }
    if (device != NO_DEVICE && !devices.has(device)) assign(device);
}

void InputRouter::joy_button(gd::Int device, PadButton button, bool down) {
    if (!down) {
        buttons_down_[device].erase(button);
        return;
    }
    bool repeat = buttons_down_[device].count(button) != 0;
    buttons_down_[device].insert(button);
    if (!repeat && !devices.has(device)) assign(device);
}

void InputRouter::joy_axis(gd::Int device, gd::Int axis, float value) {
    auto key = std::make_pair(device, axis);
    auto it = axes_.find(key);
    if (it != axes_.end() && it->second == value) return;   // Godot reports an axis only as it changes
    axes_[key] = value;
    _axis_moved(device, axis, value);
}

void InputRouter::joy_connection(gd::Int device, bool connected) {
    // A pad that comes with this id later is another pad: its axes are not known to rest.
    for (gd::Int axis : {AXIS_TRIGGER_LEFT, AXIS_TRIGGER_RIGHT, AXIS_LEFT_X, AXIS_LEFT_Y}) {
        _axes_at_rest.erase(std::make_pair(device, axis));
        axes_.erase(std::make_pair(device, axis));
    }
    buttons_down_.erase(device);
    if (connected) return;
    for (gd::Int player = 0; player < PLAYERS; ++player) {
        if (devices[player] == device) {
            devices[player] = NO_DEVICE;
            held[player] = 0;
            pressed[player] = 0;
        }
    }
}

void InputRouter::_physics_process() {
    if (escape_rules) {
        gd::Vector2i rules = escape_rules();
        escape_buttons = rules.x;
        escape_player = rules.y;
    }
    gd::Int escape = escape_latch(keys_down_.count(Keys::ESCAPE) != 0 || _back_frames > 0);
    _back_frames = gd::maxi(0, _back_frames - 1);
    for (gd::Int player = 0; player < PLAYERS; ++player) {
        gd::Int now = read_device(devices[player]);
        if (player == _escape_to) now |= escape;
        pressed[player] = now & ~gd::Int(held[player]);
        held[player] = now;
    }
    touch_latch = 0;
    // TODO: rumble.update() arrives with the vibration slice (platform/rumble.gd).
}

void InputRouter::back() {
    if (back_allowed) _back_frames = BACK_FRAMES;
}

gd::Int InputRouter::assign(gd::Int device) {
    gd::Int preferred = device == KEYBOARD ? 0 : (device == KEYBOARD_2 ? 1 : -1);
    if (preferred >= 0 && devices[preferred] == NO_DEVICE) {
        devices[preferred] = device;
        return preferred;
    }
    for (gd::Int player = 0; player < PLAYERS; ++player) {
        if (devices[player] == NO_DEVICE) {
            devices[player] = device;
            return player;
        }
    }
    return -1;
}

gd::Int InputRouter::assign_touch() {
    gd::Int other = devices[0];
    if (other == TOUCH) return 0;
    if (other != NO_DEVICE) {
        devices[0] = NO_DEVICE;
        if (devices[1] == NO_DEVICE) devices[1] = other;
    }
    devices[0] = TOUCH;
    return 0;
}

gd::Int InputRouter::read_device(gd::Int device) const {
    if (device == NO_DEVICE) return 0;
    if (device == TOUCH) return touch_bits | touch_latch;
    gd::Int bits = 0;
    if (device == KEYBOARD || device == KEYBOARD_2) {
        const auto& layout = layouts[device == KEYBOARD ? 0 : 1];
        // Alt + Enter is the host's fullscreen hotkey, not the layout's button (Windows).
        bool alt = keys_down_.count(Keys::ALT) != 0;
        for (const auto& [key, bit] : layout)
            if (keys_down_.count(key) && !(alt && (key == Keys::ENTER || key == Keys::KP_ENTER))) bits |= bit;
        return bits;
    }
    auto it = buttons_down_.find(device);
    if (it != buttons_down_.end())
        for (PadButton b : it->second) bits |= joy_bit(b);
    for (gd::Int axis : {AXIS_TRIGGER_LEFT, AXIS_TRIGGER_RIGHT, AXIS_LEFT_X, AXIS_LEFT_Y}) {
        auto a = axes_.find(std::make_pair(device, axis));
        if (a != axes_.end()) bits |= axis_bits(axis, a->second);
    }
    return bits;
}

gd::Int InputRouter::axis_bits(gd::Int axis, float value) {
    switch (axis) {
        case AXIS_TRIGGER_LEFT: return value > TRIGGER_THRESHOLD ? PadState::L2 : 0;
        case AXIS_TRIGGER_RIGHT: return value > TRIGGER_THRESHOLD ? PadState::R2 : 0;
        case AXIS_LEFT_X: return value > STICK_DEADZONE ? PadState::RIGHT : (value < -STICK_DEADZONE ? PadState::LEFT : 0);
        case AXIS_LEFT_Y: return value > STICK_DEADZONE ? PadState::DOWN : (value < -STICK_DEADZONE ? PadState::UP : 0);
        default: return 0;
    }
}

// A stick or trigger that presses a button joins its pad once that axis has been seen at rest, or
// at once when it is pushed all the way: a drifting stick never joins by itself.
void InputRouter::_axis_moved(gd::Int device, gd::Int axis, float value) {
    auto key = std::make_pair(device, axis);
    if (axis_bits(axis, value) == 0) {
        _axes_at_rest.insert(key);
    } else if ((_axes_at_rest.count(key) || std::abs(value) >= AXIS_FULL) && !devices.has(device)) {
        assign(device);
    }
}

gd::Int InputRouter::escape_owner() const {
    for (gd::Int device : {KEYBOARD, KEYBOARD_2}) {
        gd::Int player = devices.find(device);
        if (player >= 0) return player;
    }
    return 0;
}

gd::Int InputRouter::escape_latch(bool down) {
    escape_pressed = down && !_escape_down;
    _escape_down = down;
    if (!down) {
        _escape_held = 0;
    } else if (escape_pressed) {
        _escape_held = escape_buttons;
        _escape_to = escape_player >= 0 ? escape_player : escape_owner();
    }
    return _escape_held;
}

std::string InputRouter::device_name(gd::Int player) const {
    gd::Int device = devices[player];
    if (device == NO_DEVICE) return "-";
    if (device == KEYBOARD) return "Keyboard 1";
    if (device == KEYBOARD_2) return "Keyboard 2";
    if (device == TOUCH) return "Touch";
    return "Gamepad " + gd::str(device);
}

}  // namespace t3
