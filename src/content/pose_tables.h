#pragma once
// remake/content/pose_tables.gd: constant tables of the pose pipeline, converted from the game
// executable (`imported/tables/pose.json`): sine/cosine, SquareRoot0 mantissas, the Euler joint
// slots, the limb basis and the normalised skeleton offsets.
#include <string>

#include "core/math/fx.h"

namespace t3 {

class PoseTables {
public:
    static constexpr gd::Int COS_OFFSET = 1024;   // g_cosTable starts a quarter turn into g_sinTable
    static constexpr gd::Int SQRT_BASE = 0x40;

    gd::PI32 sin_table;
    gd::PI32 sqrt_table;
    gd::PI32 euler_slots;
    gd::PI32 limb_basis;
    gd::Arr<gd::PI32> skeleton_offsets;   // chest, right/left shoulder, hips
    gd::Arr<gd::PI32> limb_offsets;       // IK root offsets: arms, legs

    static gd::Ref<PoseTables> load_from(const std::string& path);

    // Sine of a 16-bit angle (65536 = one turn), 4.12, as `tab.sin` in the pose code.
    gd::Int sin16(gd::Int angle) const { return sin_table[(angle >> 4) & 0xFFF]; }
    gd::Int cos16(gd::Int angle) const { return sin_table[COS_OFFSET + ((angle >> 4) & 0xFFF)]; }
    // Entry of the 4096-unit tables (`fight_math.trig_raw`).
    gd::Int sin12(gd::Int index) const { return sin_table[index & 0xFFF]; }
    gd::Int cos12(gd::Int index) const { return sin_table[COS_OFFSET + (index & 0xFFF)]; }

    // libgte SquareRoot0 with the game's mantissa table.
    gd::Int square_root0(gd::Int a) const {
        if (a == 0) return 0;
        gd::Int lz = 32 - Fx::bit_length(a);
        lz &= ~gd::Int(1);
        gd::Int x = lz < 24 ? gd::shr(a, 24 - lz) : gd::shl(a, lz - 24);
        gd::Int mant = sqrt_table[x - SQRT_BASE] & 0xFFFFFFFF;
        return (gd::shl(mant, (31 - lz) >> 1) & 0xFFFFFFFF) >> 12;
    }

    static gd::Arr<gd::PI32> _triples(const gd::PI32& values);
};

}  // namespace t3
