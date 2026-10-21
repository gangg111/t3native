#include "fight_compare.h"

#include <cctype>
#include <cstdio>

#include "content/move_row.h"
#include "core/ai/ai_record.h"
#include "core/ai/cpu_opponent.h"
#include "core/fighter/fighter_body.h"
#include "core/fighter/fighter_hands.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/joint_frame.h"
#include "core/math/fx.h"
#include "core/sim_events.h"
#include "field_value.h"

namespace t3test {

const FieldTable<t3::FighterState>& fighter_state_fields();
const FieldTable<t3::AiRecord>& ai_record_fields();

namespace {
const char* SKIPPED[] = {"reaction", "hitDone0", "hitDone1", "hitDone2", "hitDone3",
                         "screenXCopy", "setupFlag128B", "bestHitSlot"};

std::string show(const gd::Vector2i& v) { return "(" + gd::str(v.x) + ", " + gd::str(v.y) + ")"; }
std::string show(const gd::PI32& v) {
    std::string s = "[";
    for (gd::Int i = 0; i < v.size(); ++i) s += (i ? ", " : "") + gd::str(v[i]);
    return s + "]";
}
std::string hex8(gd::Int v) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%08llX", static_cast<unsigned long long>(v & 0xFFFFFFFFLL));
    return b;
}
std::string hex4(gd::Int v) {
    char b[8];
    std::snprintf(b, sizeof b, "%04llx", static_cast<unsigned long long>(v & 0xFFFF));
    return b;
}

template <typename C>
const std::function<FieldValue(const C&)>* getter(const FieldTable<C>& table, const std::string& name) {
    for (const auto& [n, fn] : table) if (n == name) return &fn;
    return nullptr;
}
}  // namespace

FighterCompare::FighterCompare(const RecordTrace& fight_trace) : trace(&fight_trace) {
    const FieldTable<t3::FighterState>& table = fighter_state_fields();
    for (const auto& [name, type] : trace->field_types) {
        bool skip = false;
        for (const char* s : SKIPPED) if (name == s) skip = true;
        if (skip) continue;
        std::string prop = _snake(name);
        size_t at = table.size();
        for (size_t k = 0; k < table.size(); ++k) if (table[k].first == prop) at = k;
        if (at == table.size()) {
            unmapped.push_back(name);
            continue;
        }
        fields.push_back({trace->field_offset(name), type, name, prop, at});
    }
}

