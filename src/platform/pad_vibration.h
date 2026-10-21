#pragma once
// remake/platform/pad_vibration.gd
// The game's vibration scripts (VibrationUpdate 0x80029228, sound.md#vibration): PadVibrate
// queues a pattern into one of four slots per pad; every frame each slot's small-motor script
// gives on/off (a pulse mask by level, stepped by the frame counter) and its large-motor script
// a strength; the highest running slot drives each motor.
#include <functional>

#include "content/fight_tables.h"
#include "gd/gd.h"

namespace t3 {

class PadVibration {
public:
    static constexpr gd::Int PADS = 2;
    static constexpr gd::Int SLOTS = 4;
    static constexpr gd::Int CONTINUE = 0x1000;

    class MotorScript {
    public:
        gd::PI32 steps;
        gd::Int index = -1;              // −1: not running
        gd::Int count = 0;

        void start(const gd::PI32& words);
        // Advances one frame; returns the running step's word, or −1.
        gd::Int step();
    };

    // The autoload Pads' set_motors(player, small_on, large) (InputRouter has no rumble yet: the
    // app sets it to its Rumble with the player's device). Empty: the levels go nowhere.
    static inline std::function<void(gd::Int, bool, gd::Int)> Pads_set_motors;

    gd::Ref<FightTables::VibrationTables> tables;
    gd::Arr<gd::PI32> pending;                         // per pad: the pattern queued per slot
    gd::Arr<gd::Arr<gd::Ref<MotorScript>>> small;      // per pad: MotorScript per slot
    gd::Arr<gd::Arr<gd::Ref<MotorScript>>> large;
    gd::Arr<uint8_t> small_on{false, false};           // the motors of the last update
    gd::PI32 large_level{0, 0};
    gd::Int _frame = 0;                                // 0x800982E0 (vblanks)

    explicit PadVibration(const gd::Ref<FightTables::VibrationTables>& vibration);

    // PadVibrate(pad, pattern): pad 2 is both.
    void vibrate(gd::Int pad, gd::Int pattern);
    // Stops every script (a new fight, the pause menu leaving).
    void clear();
    // One frame of the scripts driving both players' motors (Pads); `enabled` false holds them still
    // (the Vibration setting off).
    void drive(bool enabled);
    // Both players' motors stopped (a fight that ends, the application quitting).
    static void stop_motors();
    // One frame (VibrationUpdate for both pads).
    void update();
};

}  // namespace t3
