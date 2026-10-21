// remake/content/fighter_tables.gd
#include "content/fighter_tables.h"

#include "content/json_file.h"

namespace t3 {

gd::Ref<FighterTables> FighterTables::load_from(const std::string& path) {
    gd::Json data = JsonFile::read(path);
    auto t = gd::make<FighterTables>();
    t->costume_keys = JsonFile::ints(data["costume_keys"]);
    t->costume_hand_shapes = JsonFile::ints(data["costume_hand_shapes"]);
    // `data.get(key, [])`: a missing key reads as null, whose items() are empty.
    for (const gd::Json& record : data.get("attachment_limits").items()) {
        t->attachment_limits.append(JsonFile::ints(record));
    }
    t->attachment_clamps = JsonFile::ints(data.get("attachment_clamps"));
    t->ratan = JsonFile::ints(data.get("ratan"));
    t->rsqrt = JsonFile::ints(data.get("rsqrt"));
    t->wing_open = JsonFile::ints(data.get("wing_open"));
    t->wing_beat = JsonFile::ints(data.get("wing_beat"));
    return t;
}

gd::Int FighterTables::key_of_slot(gd::Int slot) const {
    for (gd::Int key = 0; key < costume_keys.size(); ++key) {
        if (gd::Int(costume_keys[key]) == slot) {
            return key;
        }
    }
    return NO_KEY;
}

gd::Int FighterTables::hand_shape(gd::Int slot) const {
    return slot >= 0 && slot < costume_hand_shapes.size() ? gd::Int(costume_hand_shapes[slot]) : 0;
}

}  // namespace t3
