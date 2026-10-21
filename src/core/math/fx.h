#pragma once
// remake/core/math/fx.gd: integer arithmetic of the original game: 4.12 fixed point, 16-bit GTE
// registers and 32-bit C ints. Matrices are row-major PackedInt32Array(9); vectors (3).
//
// GDScript ints are 64-bit, so every place where the original relies on 16- or 32-bit
// wrap-around or GTE saturation calls the matching helper explicitly.
#include "gd/gd.h"

namespace t3::Fx {

using gd::Int;
using gd::PI32;

constexpr Int ONE = 0x1000;
inline const gd::Arr<Int>& IDENTITY() {
    static const gd::Arr<Int> v{ONE, 0, 0, 0, ONE, 0, 0, 0, ONE};
    return v;
}

// Wraps to a signed 16-bit value (a halfword store).
inline Int s16(Int v) {
    v &= 0xFFFF;
    return v & 0x8000 ? v - 0x10000 : v;
}

// Wraps to a signed 8-bit value (a byte store).
inline Int s8(Int v) {
    v &= 0xFF;
    return v & 0x80 ? v - 0x100 : v;
}

// The number of bits needed for a non-negative value (32 minus the count of leading zeros).
inline Int bit_length(Int v) {
    Int n = 0;
    while (v > 0) {
        v >>= 1;
        n += 1;
    }
    return n;
}

// GTE IR saturation with lm = 0.
inline Int sat16(Int v) { return gd::clampi(v, -0x8000, 0x7FFF); }

// Wraps to a signed 32-bit value, as C int arithmetic on the R3000.
inline Int w32(Int v) {
    v &= 0xFFFFFFFF;
    return v & 0x80000000 ? v - 0x100000000 : v;
}

// C integer division, truncating toward zero.
inline Int div_trunc(Int a, Int b) {
    Int q = gd::div(gd::absi(a), gd::absi(b));
    return (a >= 0) == (b >= 0) ? q : -q;
}

// Arithmetic shift right by 12 rounding toward zero (C division by 4096).
inline Int trunc12(Int v) { return v < 0 ? (v + 0xFFF) >> 12 : v >> 12; }

inline PI32 identity() { return PI32{ONE, 0, 0, 0, ONE, 0, 0, 0, ONE}; }

// GTE MVMVA rotation x vector with sf = 1, lm = 0 (IR results, saturated).
inline PI32 mvmva(const PI32& m, Int x, Int y, Int z) {
    return PI32{
        sat16((m[0] * x + m[1] * y + m[2] * z) >> 12),
        sat16((m[3] * x + m[4] * y + m[5] * z) >> 12),
        sat16((m[6] * x + m[7] * y + m[8] * z) >> 12),
    };
}

// GTE GPF with sf = 1, lm = 0: sat16((ir0 * v) >> 12) for three IR values.
inline PI32 mvmva_gpf(Int ir0, Int a, Int b, Int c) {
    Int w = s16(ir0);
    return PI32{sat16((w * s16(a)) >> 12), sat16((w * s16(b)) >> 12), sat16((w * s16(c)) >> 12)};
}

// libgte MulMatrix: m0 x m1, one column at a time through MVMVA, stored as s16.
inline PI32 mul_matrix(const PI32& m0, const PI32& m1) {
    PI32 out;
    out.resize(9);
    for (Int c = 0; c < 3; ++c) {
        PI32 col = mvmva(m0, m1[c], m1[3 + c], m1[6 + c]);
        out[c] = s16(col[0]);
        out[3 + c] = s16(col[1]);
        out[6 + c] = s16(col[2]);
    }
    return out;
}

inline PI32 transpose(const PI32& m) { return PI32{m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]}; }

}  // namespace t3::Fx
