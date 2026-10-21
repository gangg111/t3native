// remake/core/game/flow_part.gd
#include "core/game/flow_part.h"

namespace t3 {

// `_flow = weakref(flow)`: the part keeps a non-owning pointer (INTERFACES.md).
FlowPart::FlowPart(GameFlow* flow) : g(flow) {}

}  // namespace t3
