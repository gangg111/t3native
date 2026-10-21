#include "core/math/gte.h"

namespace t3::Gte {

PI32 apply(const PI32& m, Int x, Int y, Int z) {
    x = Fx::s16(x);
    y = Fx::s16(y);
    z = Fx::s16(z);
    return PI32{
        Fx::w32((m[0] * x + m[1] * y + m[2] * z) >> 12),
        Fx::w32((m[3] * x + m[4] * y + m[5] * z) >> 12),
        Fx::w32((m[6] * x + m[7] * y + m[8] * z) >> 12),
    };
}

PI32 apply_lv(const PI32& m, const PI32& v) {
    PI32 hi{0, 0, 0};
    PI32 lo{0, 0, 0};
    for (Int k = 0; k < 3; ++k) {
        Int c = Fx::w32(v[k]);
        if (c >= 0) {
            hi[k] = c >> 15;
            lo[k] = c & 0x7FFF;
        } else {
            hi[k] = -((-c) >> 15);
            lo[k] = -((-c) & 0x7FFF);
        }
    }
    PI32 out{0, 0, 0};
    for (Int r = 0; r < 3; ++r) {
        Int h = m[3 * r] * hi[0] + m[3 * r + 1] * hi[1] + m[3 * r + 2] * hi[2];
        Int l = (m[3 * r] * lo[0] + m[3 * r + 1] * lo[1] + m[3 * r + 2] * lo[2]) >> 12;
        out[r] = Fx::w32(l + (h << 3));
    }
    return out;
}

PI32 vector_normal(const PI32& v, const PI32& rsqrt) {
    Int x = Fx::s16(v[0]);
    Int y = Fx::s16(v[1]);
    Int z = Fx::s16(v[2]);
    Int sum = Fx::w32(x * x + y * y + z * z);
    if (sum == 0) return PI32{0, 0, 0};
    Int lz = 0;
    if (sum >= 0) lz = (32 - Fx::bit_length(sum)) & 0x1E;
    Int shift = (31 - lz) >> 1;
    Int index = lz >= 24 ? Fx::w32(gd::shl(sum, lz - 24)) : gd::shr(sum, 24 - lz);
    Int ir0 = rsqrt[index - 0x40];
    return PI32{gd::shr(Fx::w32(ir0 * x), shift), gd::shr(Fx::w32(ir0 * y), shift), gd::shr(Fx::w32(ir0 * z), shift)};
}

Int ratan2(Int y, Int x, const PI32& table) {
    y = Fx::w32(y);
    x = Fx::w32(x);
    bool neg_x = x < 0;
    bool neg_y = y < 0;
    x = gd::absi(x);
    y = gd::absi(y);
    if (x == 0 && y == 0) return 0;
    Int v = 0;
    if (y < x) {
        Int t = (y & 0x7FE00000) == 0 ? Fx::div_trunc(Fx::w32(y << 10), x) : Fx::div_trunc(y, x >> 10);
        v = table[t];
    } else {
        Int t = (x & 0x7FE00000) == 0 ? Fx::div_trunc(Fx::w32(x << 10), y) : Fx::div_trunc(x, y >> 10);
        v = 0x400 - table[t];
    }
    if (neg_x) v = 0x800 - v;
    if (neg_y) v = -v;
    return v;
}

PI32 rtps_origin(const PI32& tr, Int h, Int ofx, Int ofy) {
    Int sz = gd::clampi(Fx::w32(tr[2]), 0, 0xFFFF);
    Int d = divide(h & 0xFFFF, sz);
    Int sx = gd::clampi((d * Fx::sat16(Fx::w32(tr[0])) + (ofx << 16)) >> 16, -0x400, 0x3FF);
    Int sy = gd::clampi((d * Fx::sat16(Fx::w32(tr[1])) + (ofy << 16)) >> 16, -0x400, 0x3FF);
    return PI32{sx, sy, sz};
}

Int divide(Int h, Int sz3) {
    if (h >= sz3 * 2) return 0x1FFFF;
    Int z = 16 - Fx::bit_length(sz3);
    Int n = gd::shl(h, z);
    Int d = gd::shl(sz3, z);
    Int i = (d - 0x7FC0) >> 7;
    Int u = gd::maxi(0, ((gd::div(0x40000, i + 0x100) + 1) >> 1) - 0x101) + 0x101;
    d = (0x2000080 - d * u) >> 8;
    d = (0x0000080 + d * u) >> 8;
    return gd::mini(0x1FFFF, (n * d + 0x8000) >> 16);
}

}  // namespace t3::Gte
