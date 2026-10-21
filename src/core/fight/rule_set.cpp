// remake/core/fight/rule_set.gd
// Rule flags of the simulation (remake-plan.md#original-bugs): the Gameplay fixes, each named
// after its entry in docs/research/code/game-bugs.md. The Gameplay fixes option sets
// every fix flag at once; the individual flags exist for tests. The text region is not a rule:
// both regions play as Japan Rev.1 and differ only in their texts (GameRam, FlowData).
#include "core/fight/rule_set.h"

namespace t3 {

gd::Ref<RuleSet> RuleSet::original() {
    return gd::make<RuleSet>();
}

gd::Ref<RuleSet> RuleSet::with_gameplay_fixes() {
    gd::Ref<RuleSet> r = gd::make<RuleSet>();
    r->set_gameplay_fixes(true);
    return r;
}

// The Gameplay fixes option: every fix flag (the `fix_` variables) on or off.
// Reflection (get_property_list() and set(field, on) for every name beginning with FIX_PREFIX)
// replaced by the explicit member list, in declaration order.
void RuleSet::set_gameplay_fixes(bool on) {
    fix_juggle_arc_overflow = on;
    fix_elbow_bend_overflow = on;
    fix_segment_hit_overflow = on;
    fix_tap_latch_register = on;
    fix_ai_lookahead_row = on;
    fix_ai_candidate_pick = on;
    fix_ai_continuation = on;
    fix_ai_guard_count = on;
    fix_ai_damage_history = on;
    fix_unlock_pace = on;
    fix_team_least_used = on;
    fix_ball_opponent = on;
    fix_ball_cpu = on;
    fix_force_slot_timers = on;
}

}  // namespace t3
