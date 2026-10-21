#pragma once
// remake/core/fight/vibration.gd
// Pad vibration requests (PadVibrate patterns, SimEvents VIBRATE a = pad (2 both), b = pattern):
// HitVibration (0x80075CBC) for the frame's hits, FighterVibrate (0x800760D4) for the move and
// effect events and VibrateAll (0x80076078) for camera shakes. Nothing vibrates during a replay.
#include "gd/gd.h"

namespace t3 {

class FightState;
class FighterState;
class SimEvents;

class Vibration {
public:
    static constexpr gd::Int BOTH_PADS = 2;

    // HitVibration patterns: on the fighter's own pad, then the pads flagged per player.
    static constexpr gd::Int GUARDED = 0x10;
    static constexpr gd::Int HIT_LIGHT = 0x11;
    static constexpr gd::Int HIT_HEAVY = 0x12;           // damage from 21 on
    static constexpr gd::Int HEAVY_DAMAGE = 0x15;
    static constexpr gd::Int HIT_CLOSE = 0x14;
    static constexpr gd::Int HIT_COUNTER = 0x15;
    static constexpr gd::Int EXTRA_DAMAGE = 0x13;
    static constexpr gd::Int LANDED = 0x18;
    static constexpr gd::Int KNOCKED_OUT = 0x16;
    static constexpr gd::Int POWER_MOVE = 0xE;
    // Untyped (bit, pattern) pairs, indexed: gd::Arr<gd::Arr<gd::Int>>.
    static inline const gd::Arr<gd::Arr<gd::Int>> FLAGGED{
        {0x40, 0x1A}, {1, 4}, {2, 5}, {4, 6}, {8, 8}, {0x10, 9}, {0x20, 7}, {0x80, 0xB}};

    // FighterVibrate kinds: (own pad pattern or −1, other pad pattern or −1); kinds 2-5 vibrate
    // the fighter's own pad only. (Untyped pairs, indexed: gd::Arr<gd::Arr<gd::Int>>.)
    static inline const gd::Arr<gd::Arr<gd::Int>> FIGHTER_KINDS{
        {0x17, 10}, {0xF, 0x1B}, {0x19, -1}, {0xC, -1}, {0xD, -1}, {5, -1}};

    static bool _allowed(const gd::Ref<FightState>& fight);

    // FUN_800761D8: the pad of a fighter (Tekken Force: the human's pad for the first record, the
    // other pad for the enemies).
    static gd::Int _pad(const gd::Ref<FightState>& fight, const gd::Ref<FighterState>& f);

    // HitVibration: the outcome of every fighter's last applied hit on its pad, then the attacker's
    // and thrower's pads.
    static void hits(const gd::Ref<FightState>& fight, const gd::Ref<SimEvents>& events);

    // FighterVibrate: kind 0 burning, 1 move effect, 2 ground impact, 3 fire breath, 4 Gon's flame,
    // 5 unused here.
    static void fighter(const gd::Ref<FightState>& fight, gd::Int index, gd::Int kind, const gd::Ref<SimEvents>& events);

    // VibrateAll: a camera shake's pulse on both pads.
    static void all(const gd::Ref<FightState>& fight, gd::Int script, const gd::Ref<SimEvents>& events);
};

}  // namespace t3
