#pragma once
// remake/core/math/c_rand.gd: the C library's rand() of the PsyQ runtime (0x8007A73C):
// x = x * 0x41C64E6D + 0x3039, the result bits 16-30. The state is part of the simulation state.
#include "gd/gd.h"

namespace t3 {

class CRand {
public:
    gd::Int state = 0;

    void set_seed(gd::Int value) { state = value & 0xFFFFFFFF; }

    gd::Int next() {
        state = (state * 0x41C64E6D + 0x3039) & 0xFFFFFFFF;
        return (state >> 16) & 0x7FFF;
    }
};

}  // namespace t3
