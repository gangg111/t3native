// remake/content/effect_data.gd
#include "content/effect_data.h"

#include "content/json_file.h"
#include "gd/io.h"

namespace t3 {

namespace {
gd::Json object_or_empty(const gd::Json& d, const std::string& key) {
    return d.has(key) ? d[key] : gd::Json::parse("{}");
}
// `str(v)` of a JSON value the converter writes as a string.
std::string jstr(const gd::Json& v) { return v.is_string() ? v.str() : std::string(); }
}  // namespace

gd::Ref<EffectData> EffectData::load_from(const std::string& dir) {
    gd::Json data = JsonFile::read(gd::path_join(dir, "effects.json"));
    auto e = gd::make<EffectData>();
    e->directory = dir;
    e->_costume_entries = object_or_empty(data, "costumes");
    e->objects = object_or_empty(data, "objects");
    for (const gd::Json& f : data.get("system").items()) {
        e->system.append(_flipbook(dir, f));
    }
    for (const gd::Json& f : data.get("enbu").items()) {
        e->character.append(_flipbook(dir, f));
    }
    gd::Json ring = object_or_empty(data, "dust_ring");
    for (const gd::Json& o : ring.get("offsets").items()) {
        gd::PI32 v = JsonFile::ints(o);
        e->dust_offsets.append(gd::Vector3i(v[0], v[1], v[2]));
    }
    e->dust_y = ring.has("y") ? ring["y"].to_int() : 0;
    e->dust_interval = ring.has("interval") ? ring["interval"].to_int() : 2;
    std::string flipbook = ring.has("flipbook") ? jstr(ring["flipbook"]) : "";
    for (const gd::Ref<Flipbook>& f : e->system) {
        if (f->name == flipbook) {
            e->dust = f;
        }
    }
    return e;
}

gd::Ref<EffectData::Flipbook> EffectData::costume(gd::Int slot) const {
    if (!_costumes.has(slot)) {
        const gd::Json& entry = _costume_entries.get(gd::str(slot));
        _costumes.set(slot, entry.is_object() ? _flipbook(directory, entry) : nullptr);
    }
    return _costumes.at(slot);
}

gd::Ref<EffectData::Flipbook> EffectData::_flipbook(const std::string& dir, const gd::Json& f) {
    auto b = gd::make<Flipbook>();
    b->name = jstr(f["name"]);
    b->path = gd::path_join(dir, jstr(f["file"]));
    b->frames = f["frames"].to_int();
    b->additive = f["blend"].is_string() && f["blend"].str() == "add";
    gd::PI32 light = f.has("light") ? JsonFile::ints(f["light"]) : gd::PI32{0, 0, 0};
    // Color8(r, g, b): channels / 255, alpha 1.
    b->light = {static_cast<float>(gd::Int(light[0])) / 255.0f, static_cast<float>(gd::Int(light[1])) / 255.0f,
                static_cast<float>(gd::Int(light[2])) / 255.0f, 1.0f};
    return b;
}

}  // namespace t3
