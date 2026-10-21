#pragma once
// remake/core/fighter/move_events.gd
// MoveEvents (0x80045B60, moves.md#frame-events): fires the running move's frame events whose
// frame the pose passed this frame (`eventFrame < frame ≤ poseFrame`).
//
// Throw damage and damage exchanges (11–13) only record `extra_kind`; hand commands (14–61)
// go to the fighter's hands; camera shakes (1–3), flipbooks, sparks, dust and vibration become
// SimEvents (the fight runs the SHAKE_REQUEST and FIGHTER_VIBRATE calls in place after both
// fighters' events).
#include "gd/gd.h"

namespace t3 {

class FighterState;
class FighterTables;
class SimEvents;

class MoveEvents {
public:
    static constexpr gd::Int SHAKE_LAST = 3;
    static constexpr gd::Int GROUND_IMPACT = 4;
    static constexpr gd::Int EFFECT_OWN = 6;
    static constexpr gd::Int EFFECT_OPPONENT = 7;
    static constexpr gd::Int SPARK_FIRST = 8;
    static inline const gd::Arr<gd::Int> SPARK_INDEX{0, 1, 3};   // events 8–10 → system flipbook index
    static constexpr gd::Int EXTRA_FIRST = 11;
    static constexpr gd::Int EXTRA_LAST = 13;
    static constexpr gd::Int HAND_FIRST = 14;
    static constexpr gd::Int HAND_LAST = 61;
    static constexpr gd::Int HAND_RESET = 20;                    // both hands open
    static constexpr gd::Int HAND_COSTUME = 21;                  // both hands to the costume's own shape
    static constexpr gd::Int VIBRATE_EFFECT = 1;                 // FighterVibrate kinds
    static constexpr gd::Int VIBRATE_IMPACT = 2;

    static void run(const gd::Ref<FighterState>& f, const gd::Ref<FighterTables>& tables,
                    const gd::Ref<SimEvents>& out);

    // The world position of the joint block an event names (the scripted scenes have no body:
    // their presentation resolves the joint itself).
    static gd::PI32 _joint_point(const gd::Ref<FighterState>& f, gd::Int joint);
};

}  // namespace t3
