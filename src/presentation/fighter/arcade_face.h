#pragma once
// remake/presentation/fighter/arcade_face.gd
// The face of an arcade costume (FUN_80193F90 and FUN_80193EA4, arcade/README.md#eye-shapes), as a
// presentation effect: the eye shape shown, one step per simulation step.
//
// The game keeps the shape and a hold counter. A move with the closed-eyes field shows the "held"
// shape for 2 frames, a lying fighter the "down" one; when the counter runs out, open eyes (shape
// 0) turn to the costume's blink shape for 3 frames and any other shape back to open eyes for a
// random odd 1-255. An attack shout (SimEvents SHOUT) shows the character's shout shape for as many
// frames as its voice lasts (FUN_801A1C74); a costume lacking a shape keeps its face for that
// change. The blink and its random open times are this view's own (`rand`), not the simulation's.
#include <functional>

#include "content/character_model.h"
#include "gd/gd.h"
#include "presentation/fighter/blinker.h"

namespace t3 {

class ArcadeFace {
public:
    static constexpr gd::Int SHAPES = 6;
    static constexpr gd::Int OPEN = 0;
    static constexpr gd::Int BLINK_FRAMES = 3;
    static constexpr gd::Int HELD_FRAMES = 2;
    static constexpr gd::Int DOWN_FRAMES = 2;
    static constexpr gd::Int SHOUT_FIRST = 0x2000;   // the first voice id with a shout face
    static constexpr gd::Int OPEN_MASK = 0xFE;       // the open time is `(rand & 0xFE) | 1` frames
    static constexpr gd::Int SEED_STEP = 7919;       // between the seeds of two faces (a prime)

    static inline gd::Int _created = 0;              // the faces made so far in this session (`next_seed`)

    gd::Int shape = OPEN;                            // the shape shown
    std::function<gd::Int()> rand;                   // the random numbers of the open times (Callable(self, "_random"))
    gd::Ref<CharacterModel::ArcadeEyes> _eyes;
    gd::Int _hold = 0;
    RandomNumberGenerator _rng;

    // A seed that differs for every face of the session (so two fighters of one costume, or two fights,
    // do not blink alike) and is the same in every run of the program (screenshots repeat).
    static gd::Int next_seed();

    explicit ArcadeFace(const gd::Ref<CharacterModel::ArcadeEyes>& eyes, gd::Int seed_value = 0);
    ArcadeFace(const ArcadeFace&) = delete;              // `rand` captures this
    ArcadeFace& operator=(const ArcadeFace&) = delete;

    // One frame: `held` while the running move keeps the eyes closed (Blinker.held), `down` while it
    // is a lying one (Blinker.lying). Returns the shape shown.
    gd::Int step(bool held, bool down);
    // An attack shout of voice id `code` (0x2000 on) starts: the character's shout face for its frames.
    void shout(gd::Int code);
    // FUN_80193EA4: the hold counter, and the shape when the costume has it.
    void _show(gd::Int new_shape, gd::Int frames);
    gd::Int _random();
};

}  // namespace t3
