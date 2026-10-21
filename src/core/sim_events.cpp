// remake/core/sim_events.gd
// What one simulation step asks the presentation to do (remake-plan.md#one-step): effect
// spawns, sounds, music, vibration and the like. The simulation never plays anything itself.
#include "core/sim_events.h"

namespace t3 {

SimEvents::Event::Event(gd::Int event_kind, gd::Int owner, gd::Int first, gd::Int second) {
    kind = event_kind;
    fighter = owner;
    a = first;
    b = second;
}

std::string SimEvents::Event::_to_string() const {
    return KIND_NAMES[kind] + "(f" + gd::str(fighter) + ", " + gd::str(a) + ", " + gd::str(b) + ", " + gd::str(c) + ")";
}

gd::Ref<SimEvents::Event> SimEvents::add(gd::Int kind, gd::Int fighter, gd::Int a, gd::Int b) const {
    gd::Ref<Event> e = gd::make<Event>(kind, fighter, a, b);
    items.append(e);
    return e;
}

gd::Ref<SimEvents::Event> SimEvents::add_at(gd::Int kind, gd::Int fighter, const gd::PI32& point, gd::Int a, gd::Int b) const {
    gd::Ref<Event> e = add(kind, fighter, a, b);
    e->point = point;
    return e;
}

// The step's first event of this kind, or null.
gd::Ref<SimEvents::Event> SimEvents::first(gd::Int kind) const {
    for (const auto& e : items) {
        if (e->kind == kind) {
            return e;
        }
    }
    return nullptr;
}

// Whether the step raised an event of this kind.
bool SimEvents::has(gd::Int kind) const {
    return first(kind) != nullptr;
}

void SimEvents::clear() const {
    items.clear();
}

}  // namespace t3
