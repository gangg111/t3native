#pragma once
// remake/platform/rumble.gd
// Drives controller motors the way the game drives a DualShock: every frame the
// vibration scripts produce a small-motor on/off state and a large-motor strength
// (docs/research/code/sound.md#vibration). The levels are sent each frame
// with a short duration, so a motor stops when its script stops asking for it. PadVibration
// runs the scripts and InputRouter passes their levels to the player's device.
//
// The touch controls' player feels the phone's (or tablet's) own vibration motor: one motor, so
// it runs at the stronger of the two levels (the small motor's buzz at SMALL_SHARE). A phone
// motor is driven by one-shot pulses (Input.vibrate_handheld), each replacing the one running:
// a pulse of HANDHELD_PULSE frames is sent when the level changes and renewed every
// HANDHELD_RENEW frames while it holds; a 1 ms pulse cuts it short when the level falls to zero.
//
// Native: Input.vibrate_handheld(ms, amplitude) is Platform::vibrate(ms, amplitude 0-1 as
// strength 1-255). Input.start_joy_vibration has no platform call yet: a pad's two motors go to the
// same Platform::vibrate at the stronger level (TODO: per-device pad rumble); stop_joy_vibration
// sends nothing (the last pulse is HOLD_FRAMES long and ends by itself).
#include "gd/gd.h"

namespace t3 {

class Rumble {
public:
    static constexpr double FRAME_SECONDS = 1.0 / 60.0;
    static constexpr gd::Int HOLD_FRAMES = 2;          // covers one late frame without a gap
    static constexpr double LARGE_MAX = 255.0;

    static constexpr double SMALL_SHARE = 0.5;         // the small motor's buzz on a phone's single motor
    static constexpr double HANDHELD_STEPS = 8.0;      // a phone's level is rounded to eighths (a new pulse on a change)
    static constexpr gd::Int HANDHELD_PULSE = 6;       // frames
    static constexpr gd::Int HANDHELD_RENEW = 4;       // frames
    static constexpr gd::Int HANDHELD_CUT_MS = 1;

    // Vector2(weak, strong).
    struct Level {
        double x = 0.0, y = 0.0;
        bool operator==(const Level& o) const { return x == o.x && y == o.y; }
    };

    gd::Dict<gd::Int, Level> _levels;   // device → Vector2(weak, strong)
    double handheld_level = 0.0;        // the phone motor's level running now (0: still)
    gd::Int _handheld_age = 0;          // frames since its last pulse

    void set_motors(gd::Int device, bool small_on, gd::Int large);
    void update();
    void _update_handheld(double level);
};

}  // namespace t3