// FighterCompare._snake: camelCase to snake_case, a '_' before an upper-case letter that follows
// a lower-case letter or a digit.
std::string FighterCompare::_snake(const std::string& name) {
    std::string out;
    for (size_t i = 0; i < name.size(); ++i) {
        char c = name[i];
        bool upper = std::isupper(static_cast<unsigned char>(c)) != 0;
        if (upper && i > 0) {
            char prev = name[i - 1];
            if (std::islower(static_cast<unsigned char>(prev)) || std::isdigit(static_cast<unsigned char>(prev))) out += '_';
        }
        out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

std::vector<std::string> FighterCompare::differences(const t3::FighterState& f, gd::Int frame, gd::Int index,
                                                     const gd::Arr<gd::Ref<t3::MotionBank>>& banks) const {
    std::vector<std::string> out;
    const FieldTable<t3::FighterState>& table = fighter_state_fields();
    for (const Field& d : fields) {
        FieldValue value = table[d.accessor].second(f);
        if (d.type == "void*") {
            if (value.kind != FieldValue::ROW) continue;
            gd::Int pointer = trace->fighter_u32(frame, index, d.offset);
            gd::Vector2i actual = _row_key(value.row, banks);
            if (actual.x == STALE_BANK) {
                // A row of a bank that is no longer loaded (Mokujin's previous round): the game
                // still points to where that row was.
                const gd::Ref<t3::MoveRow>& row = value.row;
                if (!trace->row_addresses(frame, row->bank->type_code, row->index).has(pointer))
                    out.push_back(d.name + ": stale row " + gd::str(row->index) + " of " + row->bank->name + " != " + hex8(pointer));
                continue;
            }
            gd::Vector2i expected = trace->row_of(frame, pointer);
            if (actual != expected) out.push_back(d.name + ": " + show(actual) + " != " + show(expected));
            continue;
        }
        if (value.kind != FieldValue::INT) continue;
        gd::Int size = RecordTrace::signed_size(d.type);
        gd::Int expected_v = trace->read(trace->record(frame, index) + d.offset, size);
        gd::Int actual_v = RecordTrace::width(value.i, size);
        if (actual_v != expected_v) out.push_back(d.name + ": " + gd::str(actual_v) + " != " + gd::str(expected_v));
    }
    const t3::FighterHands& h = *f.hands;
    struct Hand {
        const char* name;
        gd::PI32 actual;
        gd::Int at, size, count;
    };
    using t3::Fx::s16;
    const Hand hands[] = {
        {"hands current", gd::PI32{s16(h.current[0]), s16(h.current[1])}, 0x127C, -2, 2},
        {"hands target", gd::PI32{s16(h.target[0]), s16(h.target[1])}, 0x1280, -2, 2},
        {"hands rate", gd::PI32{s16(h.rate[0]), s16(h.rate[1])}, 0x1284, -2, 2},
        {"face speed", gd::PI32{s16(h.face_speed)}, 0x128E, -2, 1},
        {"face", gd::PI32{h.face & 0xFF}, 0x1290, 1, 1},
        {"ogre level", gd::PI32{s16(h.ogre_level), s16(h.ogre_target), s16(h.ogre_rate)}, 0x1296, -2, 3},
        {"wings", gd::PI32{h.wing_frame & 0xFF, h.wing_phase & 0xFF, h.wing_first & 0xFF}, 0x128B, 1, 3},
    };
    for (const Hand& e : hands) {
        gd::PI32 expected;
        for (gd::Int k = 0; k < e.count; ++k)
            expected.append(trace->read(trace->record(frame, index) + e.at + k * gd::absi(e.size), e.size));
        if (e.actual != expected) out.push_back(std::string(e.name) + ": " + show(e.actual) + " != " + show(expected));
    }
    if (f.body) {
        for (gd::Int k = 0; k < t3::FighterBody::JOINT_BLOCKS; ++k) {
            gd::Int at = JOINTS + k * JOINT_BYTES + JOINT_T;
            gd::PI32 expected_t{trace->fighter_s32(frame, index, at), trace->fighter_s32(frame, index, at + 4),
                                trace->fighter_s32(frame, index, at + 8)};
            if (f.body->joints[k]->t != expected_t)
                out.push_back("joint " + gd::str(k) + " t: " + show(f.body->joints[k]->t) + " != " + show(expected_t));
        }
    }
    return out;
}

gd::Vector2i FighterCompare::_row_key(const gd::Ref<t3::MoveRow>& row, const gd::Arr<gd::Ref<t3::MotionBank>>& banks) {
    if (!row) return gd::Vector2i(-1, -1);
    for (gd::Int b = 0; b < banks.size(); ++b)
        if (banks[b] == row->bank) return gd::Vector2i(b, row->index);
    return gd::Vector2i(STALE_BANK, row->index);
}

namespace {
// (offset, size (negative: signed)) of each AiCompare.FIELDS entry, in the order of the
// generated ai_record_fields() table (tools/gen_accessors.py --names from ai_compare.gd).
struct AiField {
    gd::Int offset, size;
    const char* prop;
};
const AiField AI_FIELDS[] = {
    {0x00, -2, "slot"}, {0x02, -2, "frame_count"}, {0x04, 2, "pad"}, {0x06, 2, "prev_pad"},
    {0x14, -4, "band"}, {0x18, -4, "reach_band"}, {0x1C, -4, "distance"}, {0x20, -4, "prev_distance"},
    {0x24, -4, "distance_change"}, {0x28, -2, "move_changed"}, {0x2A, -2, "far_count"},
    {0x2C, -2, "move_frames"}, {0x2E, -2, "angle"}, {0x30, -4, "health_seen"},
    {0x34, -2, "pose_frame_seen"}, {0x36, -2, "neutral_action"}, {0x38, -2, "approach"},
    {0x3A, -2, "approach_kind"}, {0x3C, -2, "crouching"}, {0x3E, -2, "guarding"},
    {0x40, -2, "approach_hold"}, {0x42, -2, "setup_cooldown"}, {0x44, -2, "neutral_cooldown"},
    {0x46, -2, "air_cooldown"}, {0x48, -2, "direction_hold"}, {0x4A, -2, "crouch_timer"},
    {0x4C, -2, "crouch_attack_hold"}, {0x4E, -2, "move_frame_seen"}, {0x50, -2, "attacking"},
    {0x52, -2, "special_open"}, {0x54, -2, "whiff_timer"}, {0x64, 4, "situation"}, {0x68, -2, "wait"},
    {0x6A, -2, "guard_holds"}, {0x6C, -2, "guard_delay"}, {0x6E, -2, "cancel_wait"},
    {0x70, -2, "candidate_count"}, {0x72, -2, "marked_count"}, {0x80, -4, "announced_frames"},
    {0x84, -4, "passed_frames"}, {0x88, -2, "vs_stance"}, {0x8A, -2, "move_block"},
    {0x8C, -2, "guard_suspend"}, {0x8E, 1, "parried"}, {0x8F, 1, "punish"}, {0x90, 1, "throw_next"},
    {0x91, 1, "throw_chance"}, {0x92, 1, "evade_reason"}, {0x93, 1, "guard_memory"}, {0x94, 1, "evasion"},
    {0x96, -2, "throw_steps"}, {0x98, -2, "throw_list"}, {0x9C, -2, "throw_press_frame"},
    {0xC0, -2, "strings_on"}, {0xC2, -2, "strings_count"}, {0xD4, -2, "no_strings"},
    {0xD8, -2, "planned_on"}, {0xDA, -2, "planned_count"}, {0x1A0, -2, "links_on"},
    {0x1A2, -2, "links_count"}, {0x1CC, -2, "link_skip"}, {0x1D0, -2, "link_last"},
    {0x1D2, -2, "attacks_in_row"}, {0x1D4, -2, "attack_limit"}, {0x1FC, 2, "history_at"},
    {0x1FE, -2, "reload_timer"}, {0x200, 1, "opp_breath"}, {0x201, 1, "opp_gon_special"},
    {0x202, -2, "adaptive"}, {0x204, -2, "opp_move_changed"}, {0x208, -2, "opp_down"},
    {0x20A, -2, "opp_until"}, {0x20C, -2, "opp_coming"}, {0x20E, -2, "opp_unblockable"},
    {0x210, -2, "opp_charging"}, {0x212, -2, "coming_seen"}, {0x214, -2, "opp_grabs"},
    {0x216, -2, "opp_string"}, {0x21A, 2, "opp_high"}, {0x21E, 2, "input_mode"},
    {0x220, 4, "own_word"}, {0x224, 4, "own_state"}, {0x228, 4, "own_attack"}, {0x22C, 4, "opp_word"},
    {0x230, 4, "opp_state"}, {0x234, 4, "opp_attack"},
};
const std::pair<gd::Int, const char*> AI_ROWS[] = {{0x58, "collected_move"}, {0x5C, "pressed_move"}, {0x60, "last_attack"}, {0xA0, "throw_move"}};
}  // namespace

std::vector<std::string> AiCompare::differences(const t3::CpuOpponent& ai, gd::Int slot, gd::Int frame,
                                                const gd::Arr<gd::Ref<t3::MotionBank>>& banks) const {
    std::vector<std::string> out;
    const t3::AiRecord& rec = *ai.records[slot];
    const FieldTable<t3::AiRecord>& table = ai_record_fields();
    gd::Int base = RECORDS + RECORD_SIZE * slot;
    std::string ai_s = "AI " + gd::str(slot) + " ";
    for (const AiField& d : AI_FIELDS) {
        gd::Int expected = trace->ram(frame, base + d.offset, d.size);
        const auto* get = getter(table, d.prop);
        FieldValue v = get ? (*get)(rec) : FieldValue{};
        gd::Int actual = RecordTrace::width(v.i, d.size);   // `rec.get(prop) as int`: a non-int reads as 0
        if (actual != expected) out.push_back(ai_s + d.prop + ": " + gd::str(actual) + " != " + gd::str(expected));
    }
    bool pending = trace->ram(frame, base + 0x08, 4) != 0;
    if (rec.script_pending() != pending)
        out.push_back(ai_s + "script pending: " + (rec.script_pending() ? "true" : "false") + " != " + (pending ? "true" : "false"));
    for (gd::Int k = 0; k < t3::AiRecord::PARAM_WORDS; ++k) {
        gd::Int expected = trace->ram(frame, base + 0x238 + 2 * k, -2);
        if (rec.params[k] != expected) out.push_back(ai_s + "word " + gd::str(k) + ": " + gd::str(rec.params[k]) + " != " + gd::str(expected));
    }
    for (gd::Int k = 0; k < 6; ++k) {
        gd::Int own = trace->ram(frame, base + 0x314 + 2 * k, -2);
        gd::Int opp = trace->ram(frame, base + 0x320 + 2 * k, -2);
        if (rec.own_bands[k] != own || rec.opp_bands[k] != opp)
            out.push_back(ai_s + "band threshold " + gd::str(k) + ": " + gd::str(rec.own_bands[k]) + "/" + gd::str(rec.opp_bands[k]) +
                          " != " + gd::str(own) + "/" + gd::str(opp));
    }
    for (const auto& [offset, prop] : AI_ROWS) {
        const auto* get = getter(table, prop);
        FieldValue v = get ? (*get)(rec) : FieldValue{};
        _row(out, v.row, trace->ram(frame, base + offset, 4), frame, banks, ai_s + prop);
    }
    for (gd::Int k = 0; k < t3::AiRecord::HISTORY; ++k)
        _row(out, rec.history[k], trace->ram(frame, base + 0x1DC + 4 * k, 4), frame, banks, ai_s + "history " + gd::str(k));
    if (slot == 0 && ai.multi != trace->ram(frame, MULTI, 4))
        out.push_back("AI multi: " + gd::str(ai.multi) + " != " + gd::str(trace->ram(frame, MULTI, 4)));
    return out;
}

void AiCompare::_row(std::vector<std::string>& out, const gd::Ref<t3::MoveRow>& row, gd::Int pointer, gd::Int frame,
                     const gd::Arr<gd::Ref<t3::MotionBank>>& banks, const std::string& name) const {
    gd::Vector2i actual = FighterCompare::_row_key(row, banks);
    gd::Vector2i expected = trace->row_of(frame, pointer);
    if (actual.x == FighterCompare::STALE_BANK) {
        if (!trace->row_addresses(frame, row->bank->type_code, row->index).has(pointer))
            out.push_back(name + ": stale row " + gd::str(row->index) + " of " + row->bank->name + " != " + hex8(pointer));
        return;
    }
    if (actual != expected) out.push_back(name + ": " + show(actual) + " != " + show(expected));
}

std::vector<std::string> FightCalls::of_trace(const FightTrace& trace, gd::Int frame) { return of_list(trace.calls(frame)); }

std::vector<std::string> FightCalls::of_list(const std::vector<Call>& calls) {
    std::vector<std::string> out;
    for (const Call& c : calls) {
        const std::string& kind = c.kind;
        auto a = [&](size_t i) { return i < c.args.size() ? c.args[i] : 0; };
        if (kind == "sound") out.push_back("sound " + gd::str(a(0)) + " " + hex4(a(1)) + " " + gd::str(a(2)));
        else if (kind == "sound_stop") out.push_back("sound_stop " + gd::str(a(0)) + " " + hex4(a(1)));
        else if (kind == "system_sound") out.push_back("system_sound " + gd::str(a(0) & 0xFFFF));
        else if (kind == "vibrate") out.push_back("vibrate " + gd::str(a(0)) + " " + gd::str(a(1)));
        else if (kind == "shake") out.push_back("shake " + gd::str(a(0)));
        else if (kind == "music") out.push_back("music " + gd::str(a(0)));
        else if (kind == "music_volume") out.push_back("music_volume " + gd::str(a(0)) + " " + gd::str(a(1)));
    }
    return out;
}

std::vector<std::string> FightCalls::of_events(const t3::SimEvents& events) {
    using K = t3::SimEvents;
    std::vector<std::string> out;
    for (const auto& e : events.items) {
        switch (e->kind) {
            case K::SOUND: out.push_back("sound " + gd::str(e->b) + " " + hex4(e->a) + " " + gd::str(e->c)); break;
            case K::SOUND_STOP: out.push_back("sound_stop " + gd::str(e->b) + " " + hex4(e->a)); break;
            case K::SYSTEM_SOUND: out.push_back("system_sound " + gd::str(e->a & 0xFFFF)); break;
            case K::VIBRATE: out.push_back("vibrate " + gd::str(e->a) + " " + gd::str(e->b)); break;
            case K::SHAKE: out.push_back("shake " + gd::str(e->a)); break;
            case K::MUSIC:
            case K::MUSIC_PREPARE: out.push_back("music " + gd::str(e->a)); break;
            case K::MUSIC_VOLUME: out.push_back("music_volume " + gd::str(e->a) + " " + gd::str(e->b)); break;
            default: break;
        }
    }
    return out;
}

}  // namespace t3test
