#pragma once
// remake/core/game/flow_part.gd
// Base of the screens and parts GameFlow owns: they reach the flow through `g`, held weakly
// so the flow and its parts do not keep each other alive.
//
// Not mechanical: the GDScript keeps a WeakRef `_flow` and a property `g` that dereferences it.
// GameFlow creates its parts in its own constructor (`ModeRules.new(self)`), where no shared
// pointer to it exists yet, so the part keeps a plain non-owning pointer: `g` is that pointer
// (GDScript `g.fight` is `g->fight`) and the flow passes `this`.
#include "gd/gd.h"

namespace t3 {

class GameFlow;

class FlowPart {
public:
    GameFlow* g = nullptr;   // the owning flow (non-owning pointer: GameFlow outlives its parts)

    explicit FlowPart(GameFlow* flow);
    virtual ~FlowPart() = default;
};

}  // namespace t3
