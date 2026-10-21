// remake/content/fight_tables.gd
#include "content/fight_tables.h"

#include "content/ai_tables.h"
#include "content/camera_tables.h"
#include "content/fighter_tables.h"
#include "content/json_file.h"
#include "content/pose_tables.h"
#include "gd/io.h"

namespace t3 {

FightTables::FightTables() {
    effects = gd::make<EffectTables>();
    vibration = gd::make<VibrationTables>();
}

gd::Ref<FightTables> FightTables::load_from(const std::string& folder) {
    auto t = gd::make<FightTables>();
    t->pose = PoseTables::load_from(gd::path_join(folder, "pose.json"));
    t->fighter = FighterTables::load_from(gd::path_join(folder, "fighter.json"));
    t->sin_table = t->pose->sin_table;
    gd::Json camera = JsonFile::read(gd::path_join(folder, "camera.json"));
    t->atan = JsonFile::ints(camera["atan"]);
    gd::Json d = JsonFile::read(gd::path_join(folder, "fight.json"));
    t->square_root = JsonFile::ints(d["square_root"]);
    t->common_branches = _rows(d["common_branches"]);
    t->sequences_a = _rows(d["sequences_a"]);
    t->sequences_b = _rows(d["sequences_b"]);
    for (const gd::Json& entry : d["builtin_attacks"].items()) {
        gd::PI32 pair = JsonFile::ints(entry);
        t->builtin_attack_ids.append(pair[0]);
        t->builtin_attack_offsets.append(pair[1]);
    }
    const gd::Json& blob = d["builtin_blob"];
    // PackedByteArray(Array): each float converted to int, stored as a byte.
    gd::PBytes bytes;
    for (const gd::Json& v : blob.items()) bytes.append(v.to_int());
    t->builtin_blob = bytes;
    t->air_kinds = _rows(d["air_kinds"]);
    t->push = JsonFile::ints(d["push"]);
    t->launch_push = JsonFile::ints(d["launch_push"]);
    t->reactions = _rows(d["reactions"]);
    t->forced_reaction = JsonFile::ints(d["forced_reaction"]);
    t->throw_victim_slots = JsonFile::ints(d["throw_victim_slots"]);
    t->close_reactions = _rows(d["close_reactions"]);
    t->face_attack = JsonFile::ints(d["face_attack"]);
    t->face_hit = JsonFile::ints(d["face_hit"]);
    t->hurt_zone_joints = JsonFile::ints(d["hurt_zone_joints"]);
    t->body_sphere_joints = JsonFile::ints(d["body_sphere_joints"]);
    t->hurt_radii = _rows(d["hurt_radii"]);
    t->body_radii = _rows(d["body_radii"]);
    t->camera = CameraTables::load_from(gd::path_join(folder, "camera.json"));
    t->health = JsonFile::ints(d["health"]);
    t->recovery_team = JsonFile::ints(d["recovery_team"]);
    t->recovery_survival = JsonFile::ints(d["recovery_survival"]);
    t->reversal_lists = _rows(d["reversal_lists"]);
    t->directions = _rows(d["directions"]);
    t->shake_scripts = _rows(d["shake_scripts"]);
    for (const gd::Json& c : d["characters"].items()) {
        t->characters.append(c);
    }
    t->sound_codes = JsonFile::ints(d["sound_codes"]);
    t->sound_scripts = _rows(d["sound_scripts"]);
    for (const gd::Json& v : d["voices"].items()) {
        auto s = gd::make<VoiceSet>();
        s->counts = JsonFile::ints(v["counts"]);
        s->voices = JsonFile::ints(v["voices"]);
        s->shouts = JsonFile::ints(v["shouts"]);
        s->damage_counts = JsonFile::ints(v["damage_counts"]);
        s->damage = JsonFile::ints(v["damage"]);
        s->cooldown = JsonFile::number(v["cooldown"]);
        s->ids = JsonFile::ints(v["ids"]);
        t->voices.append(s);
    }
    t->impact_voices = JsonFile::ints(d["impact_voices"]);
    t->round_voices = JsonFile::ints(d["round_voices"]);
    const gd::Json& vib = d["vibration"];
    t->vibration->slots = JsonFile::ints(vib["slots"]);
    t->vibration->pulses = JsonFile::ints(vib["pulses"]);
    for (const gd::Json& pattern : vib["patterns"].items()) {
        t->vibration->small.append(JsonFile::ints(pattern[0]));
        t->vibration->large.append(JsonFile::ints(pattern[1]));
    }
    t->result_voices = JsonFile::ints(d["result_voices"]);
    t->strike_sounds = JsonFile::ints(d["strike_sounds"]);
    t->special_sounds = JsonFile::ints(d["special_sounds"]);
    const gd::Json& e = d["effects"];
    for (const gd::Json& pair : e["breath_offsets"].items()) {
        t->effects->breath_offsets.append(_rows(pair));
    }
    t->effects->flame_offsets = _rows(e["flame_offsets"]);
    t->effects->cloud_offsets = _rows(e["cloud_offsets"]);
    t->effects->gas_offsets = _rows(e["gas_offsets"]);
    t->effects->burn_joints = JsonFile::ints(e["burn_joints"]);
    t->effects->gas_times = _rows(e["gas_times"]);
    t->effects->glow_joints = JsonFile::ints(e["glow_joints"]);
    for (const gd::Json& pair : e["hand_offsets"].items()) {
        t->effects->hand_offsets.append(_rows(pair));
    }
    const gd::Json& frames = e["segment_frames"];
    for (const auto& [name, value] : frames.members()) {
        t->effects->segment_frames.set(name, JsonFile::ints(value));
    }
    t->effects->mokujin_sounds = _rows(e["mokujin_sounds"]);
    t->ai = AiTables::load_from(gd::path_join(folder, "ai.json"));
    return t;
}

gd::Arr<gd::PI32> FightTables::_rows(const gd::Json& value) {
    gd::Arr<gd::PI32> out;
    for (const gd::Json& row : value.items()) {
        out.append(JsonFile::ints(row));
    }
    return out;
}

gd::Int FightTables::face_shape(const gd::PI32& table, gd::Int slot) const {
    return slot >= 0 && slot < table.size() ? gd::Int(table[slot]) : 0;
}

gd::PI32 FightTables::push_entry(gd::Int index) const {
    return push.slice(index, index + 10);
}

gd::Json FightTables::character(gd::Int key) const {
    return characters[gd::mini(key, characters.size() - 1)];
}

}  // namespace t3
