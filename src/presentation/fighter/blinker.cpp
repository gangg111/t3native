// remake/presentation/fighter/blinker.gd
#include "presentation/fighter/blinker.h"

namespace t3 {

Blinker::Blinker(gd::Int seed_value) {
    rng.set_seed(seed_value);
    _left = _open_frames();
}

// Whether a move (its +0x24 flags) keeps the eyes closed.
bool Blinker::held(gd::Int flags) { return (flags & FLAG_EYES_CLOSED) != 0; }

// Whether a move (its +0x24 flags and +0x04 state) is a lying one that keeps them closed.
bool Blinker::lying(gd::Int flags, gd::Int state) { return (state & STATE_DOWN) != 0 && (flags & FLAG_LOOK_AT) == 0; }

// One frame of the running move (its +0x24 flags and +0x04 state); true while closed.
bool Blinker::step(gd::Int flags, gd::Int state) {
    if (held(flags) || lying(flags, state)) {
        closed = true;
        _left = 2;
        return closed;
    }
    _left -= 1;
    if (_left <= 0) {
        closed = !closed;
        _left = closed ? CLOSED_FRAMES : _open_frames();
    }
    return closed;
}

gd::Int Blinker::_open_frames() { return (rng.randi() & 0xFE) + 1; }

}  // namespace t3
