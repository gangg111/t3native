#pragma once
// remake/presentation/fighter/blinker.gd
// Eye blinking (FUN_80034354, animation.md#blinking) as a presentation effect: the eyes stay
// open for a random 1-255 frames (odd), then close for 4. Moves with flag bit 18 and lying
// moves (state bit 2 without flag bit 30) keep them closed. The attract demonstration does
// not blink (the game skips the routine in game state 6).
#include <cstdint>

#include "gd/gd.h"

namespace t3 {

// Godot's RandomNumberGenerator (core/math/random_pcg.h: PCG32 with PCG_DEFAULT_INC_64), the part
// the fighter views use: `seed` and `randi()`. The runtime has none yet, so it lives here.
// `rng.seed = x` is `rng.set_seed(x)`.
class RandomNumberGenerator {
public:
    static constexpr uint64_t DEFAULT_INC = 1442695040888963407ULL;
    uint64_t state = 0;
    uint64_t inc = DEFAULT_INC;

    // RandomPCG::seed: pcg32_srandom_r(&pcg, seed, DEFAULT_INC).
    void set_seed(gd::Int seed_value) {
        state = 0U;
        inc = (DEFAULT_INC << 1u) | 1u;
        _next();
        state += static_cast<uint64_t>(seed_value);
        _next();
    }
    // A uint32 (pcg32_random_r).
    gd::Int randi() { return static_cast<gd::Int>(_next()); }

    uint32_t _next() {
        uint64_t oldstate = state;
        state = oldstate * 6364136223846793005ULL + (inc | 1);
        uint32_t xorshifted = static_cast<uint32_t>(((oldstate >> 18u) ^ oldstate) >> 27u);
        uint32_t rot = static_cast<uint32_t>(oldstate >> 59u);
        return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
    }
};

class Blinker {
public:
    static constexpr gd::Int CLOSED_FRAMES = 4;
    static constexpr gd::Int FLAG_EYES_CLOSED = 0x40000;
    static constexpr gd::Int FLAG_LOOK_AT = 0x40000000;
    static constexpr gd::Int STATE_DOWN = 0x4;

    RandomNumberGenerator rng;
    bool closed = false;
    gd::Int _left = 0;

    explicit Blinker(gd::Int seed_value = 0);

    // Whether a move (its +0x24 flags) keeps the eyes closed.
    static bool held(gd::Int flags);
    // Whether a move (its +0x24 flags and +0x04 state) is a lying one that keeps them closed.
    static bool lying(gd::Int flags, gd::Int state);
    // One frame of the running move (its +0x24 flags and +0x04 state); true while closed.
    bool step(gd::Int flags, gd::Int state);
    gd::Int _open_frames();
};

}  // namespace t3
