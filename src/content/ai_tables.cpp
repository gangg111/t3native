// remake/content/ai_tables.gd
#include "content/ai_tables.h"

#include "content/fight_tables.h"
#include "content/json_file.h"
#include "platform/log.h"

namespace t3 {

namespace {
// `str(v)` of a JSON value the converter writes as a string.
std::string jstr(const gd::Json& v) { return v.is_string() ? v.str() : std::string(); }
}  // namespace

gd::Ref<AiTables> AiTables::load_from(const std::string& path) {
    gd::Json d = JsonFile::read(path);
    auto t = gd::make<AiTables>();
    for (const gd::Json& group : d["params"].items()) {
        gd::Arr<gd::PI32> levels;
        for (const gd::Json& words : group.items()) {
            levels.append(JsonFile::ints(words));
        }
        t->params.append(levels);
    }
    t->band_thresholds = FightTables::_rows(d["band_thresholds"]);
    t->band_reach = JsonFile::ints(d["band_reach"]);
    t->approach_reach = JsonFile::ints(d["approach_reach"]);
    t->numpad_pads = JsonFile::ints(d["numpad_pads"]);
    t->direction_masks = JsonFile::ints(d["direction_masks"]);
    t->direction_pads = JsonFile::ints(d["direction_pads"]);
    t->button_pads = JsonFile::ints(d["button_pads"]);
    t->side_steps = FightTables::_rows(d["side_steps"]);
    t->tap_forward = JsonFile::ints(d["tap_forward"]);
    t->tap_back = JsonFile::ints(d["tap_back"]);
    t->back_dash = JsonFile::ints(d["back_dash"]);
    t->wake_up = JsonFile::ints(d["wake_up"]);
    t->king_throws = FightTables::_rows(d["king_throws"]);
    t->king_vs_crouch = FightTables::_rows(d["king_vs_crouch"]);
    t->king_ground = FightTables::_rows(d["king_ground"]);
    t->ogre_escapes = JsonFile::ints(d["ogre_escapes"]);
    t->actions = JsonFile::ints(d["actions"]);
    t->evasion_states = JsonFile::ints(d["evasion_states"]);
    for (const gd::Json& h : d["hooks"].items()) {
        t->hook_banks.append(JsonFile::number(h[0]));
        t->hooks_a.append(_hook_named(jstr(h[1])));
        t->hooks_b.append(_hook_named(jstr(h[2])));
    }
    for (const gd::Json& b : d["banks"].items()) {
        auto bank = gd::make<Bank>();
        bank->counter = JsonFile::number(b["counter"]);
        bank->parry_high = JsonFile::ints(b["parry_high"]);
        bank->parry_low = JsonFile::ints(b["parry_low"]);
        for (const gd::Json& s : b["setups"].items()) {
            auto setup = gd::make<Setup>();
            setup->chance = JsonFile::number(s[0]);
            setup->slot = JsonFile::number(s[1]);
            setup->steps = JsonFile::ints(s[2]);
            bank->setups.append(setup);
        }
        t->banks.append(bank);
    }
    t->force_words = JsonFile::ints(d["force_words"]);
    return t;
}

gd::PI32 AiTables::words(gd::Int group, gd::Int level) const {
    gd::Arr<gd::PI32> levels = params[gd::mini(group & 0xFFFFFFFFLL, GROUPS - 1)];
    return levels[gd::mini(level & 0xFFFFFFFFLL, LEVELS - 1)];
}

gd::Int AiTables::hook(gd::Int bank_type, bool own) const {
    for (gd::Int k = 0; k < hook_banks.size(); ++k) {
        if (gd::Int(hook_banks[k]) == bank_type) {
            return own ? gd::Int(hooks_b[k]) : gd::Int(hooks_a[k]);
        }
    }
    return NONE;
}

gd::Int AiTables::_hook_named(const std::string& hook_name) {
    if (!HOOK_NAMES.has(hook_name)) {
        Log::error("AiTables: unknown hook \"" + hook_name + "\" (reconvert the disc)");
        return NONE;
    }
    return HOOK_NAMES.at(hook_name);
}

gd::Ref<AiTables::Bank> AiTables::bank(gd::Int bank_type) const {
    return bank_type >= 0 && bank_type < banks.size() ? banks[bank_type] : gd::make<Bank>();
}

}  // namespace t3
