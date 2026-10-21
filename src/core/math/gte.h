#pragma once
// remake/core/math/gte.gd: the libgte routines the fighter code calls, with the GTE's integer
// rules (tools/research/projection_sim.py): matrix x vector with MAC results, the 32-bit vector
// variant that splits each component into two 15-bit halves, VectorNormal and ratan2.
#include "core/math/fx.h"

namespace t3::Gte {

using gd::Int;
using gd::PI32;

// ApplyMatrix / ApplyRotMatrix: (m * v) >> 12 per row, read from the 32-bit MAC registers.
PI32 apply(const PI32& m, Int x, Int y, Int z);

// ApplyMatrixLV / ApplyRotMatrixLV: m * v for 32-bit components, as the high parts (>> 15,
// unshifted product, then << 3) plus the low 15 bits (product >> 12), signs kept.
PI32 apply_lv(const PI32& m, const PI32& v);

// VectorNormal (0x8003A86C): the vector (loaded as 16-bit) scaled to about 4096 with the
// reciprocal square root table.
PI32 vector_normal(const PI32& v, const PI32& rsqrt);

// libgte ratan2(y, x): the angle of (x, y) in 4096 units from a 1,025-entry octant table.
Int ratan2(Int y, Int x, const PI32& table);

// RTPS of the vector (0, 0, 0) with the translation `tr` (sf = 1, lm = 0): the screen
// position (SX, SY, clamped to -0x400...0x3FF) and SZ3 for projection plane `h` and the
// screen offsets `ofx`, `ofy` (pixels).
PI32 rtps_origin(const PI32& tr, Int h, Int ofx, Int ofy);

// The GTE's perspective division H / SZ3 (0.16 fixed point, at most 0x1FFFF) by the
// unsigned Newton-Raphson reciprocal (UNR table).
Int divide(Int h, Int sz3);

}  // namespace t3::Gte
