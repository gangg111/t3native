// remake/presentation/fighter/arcade_face.gd
#include "presentation/fighter/arcade_face.h"

namespace t3 {

// A seed that differs for every face of the session (so two fighters of one costume, or two fights,
// do not blink alike) and is the same in every run of the program (screenshots repeat).
gd::Int ArcadeFace::next_seed() {
    _created += 1;
    return _created * SEED_STEP;
}

ArcadeFace::ArcadeFace(const gd::Ref<CharacterModel::ArcadeEyes>& eyes, gd::Int seed_value)
    : rand([this] { return _random(); }) {
    _eyes = eyes;
    _rng.set_seed(seed_value);
}

// One frame: `held` while the running move keeps the eyes closed (Blinker.held), `down` while it
// is a lying one (Blinker.lying). Returns the shape shown.
gd::Int ArcadeFace::step(bool held, bool down) {
    if (held) {
        _show(_eyes->held, HELD_FRAMES);
    } else if (down) {
        _show(_eyes->down, DOWN_FRAMES);
    }
    if (_hold == 0) {
        if (shape == OPEN) {
            _show(_eyes->blink, BLINK_FRAMES);
        } else {
            _show(OPEN, (rand() & OPEN_MASK) | 1);
        }
    }
    _hold -= 1;
    return shape;
}

// An attack shout of voice id `code` (0x2000 on) starts: the character's shout face for its frames.
void ArcadeFace::shout(gd::Int code) {
    gd::Int index = code - SHOUT_FIRST;
    if (index < 0 || index >= _eyes->shout_frames.size()) {
        return;
    }
    _show(_eyes->shout, _eyes->shout_frames[index]);
}

// FUN_80193EA4: the hold counter, and the shape when the costume has it.
void ArcadeFace::_show(gd::Int new_shape, gd::Int frames) {
    _hold = frames;
    if (new_shape != CharacterModel::ArcadeEyes::NO_SHAPE && new_shape != shape) {
        shape = new_shape;
    }
}

gd::Int ArcadeFace::_random() { return _rng.randi(); }

}  // namespace t3
