#pragma once
// remake/core/fight/fight_math.gd
// Small integer routines of the fight code with the game's rounding and wrap-around, written
// from the verified ports in tools/research/fight_math.py (bit-exact in the CPU harness):
// Atan2Angle, FighterRelativeAngles' quadrant and fold, SinQ15/CosQ15, the table square root,
// TransitionRemap, MoveKeepsFrame, SegmentHitsCylinder, HitDamage and LaunchTrajectory.
#include "gd/gd.h"

namespace t3 {

class FightTables;
class MoveRow;

class FightMath {
public:
    static inline const gd::Arr<gd::Int> ZONE_PERCENT{90, 90, 90, 90, 100, 100, 120, 120, 130, 100, 100, 140, 125, 125};
    static constexpr gd::Int TRIG_LIMIT = 0xFFF;
    static constexpr gd::Int QUARTER_TURN = 0x4000;       // fight angles are 16-bit: 0x10000 per turn
    static constexpr gd::Int HALF_TURN = 0x8000;

    // FUN_8004B5E0: arctangent of x/4096 in 1/1024 of a quarter turn (s16).
    static gd::Int atan_table(gd::Int x, const gd::Ref<FightTables>& t);

    // Atan2Angle (0x8002A478): the 16-bit angle of (dz, dx); only the low 16 bits of each count.
    static gd::Int atan2_angle(gd::Int dz, gd::Int dx, const gd::Ref<FightTables>& t);

    // The quadrant of a heading difference: 0 front, 1 and 3 the sides, 2 back.
    static gd::Int quadrant(gd::Int delta);

    // |delta| of a 16-bit angle difference as the game folds it (~delta for negative values).
    static gd::Int fold(gd::Int delta);

    // SinQ15 (0x8002A3E8) / CosQ15: sin * 0x7FF8 from the 4096-entry table.
    static gd::Int sin_q15(gd::Int angle, const gd::Ref<FightTables>& t);

    static gd::Int cos_q15(gd::Int angle, const gd::Ref<FightTables>& t);

    static gd::Int _trig(gd::Int v);

    // Raw 4.12 g_sinTable / g_cosTable entries (index masked to 12 bits).
    static gd::Int sin12(gd::Int index, const gd::Ref<FightTables>& t);

    static gd::Int cos12(gd::Int index, const gd::Ref<FightTables>& t);

    // FUN_8004B174 (PsyQ SquareRoot0): leading-zero count plus a 192-entry table.
    static gd::Int sqrt_table(gd::Int n, const gd::Ref<FightTables>& t);

    // BranchEntersBeforeActiveEnd (0x8003127C) for the pending move.
    static bool enters_before_active_end(const gd::Ref<MoveRow>& move, gd::Int entry_frame);

    // TransitionRemap (0x800311BC).
    static gd::Int transition_remap(gd::Int code, const gd::Ref<MoveRow>& move, gd::Int entry_frame);

    // MoveKeepsFrame (0x800312C4) for the pending move.
    static bool move_keeps_frame(const gd::Ref<MoveRow>& move, gd::Int branch_kind, gd::Int pose_frame);

    // SegmentHitsCylinder (0x800479A4): segment (x0, y0, z0, x1, y1, z1) against a vertical
    // cylinder (cx, cy, cz, radius, radius^2): bounding boxes, the clip to the slab cy +- r, then
    // the horizontal distance. The perpendicular-foot products wrap to 32 bits unless `exact`
    // (game bug #9, a Gameplay fix: callers pass `rules.fix_segment_hit_overflow`).
    static bool segment_hits_cylinder(const gd::PI32& seg, const gd::PI32& cyl, bool exact);
};

}  // namespace t3
