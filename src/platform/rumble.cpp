// remake/platform/rumble.gd
#include "platform/rumble.h"

#include <algorithm>
#include <cmath>

#include "platform/app.h"
#include "platform/input_router.h"

namespace t3 {

namespace {

// Input.vibrate_handheld(duration_ms, amplitude 0-1).
void vibrate_handheld(gd::Int duration_ms, double amplitude) {
    int strength = static_cast<int>(std::lround(std::clamp(amplitude, 0.0, 1.0) * Rumble::LARGE_MAX));
    Platform::vibrate(static_cast<int>(duration_ms), std::clamp(strength, 1, 255));
}

// Input.start_joy_vibration(device, weak, strong, duration): one motor at the stronger level.
void start_joy_vibration(gd::Int, double weak, double strong, double duration) {
    vibrate_handheld(static_cast<gd::Int>(std::lround(duration * 1000.0)), std::max(weak, strong));
}

}  // namespace

void Rumble::set_motors(gd::Int device, bool small_on, gd::Int large) {
    if (device < 0 && device != InputRouter::TOUCH) {
        return;
    }
    _levels.set(device, Level{small_on ? 1.0 : 0.0, std::clamp(static_cast<double>(large) / LARGE_MAX, 0.0, 1.0)});
}

void Rumble::update() {
    double handheld = 0.0;
    for (gd::Int device : _levels.keys()) {
        Level level = _levels.at(device);
        if (device == InputRouter::TOUCH) {
            handheld = std::max(level.x * SMALL_SHARE, level.y);
        } else if (level == Level{}) {
            // Input.stop_joy_vibration(device): nothing to send natively.
        } else {
            start_joy_vibration(device, level.x, level.y, FRAME_SECONDS * HOLD_FRAMES);
        }
    }
    _levels.clear();
    _update_handheld(handheld);
}

void Rumble::_update_handheld(double level) {
    level = std::ceil(level * HANDHELD_STEPS) / HANDHELD_STEPS;
    _handheld_age += 1;
    if (level <= 0.0) {
        if (handheld_level > 0.0) {
            vibrate_handheld(HANDHELD_CUT_MS, 1.0 / LARGE_MAX);
        }
        handheld_level = 0.0;
        return;
    }
    if (level != handheld_level || _handheld_age >= HANDHELD_RENEW) {
        vibrate_handheld(std::lround(HANDHELD_PULSE * FRAME_SECONDS * 1000.0), level);
        handheld_level = level;
        _handheld_age = 0;
    }
}

}  // namespace t3
