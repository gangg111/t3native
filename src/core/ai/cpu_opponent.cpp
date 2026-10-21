// remake/core/ai/cpu_opponent.gd
// The CPU opponent's shared machinery (ai.md), written from the verified port
// `tools/research/ai_sim.py`: the candidate table filled from the running move's branch list and its
// filters, executing a candidate as pad input, input scripts, the character hooks, and the
// record set-up (AiInitRound, AiReset, AiLoadParams). AiDecision runs AiUpdate on top of it.
//
// As in the game, one candidate table serves every CPU fighter, and `ai_self` / `ai_opp` are
// globals set by the last AiInitRound or AiUpdate (AiReset reads them, not its argument).
#include "core/ai/cpu_opponent.h"

#include "content/ai_tables.h"
#include "content/fight_tables.h"
#include "content/motion_bank.h"
#include "content/move_row.h"
#include "core/ai/ai_record.h"
#include "core/combat/attack_word.h"
#include "core/combat/state_bit.h"
#include "core/combat/state_word.h"
#include "core/fight/fight_math.h"
#include "core/fight/fight_state.h"
#include "core/fight/rule_set.h"
#include "core/fighter/bank_type.h"
#include "core/fighter/fighter_state.h"
#include "core/game/game_mode.h"
#include "core/game/mode_region.h"
#include "core/math/c_rand.h"
#include "core/math/fx.h"

