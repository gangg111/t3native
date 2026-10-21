#pragma once
// remake/core/fight/rule_set.gd
// Rule flags of the simulation (remake-plan.md#original-bugs): the Gameplay fixes, each named
// after its entry in docs/research/code/game-bugs.md. The Gameplay fixes option sets
// every fix flag at once; the individual flags exist for tests. The text region is not a rule:
// both regions play as Japan Rev.1 and differ only in their texts (GameRam, FlowData).
#include <string>

#include "gd/gd.h"

namespace t3 {

class RuleSet {
public:
    static inline const std::string FIX_PREFIX = "fix_";

    bool fix_juggle_arc_overflow = false;     // #7 LaunchTrajectory's wrapped flight time
    bool fix_elbow_bend_overflow = false;     // #8 IkSolveLimb's 32-bit product
    bool fix_segment_hit_overflow = false;    // #9 SegmentHitsCylinder's 32-bit products
    bool fix_tap_latch_register = false;      // #52 LatchButtonTaps stores a stale register
    bool fix_ai_lookahead_row = false;        // #18 AiCollectCandidates tests the look-ahead condition on the wrong row
    bool fix_ai_candidate_pick = false;       // #19 AiExecuteCandidate's forward walk stops at k ≤ 0
    bool fix_ai_continuation = false;         // #20 FUN_80058ED0 reads the continuation after a common block
    bool fix_ai_guard_count = false;          // #21 FUN_800582A0 returns only its last pass's count
    bool fix_ai_damage_history = false;       // #23 the repeated-move reaction compares with history slot 0 only
    bool fix_unlock_pace = false;             // #29 FUN_800B2350 counts bit positions, not the cleared mask
    bool fix_team_least_used = false;         // #60 team battle's least-used member goes to human teams
    bool fix_ball_opponent = false;           // #30, #31 Tekken Ball's opponent: least met first, Gon counted
    bool fix_ball_cpu = false;                // #3, #24 Tekken Ball's CPU: its attack filter and projection
    bool fix_force_slot_timers = false;       // #41 Tekken Force's 16-bit enemy slot timers

    static gd::Ref<RuleSet> original();

    static gd::Ref<RuleSet> with_gameplay_fixes();

    // The Gameplay fixes option: every fix flag (the `fix_` variables) on or off.
    // Reflection replaced: the GDScript walks get_property_list() and sets every property whose
    // name begins with FIX_PREFIX; here every fix_ member is assigned explicitly, in declaration
    // order (a new fix_ flag must be added to the list in rule_set.cpp too).
    void set_gameplay_fixes(bool on);
};

}  // namespace t3
