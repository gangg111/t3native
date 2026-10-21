// remake/core/fight/fight_math.gd
// Small integer routines of the fight code with the game's rounding and wrap-around, written
// from the verified ports in tools/research/fight_math.py (bit-exact in the CPU harness):
// Atan2Angle, FighterRelativeAngles' quadrant and fold, SinQ15/CosQ15, the table square root,
// TransitionRemap, MoveKeepsFrame, SegmentHitsCylinder, HitDamage and LaunchTrajectory.
#include "core/fight/fight_math.h"

#include "content/fight_tables.h"
#include "content/move_row.h"
#include "core/fighter/transition.h"
#include "core/math/fx.h"

namespace t3 {

// FUN_8004B5E0: arctangent of x/4096 in 1/1024 of a quarter turn (s16).
gd::Int FightMath::atan_table(gd::Int x, const gd::Ref<FightTables>& t) {
    gd::Int idx = gd::absi(Fx::div_trunc(x, 4));
    gd::Int v = t->atan[idx] + (idx >> 1);
    return Fx::s16(x < 0 ? -v : v);
}

// Atan2Angle (0x8002A478): the 16-bit angle of (dz, dx); only the low 16 bits of each count.
gd::Int FightMath::atan2_angle(gd::Int dz, gd::Int dx, const gd::Ref<FightTables>& t) {
    gd::Int z = Fx::s16(dz);
    gd::Int x = Fx::s16(dx);
    gd::Int a = gd::absi(z) & 0xFFFF;
    gd::Int b = gd::absi(x) & 0xFFFF;
    gd::Int s = 0;
    if (a == b) {
        s = 0x200;
    } else if (b < a) {
        s = atan_table(gd::div(b << 12, a), t);
    } else {
        s = Fx::s16(0x400 - atan_table(gd::div(a << 12, b), t));
    }
    gd::Int u = (s * 16) & 0xFFFF;
    gd::Int r = 0;
    if (z < 0) {
        r = x < 0 ? u + 0x8000 : -s * 16 + 0x7FFF;
    } else {
        r = x < 0 ? ~u : u;
    }
    return r & 0xFFFF;
}

// The quadrant of a heading difference: 0 front, 1 and 3 the sides, 2 back.
gd::Int FightMath::quadrant(gd::Int delta) {
    gd::Int q = (delta + 0x2000) & 0xFFFF;
    if (q < 0x4000) {
        return 0;
    }
    if (q < 0x8000) {
        return 1;
    }
    return q < 0xC000 ? 2 : 3;
}

// |delta| of a 16-bit angle difference as the game folds it (~delta for negative values).
gd::Int FightMath::fold(gd::Int delta) {
    delta &= 0xFFFF;
    return (delta & 0x8000) != 0 ? (~delta & 0xFFFF) : delta;
}

// SinQ15 (0x8002A3E8) / CosQ15: sin * 0x7FF8 from the 4096-entry table.
gd::Int FightMath::sin_q15(gd::Int angle, const gd::Ref<FightTables>& t) {
    return _trig(t->sin_table[((angle & 0xFFFFFFFFLL) >> 4) & 0xFFF]);
}

gd::Int FightMath::cos_q15(gd::Int angle, const gd::Ref<FightTables>& t) {
    return _trig(t->sin_table[(((angle & 0xFFFFFFFFLL) >> 4) & 0xFFF) + 1024]);
}

gd::Int FightMath::_trig(gd::Int v) {
    v = gd::clampi(v, -TRIG_LIMIT, TRIG_LIMIT);
    return Fx::s16((v << 19) >> 16);
}

// Raw 4.12 g_sinTable / g_cosTable entries (index masked to 12 bits).
gd::Int FightMath::sin12(gd::Int index, const gd::Ref<FightTables>& t) {
    return t->sin_table[index & 0xFFF];
}

gd::Int FightMath::cos12(gd::Int index, const gd::Ref<FightTables>& t) {
    return t->sin_table[(index & 0xFFF) + 1024];
}

// FUN_8004B174 (PsyQ SquareRoot0): leading-zero count plus a 192-entry table.
gd::Int FightMath::sqrt_table(gd::Int n, const gd::Ref<FightTables>& t) {
    n = Fx::w32(n);
    if (n == 0) {
        return 0;
    }
    gd::Int magnitude = n > 0 ? n : ~n & 0xFFFFFFFFLL;
    gd::Int bits = 0;
    while (gd::shr(magnitude, bits) != 0) {
        bits += 1;
    }
    gd::Int lz = (32 - bits) & 0xFFFE;
    gd::Int shift = 0x18 - lz;
    gd::Int v = shift >= 0 ? gd::shr(n, shift) : Fx::w32(gd::shl(n, -shift));
    gd::Int e = t->square_root[v - 0x40];
    return (gd::shl(e, ((0x1F - lz) >> 1) & 0x1F) & 0xFFFFFFFFLL) >> 12;
}

// BranchEntersBeforeActiveEnd (0x8003127C) for the pending move.
bool FightMath::enters_before_active_end(const gd::Ref<MoveRow>& move, gd::Int entry_frame) {
    return move->active_first != 0 && entry_frame + 1 < move->active_last && move->active_last <= move->length;
}

// TransitionRemap (0x800311BC).
gd::Int FightMath::transition_remap(gd::Int code, const gd::Ref<MoveRow>& move, gd::Int entry_frame) {
    switch (code) {
        case Transition::CONTINUE:
            return enters_before_active_end(move, entry_frame) ? gd::Int(Transition::CONTINUE_TRACK_SLOW) : code;
        case Transition::TRACK_SLOW:
        case Transition::TRACK_HALF:
        case Transition::TRACK:
        case Transition::TRACK_AIM:
            return move->active_first != 0 ? code : gd::Int(Transition::TURN_AIM);
        case Transition::CONTINUE_TRACK_SLOW:
        case Transition::CONTINUE_TRACK:
            return enters_before_active_end(move, entry_frame) ? code : gd::Int(Transition::TURN_AIM);
        case Transition::CONTINUE_EASE_TURNING:
            return enters_before_active_end(move, entry_frame) ? gd::Int(Transition::CONTINUE_EASE) : code;
        default:
            break;
    }
    return code;
}

// MoveKeepsFrame (0x800312C4) for the pending move.
bool FightMath::move_keeps_frame(const gd::Ref<MoveRow>& move, gd::Int branch_kind, gd::Int pose_frame) {
    if (branch_kind == 3) {
        return false;
    }
    if (move->active_first == 0) {
        return true;
    }
    return move->active_first <= move->length && pose_frame <= move->active_last;
}

// SegmentHitsCylinder (0x800479A4): segment (x0, y0, z0, x1, y1, z1) against a vertical
// cylinder (cx, cy, cz, radius, radius^2): bounding boxes, the clip to the slab cy +- r, then
// the horizontal distance. The perpendicular-foot products wrap to 32 bits unless `exact`
// (game bug #9, a Gameplay fix: callers pass `rules.fix_segment_hit_overflow`).
bool FightMath::segment_hits_cylinder(const gd::PI32& seg, const gd::PI32& cyl, bool exact) {
    gd::Int x0 = seg[0];
    gd::Int y0 = seg[1];
    gd::Int z0 = seg[2];
    gd::Int x1 = seg[3];
    gd::Int y1 = seg[4];
    gd::Int z1 = seg[5];
    gd::Int cx = cyl[0];
    gd::Int cy = cyl[1];
    gd::Int cz = cyl[2];
    gd::Int r = Fx::s16(cyl[3]);
    gd::Int r2 = cyl[4];
    if ((gd::absi(x1 - x0) >> 1) + r < gd::absi(cx - Fx::div_trunc(x0 + x1, 2))) {
        return false;
    }
    if ((gd::absi(z1 - z0) >> 1) + r < gd::absi(cz - Fx::div_trunc(z0 + z1, 2))) {
        return false;
    }
    if ((gd::absi(y1 - y0) >> 1) + r < gd::absi(cy - Fx::div_trunc(y0 + y1, 2))) {
        return false;
    }
    gd::Int sx = x0;
    gd::Int sz = z0;
    gd::Int ex = x1;
    gd::Int ez = z1;
    gd::Int dx = x1 - x0;
    gd::Int dy = y1 - y0;
    gd::Int dz = z1 - z0;
    if (y0 != y1) {
        gd::Int lo = cy - r;
        gd::Int hi = cy + r;
        if (y1 < y0) {
            if (hi < y0) {
                gd::Int t = Fx::div_trunc(Fx::w32((hi - y0) * 0x1000), dy);
                sx = x0 + Fx::trunc12(Fx::w32(t * dx));
                sz = z0 + Fx::trunc12(Fx::w32(t * dz));
            }
            if (y1 < lo) {
                gd::Int t = Fx::div_trunc(Fx::w32((lo - y0) * 0x1000), dy);
                ex = x0 + Fx::trunc12(Fx::w32(t * dx));
                ez = z0 + Fx::trunc12(Fx::w32(t * dz));
            }
        } else {
            if (hi < y1) {
                gd::Int t = Fx::div_trunc(Fx::w32((hi - y0) * 0x1000), dy);
                ex = x0 + Fx::trunc12(Fx::w32(t * dx));
                ez = z0 + Fx::trunc12(Fx::w32(t * dz));
            }
            if (y0 < lo) {
                gd::Int t = Fx::div_trunc(Fx::w32((lo - y0) * 0x1000), dy);
                sx = x0 + Fx::trunc12(Fx::w32(t * dx));
                sz = z0 + Fx::trunc12(Fx::w32(t * dz));
            }
        }
    }
    gd::Int vx = ex - sx;
    gd::Int vz = ez - sz;
    gd::Int px = sx - cx;
    gd::Int pz = sz - cz;
    gd::Int length2 = Fx::w32(vx * vx + vz * vz);
    gd::Int proj = Fx::w32(-(vx * px + vz * pz));
    gd::Int d2 = 0;
    if (length2 == 0 || proj < 0) {
        d2 = Fx::w32(px * px + pz * pz);
    } else if (length2 < proj) {
        gd::Int qx = ex - cx;
        gd::Int qz = ez - cz;
        d2 = Fx::w32(qx * qx + qz * qz);
    } else if (exact) {
        px += Fx::div_trunc(vx * proj, length2);
        pz += Fx::div_trunc(vz * proj, length2);
        d2 = px * px + pz * pz;
    } else {
        px += Fx::div_trunc(Fx::w32(vx * proj), length2);
        pz += Fx::div_trunc(Fx::w32(vz * proj), length2);
        d2 = Fx::w32(Fx::w32(px * px) + Fx::w32(pz * pz));
    }
    return d2 <= r2;
}

}  // namespace t3