namespace t3 {

CpuOpponent::CpuOpponent(const gd::Ref<FightState>& fight_state, const gd::Ref<MoveSystem>& move_system) {
    fight = fight_state;
    moves = move_system;
    t = fight->tables->ai;
    for (gd::Int i = 0; i < 3; ++i) {
        records.append(gd::make<AiRecord>());
    }
    cand_target.resize(MAX_CANDIDATES);
    cand_row.resize(MAX_CANDIDATES);
    cand_mark.resize(MAX_CANDIDATES);
}

void CpuOpponent::set_ai_self(const gd::Ref<FighterState>& v) {
    ai_self = v;
    fight->region->put32(ModeRegion::AI_SELF, FighterState::address(v->index));
}

gd::Ref<AiRecord> CpuOpponent::record(const gd::Ref<FighterState>& f) const {
    return records[f->ai_slot];
}

// ---- random numbers

// The AI generator 0x800AE168 (x = 5x + 3): returns the old value and advances it.
gd::Int CpuOpponent::lcg_next() {
    gd::Int x = fight->ai_rng;
    fight->ai_rng = (5 * x + 3) & 0xFFFFFFFFLL;
    return x;
}

// FUN_800569D0 and the draws like it: n · ((rand() + LCG) & 0x7FFF) >> 15.
gd::Int CpuOpponent::random_below(gd::Int n) {
    gd::Int r = fight->rng->next();
    gd::Int x = lcg_next();
    gd::Int p = n * ((r + x) & 0x7FFF);
    if (p < 0) {
        p += 0x7FFF;
    }
    return p >> 15;
}

// ---- branch lists

// The move a branch row leads to through the index of AI slot `slot` (record +0x0C).
gd::Ref<MoveRow> CpuOpponent::slot_move(gd::Int slot, const gd::PI32& row) const {
    gd::Arr<gd::Ref<MoveRow>> table = fight->slots[slot];
    gd::Int s = row[ROW_TARGET] & 0xFFFF;
    return s < table.size() ? table[s] : gd::Ref<MoveRow>();
}

// The rows of a branch list with the common blocks (0xC00C) expanded by their count, without
// the terminator.
gd::Arr<gd::PI32> CpuOpponent::list_rows(const gd::Ref<MoveRow>& move) const {
    gd::Arr<gd::PI32> out;
    gd::Arr<gd::PI32> list = move->bank->branches;
    gd::Int i = move->branches;
    while (list[i][ROW_COMMAND] != END) {
        gd::PI32 row = list[i];
        if (row[ROW_COMMAND] == COMMON) {
            gd::Int base = row[ROW_TARGET];
            gd::Int n = row[ROW_ENTRY];
            for (gd::Int k = 0; k < n; ++k) {
                out.append(fight->tables->common_branches[base + k]);
            }
        } else {
            out.append(row);
        }
        i += 1;
    }
    return out;
}

// The branch-row restriction byte as the AI tests it (values above 0x73 never match).
bool CpuOpponent::restriction_ok(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, gd::Int value) {
    if (value == 0) {
        return true;
    }
    if (value < 0x18) {
        return f->bank_type == value - 1;
    }
    if (value < 0x2F) {
        return f->bank_type != value - 0x18;
    }
    if (value < 0x46) {
        return opp->bank_type == value - 0x2F;
    }
    if (value < 0x5D) {
        return f->char_id == value - 0x46;
    }
    if (value > 0x73) {
        return false;
    }
    return f->char_id != value - 0x5D;
}

// A row's condition with the situation bits standing in for the conditions they cache.
bool CpuOpponent::cond_ok(const gd::PI32& row, const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp,
                          gd::Int sit) {
    gd::Int c = row[ROW_CONDITION];
    if (c == BranchCondition::ALWAYS) {
        return true;
    }
    gd::Int k = SITUATION_CONDITIONS.find(c);
    if (k >= 0) {
        return (sit & situation_bit(c)) != 0;
    }
    return moves->condition(row, f, opp);
}

gd::Int CpuOpponent::situation_bit(gd::Int condition) {
    return SITUATION_BITS[SITUATION_CONDITIONS.find(condition)];
}

// AiCollectCandidates (0x80056A4C): the rows of the running move (record +0x58) open now into
// the table; unless `current_only`, also those opening on frame 1 of the default continuation.
// Returns the count. Two quirks are kept: the look-ahead tests an ordinary row's condition on
// the row after the last common block (or the list start; bug #18, fixed by
// RuleSet.fix_ai_lookahead_row), and the stale-word test of bug #17 never matches.
gd::Int CpuOpponent::collect_candidates(const gd::Ref<AiRecord>& rec, bool current_only) {
    gd::Ref<FighterState> f = ai_self;
    gd::Ref<FighterState> opp = ai_opp;
    gd::Int sit = rec->situation;
    gd::Int count = 0;
    gd::Ref<MoveRow> move = rec->collected_move;
    // First walk: the rows open on the current pose frame.
    gd::Arr<gd::PI32> list = move->bank->branches;
    gd::Int i = move->branches;
    gd::Arr<gd::PI32> stop_list = list;
    gd::Int stop_i = -1;
    while (list[i][ROW_COMMAND] != END) {
        gd::PI32 r = list[i];
        if (r[ROW_COMMAND] == COMMON) {
            gd::Int base = r[ROW_TARGET];
            bool filled = false;
            gd::Int n = r[ROW_ENTRY];
            for (gd::Int k = 0; k < n; ++k) {
                gd::PI32 row = fight->tables->common_branches[base + k];
                if (_open_now(row, f, opp) && _cond_ok_lookahead(row, row, false, f, opp, sit)) {
                    if (count > MAX_CANDIDATES - 1) {
                        filled = true;
                        break;
                    }
                    count = _add(rec, count, row);
                }
            }
            if (filled) {
                stop_i = i;
                break;
            }
        } else if (_open_now(r, f, opp) && _cond_ok_lookahead(r, r, false, f, opp, sit)) {
            if (count > MAX_CANDIDATES - 1) {
                stop_i = i;
                break;
            }
            count = _add(rec, count, r);
        }
        i += 1;
    }
    if (stop_i < 0) {
        stop_list = list;
        stop_i = i;
    }
    if (current_only) {
        return count;
    }
    gd::PI32 stop = stop_list[stop_i];
    gd::Ref<MoveRow> nxt = slot_move(rec->slot, stop);
    if (nxt != nullptr && stop[ROW_FIRST] <= f->pose_frame && f->pose_frame <= stop[ROW_LAST] && nxt != f->pose_move) {
        count = _walk_lookahead(rec, nxt, count, f, opp, sit);
    }
    for (gd::Int k = 0; k < count; ++k) {
        if ((cand_target[k]->state & StateBit::AI_UNUSABLE) != 0) {
            cand_mark[k] = -1;
        }
    }
    if (rec->filter_no_lows) {
        rec->candidate_count = Fx::s16(count);
        filter_no_lows(rec);
    }
    return count;
}

gd::Int CpuOpponent::_walk_lookahead(const gd::Ref<AiRecord>& rec, const gd::Ref<MoveRow>& move, gd::Int count,
                                     const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, gd::Int sit) {
    gd::Arr<gd::PI32> list = move->bank->branches;
    gd::Int i = move->branches;
    gd::PI32 cond_row = list[i];
    while (list[i][ROW_COMMAND] != END) {
        gd::PI32 r = list[i];
        if (r[ROW_COMMAND] == COMMON) {
            gd::Int base = r[ROW_TARGET];
            gd::Int n = r[ROW_ENTRY];
            for (gd::Int k = 0; k < n; ++k) {
                gd::PI32 row = fight->tables->common_branches[base + k];
                if (_open_lookahead(row, f, opp) && _cond_ok_lookahead(row, row, true, f, opp, sit)) {
                    if (count > MAX_CANDIDATES - 1) {
                        return count;
                    }
                    count = _add(rec, count, row);
                }
            }
            cond_row = fight->tables->common_branches[base + r[ROW_ENTRY]];
        } else if (_open_lookahead(r, f, opp) && _cond_ok_lookahead(r, fight->rules->fix_ai_lookahead_row ? r : cond_row,
                                                                     true, f, opp, sit)) {
            if (count > MAX_CANDIDATES - 1) {
                return count;
            }
            count = _add(rec, count, r);
        }
        i += 1;
    }
    return count;
}

bool CpuOpponent::_cond_ok_lookahead(const gd::PI32& row, const gd::PI32& cond_row, bool lookahead,
                                     const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, gd::Int sit) {
    gd::Int c = row[ROW_CONDITION];
    if (c == 0) {
        return true;
    }
    if (c == 0x24 || c == 0x25) {
        return (sit & situation_bit(c)) != 0;
    }
    if (c == 0x26 || c == 0x30 || c == 0x27) {
        return lookahead ? true : (sit & situation_bit(c)) != 0;
    }
    return moves->condition(cond_row, f, opp);
}

bool CpuOpponent::_open_now(const gd::PI32& row, const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp) const {
    return row[ROW_FIRST] <= f->pose_frame && f->pose_frame <= row[ROW_LAST]
        && restriction_ok(f, opp, row[ROW_RESTRICTION]);
}

bool CpuOpponent::_open_lookahead(const gd::PI32& row, const gd::Ref<FighterState>& f,
                                  const gd::Ref<FighterState>& opp) const {
    return row[ROW_FIRST] < 2 && f->pose_frame <= row[ROW_LAST] && restriction_ok(f, opp, row[ROW_RESTRICTION]);
}

gd::Int CpuOpponent::_add(const gd::Ref<AiRecord>& rec, gd::Int count, const gd::PI32& row) {
    gd::Ref<MoveRow> target = slot_move(rec->slot, row);
    if (target == nullptr) {
        return count;
    }
    cand_target[count] = target;
    cand_row[count] = row;
    cand_mark[count] = 0;
    return count + 1;
}

// ---- filters

// The loop every filter shares: marks each unmarked candidate that passes `pred(target, row)`,
// adds the number to the marked count (+0x72) and returns it.
gd::Int CpuOpponent::mark_candidates(const gd::Ref<AiRecord>& rec,
                                     const std::function<bool(const gd::Ref<MoveRow>&, const gd::PI32&)>& pred) {
    gd::Int n = rec->candidate_count;
    gd::Int marked = 0;
    for (gd::Int k = 0; k < n; ++k) {
        if (cand_mark[k] == 0 && pred(cand_target[k], cand_row[k])) {
            cand_mark[k] = 1;
            marked += 1;
        }
    }
    if (n > 0) {
        rec->marked_count = Fx::s16(rec->marked_count + marked);
    }
    return marked;
}

// The target's reach bits (none: close or middle) against the distance band's mask.
bool CpuOpponent::reach_ok(const gd::Ref<AiRecord>& rec, const gd::Ref<MoveRow>& target) const {
    gd::Int w = target->attack_word;
    gd::Int reach = w & 0xFFFF0000LL;
    if ((w & HINT_REACH) == 0) {
        reach |= HINT_CLOSE | HINT_MIDDLE;
    }
    return (reach & gd::Int(t->band_reach[rec->band])) != 0;
}

bool CpuOpponent::is_attack(const gd::Ref<MoveRow>& m) {
    return m->active_first != 0 && m->air_first == 0 && (m->attack_word & HINT_NEVER) == 0
        && (m->state & StateBit::NOT_AI_ATTACK) == 0;
}

bool CpuOpponent::grounded_or_special(const gd::Ref<MoveRow>& m) {
    return m->air_first == 0 || (m->state & 0x80000) != 0 || (m->attack_word & HINT_SPECIAL) != 0;
}

bool CpuOpponent::_attack_in_reach(const gd::Ref<AiRecord>& rec, const gd::Ref<MoveRow>& m) const {
    return (m->attack_word & HINT_NEVER) == 0 && (m->state & StateBit::NOT_AI_ATTACK) == 0
        && (reach_ok(rec, m) || (m->state & StateBit::REACH_ANY) != 0);
}

gd::Int CpuOpponent::damage_of(const gd::Ref<MoveRow>& m) {
    return Fx::s16(m->damage_word);
}

// FUN_8005758C: side steps (0xC001), state bit 19 moves, and attacks in reach.
gd::Int CpuOpponent::filter_attacks(const gd::Ref<AiRecord>& rec) {
    return mark_candidates(rec, [=, this](const gd::Ref<MoveRow>& m, const gd::PI32& br) -> bool {
        if (!grounded_or_special(m)) {
            return false;
        }
        if (br[ROW_COMMAND] == SIDE_STEP || (m->state & 0x80000) != 0) {
            return true;
        }
        return m->active_first != 0 && _attack_in_reach(rec, m);
    });
}

// FUN_800576DC: attacks in reach except the attack words 0x412 and 0x706.
gd::Int CpuOpponent::filter_attacks_no_special(const gd::Ref<AiRecord>& rec) {
    return mark_candidates(rec, [=, this](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
        gd::Int a = m->attack_word & 0xFFFF;
        return grounded_or_special(m) && m->active_first != 0 && a != AttackWord::HIGH && a != AttackWord::UNBLOCKABLE_2
            && _attack_in_reach(rec, m);
    });
}

// FUN_80057820: damaging ground attacks with startup below 14 frames.
gd::Int CpuOpponent::filter_fast_attacks(const gd::Ref<AiRecord>& rec) {
    return mark_candidates(rec, [=, this](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
        return damage_of(m) > 0 && m->active_first != 0 && m->air_first == 0 && m->active_first < 14
            && _attack_in_reach(rec, m);
    });
}

// FUN_80057954: attacks in reach whose level suits the opponent's posture (+0x230).
gd::Int CpuOpponent::filter_vs_posture(const gd::Ref<AiRecord>& rec) {
    return mark_candidates(rec, [=, this](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
        if (!is_attack(m) || damage_of(m) < 0 || !reach_ok(rec, m)) {
            return false;
        }
        gd::Int g = rec->opp_state;
        gd::Int a = m->attack_word & 0xFFFF;
        if ((g & StateBit::STANDING) != 0) {
            return a != AttackWord::HIGH;
        }
        if ((g & StateBit::CROUCHING) != 0) {
            return a != AttackWord::HIGH && a != AttackWord::LOW;
        }
        return !((g & StateBit::DOWN) != 0 && (a == AttackWord::HIGH || a == AttackWord::MID));
    });
}

// Attack level `a` against the opponent state `g` (FUN_80057AC4, FUN_80057C88).
bool CpuOpponent::level_vs_state(gd::Int g, gd::Int a) {
    if ((g & 0xFFFF) == StateWord::LAUNCHED) {
        return true;
    }
    if ((g & StateBit::AIRBORNE) != 0) {
        return a == AttackWord::HIGH || a == AttackWord::MID || a == AttackWord::MID_2;
    }
    if ((g & StateBit::DOWN) != 0) {
        return a == AttackWord::LOW || a == AttackWord::LOW_2;
    }
    if ((g & StateBit::CROUCHING) != 0) {
        return a == AttackWord::MID || a == AttackWord::LOW;
    }
    if ((g & StateBit::STANDING) != 0) {
        return a == AttackWord::LOW || a == AttackWord::MID || a == AttackWord::HIGH;
    }
    return false;
}

// FUN_80057AC4: attacks in reach whose level hits the opponent's current state.
gd::Int CpuOpponent::filter_punish_level(const gd::Ref<AiRecord>& rec) {
    return mark_candidates(rec, [=, this](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
        return is_attack(m) && reach_ok(rec, m) && level_vs_state(rec->opp_state, m->attack_word & 0xFFFF);
    });
}

// FUN_80057C88: as FUN_80057AC4 without the attack word 0x412.
gd::Int CpuOpponent::filter_punish_level_no_high(const gd::Ref<AiRecord>& rec) {
    return mark_candidates(rec, [=, this](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
        gd::Int a = m->attack_word & 0xFFFF;
        return a != AttackWord::HIGH && is_attack(m) && reach_ok(rec, m) && level_vs_state(rec->opp_state, a);
    });
}

// FUN_80057E48: attacks in reach from rows without command bits 5-13 (plain buttons).
gd::Int CpuOpponent::filter_plain_attacks(const gd::Ref<AiRecord>& rec) {
    return mark_candidates(rec, [=, this](const gd::Ref<MoveRow>& m, const gd::PI32& br) -> bool {
        return is_attack(m) && (br[ROW_COMMAND] & 0x3FE0) == 0 && damage_of(m) >= 0 && reach_ok(rec, m);
    });
}

// FUN_80058040: damaging attacks in reach faster than +0x20A (air ones only with state bit 19).
gd::Int CpuOpponent::filter_quick_attacks(const gd::Ref<AiRecord>& rec) {
    return mark_candidates(rec, [=, this](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
        gd::Int s = m->state;
        return (m->attack_word & HINT_NEVER) == 0 && damage_of(m) > 0 && m->active_first != 0
            && (m->air_first == 0 || (s & 0x80000) != 0) && m->active_first < rec->opp_until
            && (s & 0x20000) == 0 && reach_ok(rec, m);
    });
}

// FUN_8005817C: the same without the attack word 0x412.
gd::Int CpuOpponent::filter_quick_attacks_no_high(const gd::Ref<AiRecord>& rec) {
    return mark_candidates(rec, [=, this](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
        gd::Int w = m->attack_word;
        return (w & HINT_NEVER) == 0 && damage_of(m) > 0 && m->active_first != 0
            && m->active_first < rec->opp_until && (w & 0xFFFF) != AttackWord::HIGH
            && (m->state & StateBit::NOT_AI_ATTACK) == 0 && reach_ok(rec, m);
    });
}

gd::Int CpuOpponent::mark_command(const gd::Ref<AiRecord>& rec, gd::Int command) {
    return mark_candidates(rec, [=](const gd::Ref<MoveRow>& /*_m*/, const gd::PI32& br) -> bool {
        return br[ROW_COMMAND] == command;
    });
}

// FUN_80058AC0: side steps with probability 32/4096 at band 2 or 1024/4096 at band 3, then
// FUN_8005817C against a standing opponent (+0x230 bit 0), else FUN_80058040.
gd::Int CpuOpponent::filter_side_steps_then_quick(const gd::Ref<AiRecord>& rec) {
    gd::Int r = (lcg_next() >> 1) & 0xFFF;
    gd::Int count = 0;
    if ((rec->band == 2 && r < 0x20) || (rec->band == 3 && r < 0x400)) {
        count = mark_command(rec, SIDE_STEP);
    }
    if ((rec->opp_state & StateBit::CROUCHING) != 0) {
        return count + filter_quick_attacks_no_high(rec);
    }
    return count + filter_quick_attacks(rec);
}

bool CpuOpponent::is_neutral(const gd::Ref<MoveRow>& m) {
    return (m->state & StateBit::CROUCHING) == StateBit::CROUCHING && m->active_first == 0;
}

gd::Int CpuOpponent::mark_neutral(const gd::Ref<AiRecord>& rec) {
    return mark_candidates(rec, [=](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
        return is_neutral(m);
    });
}

// FUN_800582A0: guard and evasion candidates against the opponent's attack word (+0x234). The
// last block's count replaces the earlier ones (bug #21; RuleSet.fix_ai_guard_count adds it).
gd::Int CpuOpponent::filter_defence(const gd::Ref<AiRecord>& rec) {
    if (rec->distance > 0xB86) {
        return 0;
    }
    gd::Int attack = rec->opp_attack;
    if (rec->opp_grabs != 0) {
        gd::Int grabbed = attack != AttackWord::MID ? mark_neutral(rec) : 0;
        return grabbed + _crouch_dashes_if_closing(rec);
    }
    gd::Int count = 0;
    if (rec->opp_coming != 0) {
        switch (attack) {
            case AttackWord::MID_2:
                count = _mark_guard(rec, gd::Arr<gd::Int>{StateWord::GUARD_HIGH, StateWord::GUARD_LOW}, true);
                break;
            case AttackWord::MID:
                count = _mark_guard(rec, gd::Arr<gd::Int>{StateWord::GUARD_HIGH}, false);
                break;
            case AttackWord::LOW:
                count = mark_candidates(rec, [=](const gd::Ref<MoveRow>& m, const gd::PI32& /*_br*/) -> bool {
                    return (m->state & 0xFFFF) == StateWord::GUARD_LOW;
                });
                count += _crouch_dashes_if_closing(rec);
                break;
            case AttackWord::HIGH: {
                gd::Int posture = ai_self->state & 0xFFFF;
                if (posture == StateWord::CROUCH || posture == StateWord::GUARD_LOW) {
                    count = mark_neutral(rec);
                } else {
                    count = _mark_guard(rec, gd::Arr<gd::Int>{StateWord::GUARD_HIGH}, false);
                    gd::Int r = lcg_next() & 0xFFF;
                    if (r % 50 == 0) {
                        count += mark_neutral(rec);
                    }
                    count += _crouch_dashes_if_closing(rec);
                }
                break;
            }
            case AttackWord::UNBLOCKABLE_2:
                count = mark_neutral(rec);
                count += _crouch_dashes_if_closing(rec);
                break;
            default:
                break;
        }
    }
    if (rec->distance_change < -0x46 && rec->reach_band < 3) {
        gd::Int dashes = mark_command(rec, CROUCH_DASH);
        count = fight->rules->fix_ai_guard_count ? count + dashes : dashes;
    }
    return count;
}

gd::Int CpuOpponent::_mark_guard(const gd::Ref<AiRecord>& rec, const gd::Arr<gd::Int>& states, bool low_bit_clear) {
    return mark_candidates(rec, [=](const gd::Ref<MoveRow>& m, const gd::PI32& br) -> bool {
        if (br[ROW_COMMAND] == CROUCH_DASH || (low_bit_clear && (m->state & StateBit::HUMAN_GUARD) != 0)) {
            return false;
        }
        return states.has(m->state & 0xFFFF);
    });
}

gd::Int CpuOpponent::_crouch_dashes_if_closing(const gd::Ref<AiRecord>& rec) {
    if (rec->reach_band < 3) {
        return 0;
    }
    return mark_command(rec, CROUCH_DASH);
}

// FUN_800616B4: unmarks candidates with AI hint bit 0 or the low attack word 0x10F.
gd::Int CpuOpponent::filter_no_lows(const gd::Ref<AiRecord>& rec) {
    gd::Int removed = 0;
    for (gd::Int k = 0; k < rec->candidate_count; ++k) {
        gd::Int mark = cand_mark[k];
        if (mark >= 0) {
            gd::Int w = cand_target[k]->attack_word;
            if ((w & HINT_SAFE) != 0 || (w & 0xFFFF) == AttackWord::LOW) {
                if (mark > 0) {
                    removed += 1;
                }
                cand_mark[k] = -1;
            }
        }
    }
    rec->marked_count = Fx::s16(rec->marked_count - removed);
    return removed;
}

// FUN_80057300: a branch of the running move open now leads to a move the AI never chooses
// (such moves are reserved for the hooks).
bool CpuOpponent::hint_never_open(const gd::Ref<AiRecord>& rec) {
    gd::Ref<FighterState> f = ai_self;
    gd::Ref<FighterState> opp = ai_opp;
    for (const gd::PI32& row : list_rows(f->pose_move)) {
        if (row[ROW_FIRST] <= f->pose_frame && f->pose_frame <= row[ROW_LAST]
                && restriction_ok(f, opp, row[ROW_RESTRICTION]) && cond_ok(row, f, opp, rec->situation)) {
            gd::Ref<MoveRow> target = slot_move(rec->slot, row);
            if (target != nullptr && (target->attack_word & HINT_NEVER) != 0) {
                return true;
            }
        }
    }
    return false;
}

// ---- pad input

// AiPadFromStep (0x80061AC4): numpad direction in bits 0-3, buttons in bits 8-11.
gd::Int CpuOpponent::pad_from_step(gd::Int step) const {
    gd::Int pad = (step & 0x100) != 0 ? 0x80 : 0;
    if ((step & 0x200) != 0) {
        pad |= 0x10;
    }
    if ((step & 0x400) != 0) {
        pad |= 0x40;
    }
    if ((step & 0x800) != 0) {
        pad |= 0x20;
    }
    return pad | gd::Int(t->numpad_pads[step & 0xF]);
}

// AiStartScript (0x80061B24): presses the first step now and keeps the rest pending.
void CpuOpponent::start_script(const gd::Ref<AiRecord>& rec, const gd::PI32& script) const {
    rec->script_steps = script;
    rec->script_at = script.size() > 1 ? 1 : -1;
    rec->pad = pad_from_step(!script.is_empty() ? gd::Int(script[0]) : 0);
}

// FUN_8002CFD0: the steps of a motion command's input sequence (empty when it has none).
gd::PI32 CpuOpponent::input_sequence(gd::Int command) const {
    if (command < 0xC00E) {
        if (command == SIDE_STEP) {
            return t->tap_forward;
        }
        if (command == CROUCH_DASH) {
            return t->tap_back;
        }
        return gd::PI32();
    }
    if (command < 0xC7FF) {
        if (command - 0xC00E < 0x3F) {
            return fight->tables->sequences_a[command - 0xC00E].slice(1);
        }
        return gd::PI32();
    }
    if (command - 0xC7FF < 0x29) {
        return fight->tables->sequences_b[command - 0xC7FF].slice(1);
    }
    return gd::PI32();
}

// AiPressCommand (0x80061BA8): a branch command as pad input, remembering the target move
// (+0x5C). Returns the pad word, −1 for a motion script, 0 when the command has no script.
gd::Int CpuOpponent::press_command(const gd::Ref<AiRecord>& rec, gd::Int command, const gd::Ref<MoveRow>& target) const {
    command &= 0xFFFF;
    gd::Int result = 0;
    if ((command & 0xC000) == 0) {
        gd::Int d = 9;
        while (d > 0 && (gd::Int(t->direction_masks[d]) & (command >> 5) & 0x1FF) == 0) {
            d -= 1;
        }
        gd::Int buttons = 0;
        for (gd::Int k = 0; k < 4; ++k) {
            if ((gd::shr(command, k) & 1) != 0) {
                buttons |= t->button_pads[k];
            }
        }
        rec->pad = (gd::Int(t->direction_pads[d]) | buttons) & 0xFFFF;
        result = rec->pad;
    } else {
        gd::PI32 seq = input_sequence(command);
        if (seq.is_empty() || (command & 0xC000) != 0xC000) {
            return 0;
        }
        start_script(rec, seq);
        result = -1;
    }
    rec->pressed_move = target;
    return result;
}

// FUN_80056A30: restarts the approach timers.
void CpuOpponent::reset_approach(const gd::Ref<AiRecord>& rec) {
    rec->approach_hold = 0x28;
    rec->approach_kind = 0;
    rec->approach = 0;
    rec->direction_hold = -1;
}

// AiSideStep (0x80061D40). `side` −1 chooses from the heading difference (random when nearly
// straight or reversed) and the fighters' order on screen.
void CpuOpponent::side_step(const gd::Ref<AiRecord>& rec, gd::Int side) {
    if (side == -1) {
        gd::Int h = ai_self->opp_heading_delta & 0xFFFF;
        side = 0;
        if (((h - 0x101) & 0xFFFFFFFFLL) > 0x7EFE) {
            side = 1;
            if (((h + 0x7FFF) & 0xFFFF) > 0x7EFD) {
                side = (fight->rng->next() >> 4) & 1;
            }
        }
        if (Fx::s16(ai_opp->screen_x) < Fx::s16(ai_self->screen_x)) {
            side = 1 - side;
        }
    }
    start_script(rec, t->side_steps[side]);
    reset_approach(rec);
}

// ---- executing a candidate

// AiAfterAttack (0x80058C40): counts forced "never chosen" moves and, for an attack, sets the
// wait (+0x68) to word 2 plus a random one of words 3-6 and resets the approach timers.
void CpuOpponent::after_attack(const gd::Ref<AiRecord>& rec, const gd::PI32& row) {
    gd::Ref<MoveRow> target = slot_move(ai_self->ai_slot, row);
    if ((target->attack_word & HINT_NEVER) != 0) {
        rec->throw_steps = Fx::s16(rec->throw_steps + 1);
    }
    if (target->active_first == 0) {
        return;
    }
    gd::Int u = lcg_next() & 3;
    rec->wait = Fx::s16(rec->word(AiRecord::Param::WAIT) + rec->word(AiRecord::Param::WAIT_EXTRA + u));
    if (fight->mode == GameMode::FORCE) {
        // Tekken Force: the other enemy holds off, and this one pauses unless already pausing.
        gd::Int i = 2 - ai_self->index;
        force_timers[i] = t->force_words[rec->force_words];
        if (force_timers[2 + i] < 0) {
            force_timers[2 + i] = t->force_words[rec->force_words + 1];
        }
    }
    rec->neutral_cooldown = -1;
    if (rec->air_cooldown > 0) {
        rec->air_cooldown = -1;
    }
    rec->approach_kind = 0;
    rec->approach = 0;
    rec->approach_hold = 0x28;
    rec->direction_hold = -1;
}

// AiExecuteCandidate (0x80058D6C): picks the k-th marked candidate (k random below +0x72) and
// presses it. On odd frame counts the walk goes forwards and stops at k ≤ 0 (bug #19: the first
// marked candidate is twice as likely, the last never chosen; RuleSet.fix_ai_candidate_pick stops
// at k < 0 as backwards); backwards is uniform. Returns the table index, or −1.
gd::Int CpuOpponent::execute_candidate(const gd::Ref<AiRecord>& rec) {
    gd::Int m = rec->marked_count;
    if (m <= 0) {
        return -1;
    }
    gd::Int k = random_below(m);
    gd::Int pick = marked_pick(cand_mark, rec->candidate_count, k, (rec->frame_count & 1) != 0,
                               fight->rules->fix_ai_candidate_pick);
    if (pick < 0) {
        return -1;
    }
    press_command(rec, cand_row[pick][ROW_COMMAND], cand_target[pick]);
    after_attack(rec, cand_row[pick]);
    return pick;
}

// The k-th marked entry of the first `n` marks, walking forwards (stopping at k ≤ 0 as the game
// does, or at k < 0 when `fixed`) or backwards (k < 0); −1 without candidates.
gd::Int CpuOpponent::marked_pick(const gd::PI32& marks, gd::Int n, gd::Int k, bool forward, bool fixed) {
    if (forward) {
        if (n <= 0) {
            return -1;
        }
        gd::Int last = fixed ? -1 : 0;
        for (gd::Int i = 0; i < n; ++i) {
            if (marks[i] > 0) {
                k -= 1;
                if (k <= last) {
                    return i;
                }
            }
        }
        return n - 1;
    }
    gd::Int i = n - 1;
    while (i >= 0) {
        if (marks[i] > 0) {
            k -= 1;
            if (k < 0) {
                return i;
            }
        }
        i -= 1;
    }
    return n > 0 ? 0 : n;
}

// FUN_80058ED0: a branch of the running move to `slot` is open now or, failing that, opens on
// frame 1 of the default continuation, which is read from the row after the last walked row
// (bug #20: a common row when the list ends with a common block; RuleSet.fix_ai_continuation
// reads the terminator row, as AiCollectCandidates does).
bool CpuOpponent::branch_open_to(const gd::Ref<AiRecord>& rec, gd::Int slot) {
    gd::Ref<FighterState> f = ai_self;
    gd::Ref<FighterState> opp = ai_opp;
    slot &= 0xFFFF;
    WalkTo walked = _walk_to(rec, f->pose_move, slot, false, f, opp);
    if (walked.found) {
        return true;
    }
    gd::PI32 after = fight->rules->fix_ai_continuation ? walked.terminator : walked.after;
    gd::Ref<MoveRow> nxt = slot_move(f->ai_slot, after);
    if (nxt == nullptr || nxt == f->pose_move || !(after[ROW_FIRST] <= f->pose_frame && f->pose_frame <= after[ROW_LAST])) {
        return false;
    }
    return _walk_to(rec, nxt, slot, true, f, opp).found;
}

// Walks a branch list for an open row to `slot`: [found, the row after the last walked row, and
// when none is found the list's terminator row].
CpuOpponent::WalkTo CpuOpponent::_walk_to(const gd::Ref<AiRecord>& rec, const gd::Ref<MoveRow>& move, gd::Int slot,
                                          bool first_frame_one, const gd::Ref<FighterState>& f,
                                          const gd::Ref<FighterState>& opp) {
    gd::Arr<gd::PI32> list = move->bank->branches;
    gd::Int i = move->branches;
    gd::Arr<gd::PI32> after_list = list;
    gd::Int after_i = i;
    while (list[i][ROW_COMMAND] != END) {
        gd::PI32 r = list[i];
        gd::Arr<gd::PI32> base_list = list;
        gd::Int base = i;
        gd::Int n = 1;
        if (r[ROW_COMMAND] == COMMON) {
            base_list = fight->tables->common_branches;
            base = r[ROW_TARGET];
            n = r[ROW_ENTRY];
        }
        after_list = base_list;
        after_i = base;
        for (gd::Int k = 0; k < n; ++k) {
            gd::PI32 row = base_list[base + k];
            if ((row[ROW_TARGET] & 0xFFFF) == slot && !(first_frame_one && row[ROW_FIRST] != 1)
                    && row[ROW_FIRST] <= f->pose_frame && f->pose_frame <= row[ROW_LAST]
                    && restriction_ok(f, opp, row[ROW_RESTRICTION]) && cond_ok(row, f, opp, rec->situation)) {
                return WalkTo{true, after_list[after_i], list[i]};
            }
            after_i = base + k + 1;
        }
        i += 1;
    }
    return WalkTo{false, after_list[after_i], list[i]};
}

// ---- character hooks
// Hooks return a pad word to hold, −1 (nothing), −2 (input set, for example a script) or −3
// (execute a marked candidate).

// FUN_8005967C: FUN_800616B4 filters this frame's candidates.
void CpuOpponent::install_no_lows(const gd::Ref<AiRecord>& rec) {
    rec->filter_no_lows = true;
}

// AiCallHook (0x800594C4): with no script pending (+0x8A < 0) and a draw below word 29, runs the
// hook of the opponent's bank (`own` false) or the CPU's own. True when it produced input.
bool CpuOpponent::call_hook(const gd::Ref<AiRecord>& rec, bool own) {
    if (rec->move_block >= 0) {
        return false;
    }
    gd::Int x = lcg_next();
    if (rec->word(AiRecord::Param::HOOKS) <= (x & 0xFFF)) {
        return false;
    }
    gd::Int hook = own ? rec->hook_own : rec->hook_opponent;
    if (hook == AiTables::Hook::NONE) {
        return false;
    }
    gd::Int r = _run_hook(rec, hook);
    if (r == -2) {
        return true;
    }
    if (r == -3) {
        return rec->marked_count > 0 && execute_candidate(rec) >= 0;
    }
    if (r == -1) {
        return false;
    }
    if (r == PAD_FORWARD || r == PAD_BACK) {
        rec->approach = 0x10;
        rec->approach_kind = 0;
        rec->approach_hold = 0x38;
    }
    rec->pad = r & 0xFFFF;
    return true;
}

gd::Int CpuOpponent::_run_hook(const gd::Ref<AiRecord>& rec, gd::Int hook) {
    switch (hook) {
        case AiTables::Hook::VS_PAUL:
            return _hook_vs_paul(rec);
        case AiTables::Hook::AS_KING:
            return _hook_as_king(rec);
        case AiTables::Hook::VS_YOSHIMITSU:
            return _hook_vs_yoshimitsu(rec);
        case AiTables::Hook::AS_HWOARANG:
            return _hook_as_hwoarang(rec);
        case AiTables::Hook::VS_OGRE:
            return _hook_vs_ogre(rec);
        case AiTables::Hook::VS_GON:
            return _hook_vs_gon(rec);
        default:
            break;
    }
    return -1;
}

gd::Int CpuOpponent::_hook_vs_paul(const gd::Ref<AiRecord>& rec) {
    gd::Ref<FighterState> opp = ai_opp;
    if (Fx::s16(opp->cur_slot) == 0x2C7 && rec->reach_band < 3) {
        if (Fx::s16(opp->pose_frame) > 15) {
            return PAD_BACK;
        }
        rec->wait = -1;
    }
    return -1;
}

gd::Int CpuOpponent::_hook_vs_gon(const gd::Ref<AiRecord>& rec) {
    if (Fx::s16(ai_opp->cur_slot) != 0x137) {
        return -1;
    }
    if (rec->reach_band < 2) {
        rec->wait = -1;
        return -2;
    }
    return rec->reach_band < 4 ? -1 : PAD_BACK;
}

// Reacts to its own slot 0xDE3, which no bank defines (bug #11).
gd::Int CpuOpponent::_hook_as_hwoarang(const gd::Ref<AiRecord>& rec) {
    gd::Ref<FighterState> f = ai_self;
    if (Fx::s16(f->cur_slot) != BANK_HWOARANG_SLOT) {
        return -1;
    }
    if ((((f->pose_frame & 0xFFFF) - 0xD) & 0xFFFFFFFFLL) > 4 || rec->opp_until > 9) {
        return (fight->rng->next() & 0xF) == 0 ? -1 : PAD_FORWARD;
    }
    return -1;
}

gd::Int CpuOpponent::_hook_vs_yoshimitsu(const gd::Ref<AiRecord>& rec) {
    gd::Ref<FighterState> f = ai_self;
    gd::Ref<FighterState> opp = ai_opp;
    gd::Int frame = Fx::s16(opp->pose_frame);
    gd::Int move = Fx::s16(opp->cur_slot);
    gd::Int band = rec->band;
    gd::Int closing = rec->reach_band;
    gd::Ref<MoveRow> pm = opp->pose_move;
    if (move == 0x16F) {
        if (closing < 3) {
            rec->wait = -1;
        }
        return -1;
    }
    if (move == YOSHIMITSU_STANCE) {
        if (closing > 2) {
            return PAD_FORWARD;
        }
        rec->guard_suspend = 4;
        install_no_lows(rec);
        rec->wait = -1;
        return -1;
    }
    if ((((opp->cur_slot & 0xFFFF) - 0x459) & 0xFFFFFFFFLL) > 2) {
        if (move == 0x472) {
            return -1;
        }
        if (move != 0x16A && move != 0x43A) {
            if (!gd::Arr<gd::Int>{0x458, 0x49C, 0x49D, 0x49E}.has(move)) {
                return -1;
            }
            if (Fx::s16(f->opp_rel_angle) >= FightMath::QUARTER_TURN) {
                if (band < 2) {
                    rec->wait = -1;
                }
                return -1;
            }
            if (band < 2) {
                rec->wait = 1;
                return 0;
            }
            if (band < 3) {
                install_no_lows(rec);
                rec->wait = -1;
                return -1;
            }
            return PAD_FORWARD;
        }
        if (pm->active_first == 0 || frame <= pm->active_last) {
            if (Fx::s16(f->opp_rel_angle) > 0x1000) {
                return closing < 2 ? -1 : PAD_FORWARD;
            }
            if (closing > 2 && (rec->distance & 0xFFFFFFFFLL) < 0xA8D) {
                start_script(rec, t->back_dash);
                return -2;
            }
            if (closing < 2 && rec->opp_until > 10) {
                return -1;
            }
            if (closing < 3 && rec->opp_until > 10) {
                side_step(rec, fight->rng->next() & 1);
                return -2;
            }
        } else {
            rec->wait = -1;
        }
        return 0;
    }
    // The opponent's slots 0x459-0x45B.
    if (band < 1) {
        if (pm->active_first <= frame && frame <= pm->active_last) {
            return 0;
        }
        install_no_lows(rec);
        rec->wait = -1;
        rec->guard_suspend = 4;
        return -2;
    }
    gd::Int limit = 0;
    if ((((opp->rel_angle & 0xFFFF) - 0x2000) & 0xFFFFFFFFLL) < 0x4001) {
        rec->guard_suspend = 0x14;
        if (band < 2) {
            install_no_lows(rec);
            rec->wait = -1;
            return -1;
        }
        limit = 1;
    } else {
        if (band < 3) {
            if ((fight->rng->next() & 7) != 0) {
                return 0;
            }
            if ((rec->own_state & 0x400001) != 0) {
                return 0;
            }
            side_step(rec, -1);
            return -2;
        }
        rec->guard_suspend = 10;
        limit = 2;
    }
    return band > limit ? PAD_FORWARD : -1;
}

// Inlined in the hook against Ogre: a side from the heading unless the CPU's own move has state
// bit 22, the fighters are 7,001+ apart or the heading is ambiguous; side-steps at once.
gd::Int CpuOpponent::_ogre_side(const gd::Ref<AiRecord>& rec) {
    gd::Ref<FighterState> f = ai_self;
    if ((f->pose_move->state & 0x400000) != 0 || (rec->distance & 0xFFFFFFFFLL) >= 0x1B59) {
        return -1;
    }
    gd::Int h = f->opp_heading_delta & 0xFFFF;
    gd::Int side = 0;
    if (((h - 0x101) & 0xFFFFFFFFLL) < 0x7EFF) {
        side = 0;
    } else if (((h + 0x7FFF) & 0xFFFF) < 0x7EFE) {
        side = 1;
    } else {
        return -1;
    }
    if (Fx::s16(ai_opp->screen_x) < Fx::s16(f->screen_x)) {
        side = 1 - side;
    }
    side_step(rec, side);
    return side;
}

// Against Ogre's fire breath (slot 0x16E); the 0xDE4 branch is dead (bug #11).
gd::Int CpuOpponent::_hook_vs_ogre(const gd::Ref<AiRecord>& rec) {
    gd::Ref<FighterState> f = ai_self;
    gd::Ref<FighterState> opp = ai_opp;
    gd::Ref<MoveRow> pm = opp->pose_move;
    gd::Int frame = Fx::s16(opp->pose_frame);
    gd::Int band = rec->band;
    gd::Int move = Fx::s16(opp->cur_slot);
    if (move == 0xDE4) {
        rec->guard_suspend = 8;
        if (((frame - 9) & 0xFFFFFFFFLL) < 5 && band < 2 && (fight->rng->next() & 0x1F) == 0) {
            install_no_lows(rec);
            rec->wait = -1;
            return PAD_FORWARD;
        }
        if (frame < pm->active_first - 0x1E) {
            if (band <= 1) {
                rec->wait = -1;
                return PAD_FORWARD;
            }
            if (band < 3) {
                rec->wait = Fx::s16(rec->wait - 4);
                return PAD_FORWARD;
            }
            gd::Int side = _ogre_side(rec);
            return side == -1 ? PAD_FORWARD : _step_again(rec, side);
        }
        if (frame < pm->active_first - 0x14) {
            if (Fx::s16(f->rel_angle) > 0x37FF) {
                if (band < 3) {
                    return PAD_FORWARD;
                }
                gd::Int side2 = _ogre_side(rec);
                return side2 == -1 ? PAD_FORWARD : _step_again(rec, side2);
            }
            if (band < 3) {
                return 0;
            }
            if (band < 4) {
                return PAD_FORWARD;
            }
        } else if (frame < pm->active_last) {
            if (Fx::s16(f->rel_angle) > 0x5FFF) {
                rec->wait = 8;
                return PAD_BACK;
            }
            if (Fx::s16(f->rel_angle) > 0x37FF) {
                if (band > 1) {
                    return PAD_FORWARD;
                }
                rec->wait = 8;
                return 0;
            }
            gd::Int side3 = _ogre_side(rec);
            if (side3 != -1) {
                return _step_again(rec, side3);
            }
            return f->facing_quadrant == 0 ? PAD_FORWARD : 0;
        } else if (frame < pm->active_last + 0x28) {
            if (band < 2) {
                rec->wait = -1;
                return PAD_FORWARD;
            }
        } else if (band < 3) {
            rec->wait = -1;
            return PAD_FORWARD;
        }
    } else if (move == 0x16E) {
        rec->guard_suspend = 8;
        if (frame < pm->active_first - 8) {
            gd::Int side = -1;
            if (((((f->heading_delta & 0xFFFF) + 0x2000) & 0xFFFF) < 0x4001)
                    && (f->pose_move->state & 0x400000) == 0 && (rec->distance & 0xFFFFFFFFLL) < 0x1B59) {
                side = Fx::s16(f->screen_x) < Fx::s16(opp->screen_x) ? 1 : 0;
            }
            if (band < 1) {
                rec->wait = -1;
                return PAD_FORWARD;
            }
            if (band < 3) {
                return PAD_FORWARD;
            }
            if ((fight->rng->next() & 7) == 0 && f->facing_quadrant == 0 && side != -1) {
                return _step_again(rec, side);
            }
        } else {
            if (pm->active_last <= frame) {
                return -1;
            }
            if (f->facing_quadrant == 0) {
                return rec->distance < 0x1B59 ? PAD_DOWN_FORWARD : 0;
            }
            if (band < 2) {
                return t->ogre_escapes[fight->rng->next() & 3];
            }
            if (band < 3) {
                return PAD_DOWN_FORWARD;
            }
        }
    } else {
        return -1;
    }
    return (fight->rng->next() & 7) == 0 ? PAD_FORWARD : 0;
}

// A chosen side step is started again (the second call repeats the first).
gd::Int CpuOpponent::_step_again(const gd::Ref<AiRecord>& rec, gd::Int side) {
    side_step(rec, side);
    return -2;
}

// King's command throws (slot 0x15 open) and the scripts against crouching or downed opponents.
gd::Int CpuOpponent::_hook_as_king(const gd::Ref<AiRecord>& rec) {
    gd::Ref<FighterState> f = ai_self;
    gd::Ref<FighterState> opp = ai_opp;
    gd::Int u = lcg_next() & 0xFFF;
    if (rec->wait > 0 || u >= rec->word(AiRecord::Param::THROW_LIST)) {
        return -1;
    }
    gd::Int band = rec->band;
    gd::Ref<MoveRow> own = f->pose_move;
    if (opp->bank_type != BankType::GON && (rec->opp_state & StateBit::STANDING) != 0 && band < 3
            && opp->in_air == 0 && opp->about_to_hit == 0 && own->active_first == 0
            && rec->opp_until > 8 && f->facing_quadrant == 0 && f->opp_quadrant == 0
            && (fight->rng->next() & 7) == 0 && branch_open_to(rec, 0x15)) {
        start_script(rec, t->king_throws[(fight->rng->next() & 0xFFF) % 3]);
        rec->planned_on = 1;
        return -2;
    }
    gd::Arr<gd::PI32> scripts;
    if ((rec->opp_state & StateBit::CROUCHING) != 0 && band <= 1 && opp->about_to_hit == 0 && own->active_first == 0
            && rec->opp_until >= 9 && f->facing_quadrant == 0 && f->opp_quadrant == 0
            && hint_never_open(rec)) {
        scripts = t->king_vs_crouch;
    } else {
        if (rec->opp_down == 0 || band > 1 || !(f->opp_quadrant == 0 || f->opp_quadrant == 2)
                || f->facing_quadrant != 0 || opp->about_to_hit != 0 || own->active_first != 0
                || rec->opp_until < 9 || rec->vs_stance != 0 || !hint_never_open(rec)) {
            return -1;
        }
        scripts = t->king_ground;
    }
    start_script(rec, scripts[fight->rng->next() & 1]);
    return -2;
}

// ---- reset and parameters

// AiLoadParams (0x80061770).
void CpuOpponent::load_params(const gd::Ref<AiRecord>& rec, gd::Int group, gd::Int level, gd::Int disabled) {
    if (group < 1) {
        group = fight->ai_difficulty & 0xFFFF;
    }
    if (level < 1) {
        level = fight->ai_level & 0xFF;
    }
    rec->input_mode = disabled & 0xFFFF;
    if (fight->attract != 0) {
        level = 4;
    }
    rec->params = t->words(group, level).duplicate();
    rec->own_bands = t->band_thresholds[ai_self->char_id].duplicate();
    rec->opp_bands = t->band_thresholds[ai_opp->char_id].duplicate();
    rec->attack_limit = gd::maxi(rec->word(AiRecord::Param::ATTACKS_B), rec->word(AiRecord::Param::ATTACKS_A));
    if (fight->mode == GameMode::FORCE) {
        // Three words per level (up to 3), fifteen per group: the other enemy's hold after an
        // attack, this enemy's pause, and the chance of attacking while not the player's target.
        gd::Int g = gd::mini(group & 0xFFFFFFFFLL, 2);
        gd::Int l = gd::mini(gd::mini(level & 0xFFFFFFFFLL, 9), FORCE_LEVELS - 1);
        rec->force_words = FORCE_GROUP_WORDS * g + FORCE_LEVEL_WORDS * l;
    }
}

// AiReset (0x800596B0) for `f`'s record. The hooks, the band tables and the health come from
// the `ai_self` / `ai_opp` globals, not from `f`.
void CpuOpponent::reset(const gd::Ref<FighterState>& f, gd::Int group, gd::Int level, gd::Int disabled) {
    gd::Ref<AiRecord> rec = records[f->ai_slot];
    gd::Ref<FighterState> me = ai_self;
    gd::Ref<FighterState> opp = ai_opp;
    rec->pose_frame_seen = -1;
    rec->neutral_action = 0;
    rec->crouching = 0;
    rec->crouch_timer = -1;
    rec->crouch_attack_hold = 0;
    rec->move_block = -1;
    rec->direction_hold = -1;
    rec->approach = -1;
    rec->approach_kind = 0;
    rec->collected_move = nullptr;
    rec->throw_move = nullptr;
    rec->whiff_timer = -1;
    rec->guarding = 0;
    rec->air_cooldown = -1;
    rec->neutral_cooldown = -1;
    rec->move_frame_seen = -1;
    rec->move_frames = 0;
    rec->far_count = 0;
    rec->setup_cooldown = -1;
    rec->last_attack = nullptr;
    rec->guard_suspend = -1;
    rec->reload_timer = -1;
    rec->guard_delay = -1;
    rec->guard_holds = 0;
    rec->marked_count = 0;
    rec->candidate_count = 0;
    rec->strings_on = 0;
    rec->strings_count = 0;
    rec->health_seen = me->health;
    rec->no_strings = 0;
    rec->planned_on = 0;
    rec->planned_count = 0;
    rec->link_skip = 0;
    rec->links_on = 0;
    rec->links_count = 0;
    rec->attacks_in_row = 0;
    rec->throw_steps = 0;
    rec->throw_list = 0;
    rec->throw_press_frame = -1;
    rec->throw_row = gd::PI32();
    rec->cancel_wait = 0;
    rec->script_at = -1;
    rec->opp_string = -1;
    rec->history_at = 0;
    for (gd::Int k = 0; k < AiRecord::HISTORY; ++k) {
        rec->history[k] = nullptr;
    }
    rec->punish = 0;
    rec->parried = 0;
    rec->throw_chance = 0;
    rec->throw_next = 0;
    rec->evade_reason = 0;
    rec->guard_memory = 0;
    rec->evasion = 0;
    load_params(rec, group, level, disabled);
    rec->hook_opponent = t->hook(opp->bank_type, false);
    rec->hook_own = t->hook(me->bank_type, true);
    if (fight->attract != 0) {
        for (gd::Int w = AiRecord::Param::WAIT_EXTRA; w < AiRecord::Param::WAIT_EXTRA + 4; ++w) {
            rec->params[w] = Fx::div_trunc(rec->params[w], 2);
        }
        rec->params[AiRecord::Param::GUARD] = Fx::div_trunc(rec->params[AiRecord::Param::GUARD], 4);
    }
    rec->wait = rec->word(AiRecord::Param::WAIT_EXTRA);
    gd::Int r = fight->rng->next();
    gd::Int u = (r + lcg_next()) & 0x7FFF;
    rec->approach_hold = ((u * 30) >> 15) + 30;
    for (gd::Int k = 0; k < MAX_CANDIDATES; ++k) {
        cand_mark[k] = -1;
    }
}

// AiInitRound (0x8005993C): the AI slots and targets of the fighters, each record reset. Tekken
// Force gives its two enemy records the AI slots 0 and 1, both after the player.
void CpuOpponent::init_round() {
    multi = (fight->attract != 0 || fight->mode == GameMode::FORCE) ? 1 : 0;
    for (gd::Int k = 0; k < 4; ++k) {
        force_timers[k] = -1;
    }
    gd::Arr<gd::Int> slots{0, 1, -1};
    gd::Arr<gd::Int> targets{1, 0, -1};
    if (fight->mode == GameMode::FORCE) {
        if (fight->human_mask == 1) {
            slots = gd::Arr<gd::Int>{-1, 0, 1};
            targets = gd::Arr<gd::Int>{-1, 0, 0};
        } else if (fight->human_mask == 2) {
            slots = gd::Arr<gd::Int>{0, -1, 1};
            targets = gd::Arr<gd::Int>{1, -1, 1};
        } else {
            return;
        }
    }
    for (const gd::Ref<FighterState>& f : fight->fighters) {
        gd::Int i = f->index;
        f->ai_slot = slots[i];
        f->ai_target = targets[i];
        if (slots[i] < 0) {
            continue;
        }
        gd::Ref<AiRecord> rec = records[slots[i]];
        rec->frame_count = slots[i];
        rec->slot = slots[i];
        gd::Ref<FighterState> opp = fight->fighters[targets[i]];
        ai_opp = opp;
        rec->target = opp;
        set_ai_self(f);
        rec->prev_pad = 0;
        rec->pad = 0;
        reset(f, -1, -1, -1);
    }
}

}  // namespace t3
