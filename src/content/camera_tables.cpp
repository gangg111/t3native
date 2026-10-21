// remake/content/camera_tables.gd
#include "content/camera_tables.h"

#include "content/json_file.h"

namespace t3 {

gd::Ref<CameraTables> CameraTables::load_from(const std::string& path) {
    gd::Json data = JsonFile::read(path);
    auto t = gd::make<CameraTables>();
    t->atan_table = JsonFile::ints(data["atan"]);
    // `data.get(key, [])`: a missing key reads as null, whose items() are empty.
    for (const gd::Json& row : data.get("presets").items()) {
        t->presets.append(JsonFile::ints(row));
    }
    t->preset_index = JsonFile::ints(data.get("preset_index"));
    t->outside_blend_out = JsonFile::number(data.get("outside_blend_out"));   // default 0: number(null) is 0
    for (const gd::Json& row : data.get("choices").items()) {
        t->choices.append(JsonFile::ints(row));
    }
    for (const gd::Json& row : data.get("hit_rows").items()) {
        t->hit_rows.append(JsonFile::ints(row));
    }
    for (const gd::Json& records : data.get("hit_lists").items()) {
        gd::Arr<gd::PI32> list;
        for (const gd::Json& record : records.items()) {
            list.append(JsonFile::ints(record));
        }
        t->hit_lists.append(list);
    }
    for (const gd::Json& words : data.get("hit_scripts").items()) {
        gd::PI32 w = JsonFile::ints(words);
        auto s = gd::make<HitScript>();
        s->kind = w[0];
        s->frames = w[1];
        s->blend_in = w[2];
        s->blend_out = w[3];
        s->start_distance = w[4];
        s->end_distance = w[5];
        s->pitches = w.slice(6, 10);
        s->yaws = w.slice(10, 14);
        t->hit_scripts.append(s);
    }
    for (const gd::Json& row : data.get("intro_offsets").items()) {
        t->intro_offsets.append(JsonFile::ints(row));
    }
    t->intro_offset_ball = data.has("intro_offset_ball") ? JsonFile::ints(data["intro_offset_ball"]) : gd::PI32{0, 0, 0};
    t->attract_styles = JsonFile::ints(data.get("attract_styles"));
    t->attract_pitches = JsonFile::ints(data.get("attract_pitches"));
    t->attract_speeds = JsonFile::ints(data.get("attract_speeds"));
    t->winner_lateral = JsonFile::ints(data.get("winner_lateral"));
    t->winner_forward = JsonFile::ints(data.get("winner_forward"));
    t->winner_height = JsonFile::ints(data.get("winner_height"));
    return t;
}

}  // namespace t3
