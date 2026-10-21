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

double eighths(double v) { return std::ceil(v * Rumble::HANDHELD_STEPS) / Rumble::HANDHELD_STEPS; }

int strength(double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * Rumble::LARGE_MAX)); }

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
        } else {
            _update_pad(device, level);
        }
    }
    // A pad the game no longer asks about (its player gone): stopped.
    for (gd::Int device : _pads.keys()) {
        if (!_levels.has(device)) _update_pad(device, Level{});
    }
    _levels.clear();
    _update_handheld(handheld);
}

void Rumble::_update_pad(gd::Int device, Level level) {
    level = Level{eighths(level.x), eighths(level.y)};
    if (level == Level{}) {
        if (_pads.has(device)) {
            Platform::pad_rumble(static_cast<int>(device), 0, 0, 0);
            _pads.erase(device);
        }
        return;
    }
    Sent sent = _pads.has(device) ? _pads.at(device) : Sent{};
    sent.age += 1;
    if (!(level == sent.level) || sent.age >= PAD_RENEW) {
        Platform::pad_rumble(static_cast<int>(device), strength(level.x), strength(level.y), static_cast<int>(PAD_PULSE_MS));
        sent = Sent{level, 0};
    }
    _pads.set(device, sent);
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
