// remake/content/character_model.gd
#include "content/character_model.h"

#include <cstring>

#include "content/json_file.h"
#include "core/math/fx.h"

namespace t3 {

namespace {
// Typed reads of `data.get(key, fallback)` (Variant float -> int truncates).
gd::Int int_or(const gd::Json& d, const std::string& key, gd::Int fallback) {
    return d.has(key) ? d[key].to_int() : fallback;
}
std::string str_or(const gd::Json& d, const std::string& key, const std::string& fallback) {
    return d.has(key) && d[key].is_string() ? d[key].str() : fallback;
}
double float_or(const gd::Json& d, const std::string& key, double fallback) {
    return d.has(key) ? d[key].num() : fallback;
}
// `var b: bool = value`.
bool to_bool(const gd::Json& v) { return v.is_bool() ? v.boolean() : v.num() != 0.0; }
// `{}` when the key is missing.
gd::Json object_or_empty(const gd::Json& d, const std::string& key) {
    return d.has(key) ? d[key] : gd::Json::parse("{}");
}
// PackedByteArray.to_float32_array().
gd::Packed<float> to_float32_array(const gd::PBytes& b) {
    gd::Packed<float> out;
    out.resize(b.size() / 4);
    if (out.size() > 0) std::memcpy(out.data(), b.data(), static_cast<size_t>(out.size()) * 4);
    return out;
}
// PackedByteArray.to_int32_array().
gd::PI32 to_int32_array(const gd::PBytes& b) {
    gd::PI32 out;
    out.resize(b.size() / 4);
    for (gd::Int i = 0; i < out.size(); ++i) out[i] = b.decode_s32(4 * i);
    return out;
}
// A float element of a PackedFloat32Array (read and written as float, not through gd::Int).
float& fat(const gd::Packed<float>& a, gd::Int i) { return a.vec()[gd::index(i, a.vec().size())]; }
}  // namespace

gd::Ref<CharacterModel> CharacterModel::load_from(const std::string& dir) {
    gd::Json data = JsonFile::read(dir + "/model.json");
    gd::PBytes mesh = DataFile::read(dir + "/mesh.bin.gz");
    auto m = gd::make<CharacterModel>();
    m->directory = dir;
    m->name = str_or(data, "name", "");
    m->arcade = str_or(data, "source", "") == "arcade";
    m->costume_slot = int_or(data, "costume_slot", 0);
    m->scale_percent = int_or(data, "scale_percent", 100);
    m->vertex_floats = int_or(data, "vertex_floats", 0);
    m->smooth_crease_cos = float_or(data, "smooth_crease_cos", 1.0);
    m->texture = str_or(data, "texture", "");
    for (const gd::Json& p : data.get("parts").items()) {
        auto part = gd::make<Part>();
        part->row = p["row"].to_int();
        part->parent = p["parent"].to_int();
        gd::PI32 o = JsonFile::ints(p["offset"]);
        part->offset = gd::Vector3i(o[0], o[1], o[2]);
        part->joint = p["joint"].to_int();
        part->drawn = to_bool(p["drawn"]);
        part->rest = JsonFile::ints(p.get("rest"));
        const gd::Json& dynamics = p.get("dynamics");
        if (!dynamics.is_null()) {
            part->dynamics = JsonFile::ints(dynamics);
        }
        m->parts.append(part);
    }
    for (const gd::Json& a : data.get("arcade_attachments").items()) {
        auto attachment = gd::make<ArcadeAttachment>();
        attachment->joint = a["joint"].to_int();
        attachment->parent = a["parent"].to_int();
        gd::PI32 ao = JsonFile::ints(a["offset"]);
        attachment->offset = gd::Vector3i(ao[0], ao[1], ao[2]);
        attachment->rest = JsonFile::ints(a["rest"]);
        attachment->static_record = to_bool(a["static"]);
        attachment->record = JsonFile::ints(a["record"]);
        m->arcade_attachments.append(attachment);
    }
    if (data.has("texture_closed")) {
        m->eye_textures.set(CLOSED_SHAPE, data["texture_closed"].str());
    }
    gd::Json swap = object_or_empty(data, "palette_swap");
    if (swap.size() != 0) {
        m->swap_texture = swap["texture"].str();
        m->swap_variants = JsonFile::ints(swap["variants"]);
    }
    m->gaze = object_or_empty(data, "gaze");
    const gd::Json& shapes = data.get("eye_textures");
    for (const auto& [shape, value] : shapes.members()) {
        m->eye_textures.set(std::stoll(shape), value.str());
    }
    gd::Json eyes = object_or_empty(data, "arcade_eyes");
    if (eyes.size() != 0) {
        m->arcade_eyes = gd::make<ArcadeEyes>();
        m->arcade_eyes->blink = eyes["blink"].to_int();
        m->arcade_eyes->held = eyes["held"].to_int();
        m->arcade_eyes->down = eyes["down"].to_int();
        m->arcade_eyes->shout = eyes["shout"].to_int();
        m->arcade_eyes->shout_frames = JsonFile::ints(eyes["shout_frames"]);
    }
    const gd::Json& surfaces = data.get("surfaces");
    m->surfaces = _surfaces(surfaces, mesh, m->vertex_floats);
    m->hands = _groups(data.get("hands"), mesh, m->vertex_floats);
    m->wings = _groups(data.get("wings"), mesh, m->vertex_floats);
    return m;
}

gd::Arr<gd::Ref<CharacterModel::VariantGroup>> CharacterModel::variant_groups() const {
    gd::Arr<gd::Ref<VariantGroup>> groups;
    groups.append_array(hands);
    groups.append_array(wings);
    return groups;
}

gd::Arr<gd::Ref<CharacterModel::VariantGroup>> CharacterModel::_groups(const gd::Json& list, const gd::PBytes& mesh,
                                                                     gd::Int floats) {
    gd::Arr<gd::Ref<VariantGroup>> out;
    for (const gd::Json& g : list.items()) {
        auto group = gd::make<VariantGroup>();
        group->channel = g["channel"].to_int();
        group->part = g["part"].to_int();
        if (g.has("poses")) {
            gd::Arr<gd::Ref<Surface>> base = _surfaces(g["variants"][0], mesh, floats);
            group->variants = _posed(base, g["poses"], mesh, floats);
            group->select = JsonFile::ints(g["select"]);
        } else {
            for (const gd::Json& variant : g["variants"].items()) {
                group->variants.append(_surfaces(variant, mesh, floats));
            }
        }
        out.append(group);
    }
    return out;
}

gd::Arr<gd::Arr<gd::Ref<CharacterModel::Surface>>> CharacterModel::_posed(const gd::Arr<gd::Ref<Surface>>& base,
                                                                         const gd::Json& poses, const gd::PBytes& mesh,
                                                                         gd::Int floats) {
    gd::Int count = poses["count"].to_int();
    gd::Int vertices = poses["vertices"].to_int();
    gd::Int table_start = poses["table"].to_int();
    gd::Packed<float> table = to_float32_array(mesh.slice(table_start, table_start + count * vertices * POSITION_BYTES));
    gd::Int corners = 0;
    for (const gd::Ref<Surface>& surface : base) {
        corners += gd::div(surface->vertices.size(), floats);
    }
    gd::Int refs_start = poses["refs"].to_int();
    gd::PI32 refs = to_int32_array(mesh.slice(refs_start, refs_start + corners * REF_BYTES));
    // The positions a pose moves, found once: per surface the float each one is at and its vertex.
    gd::Arr<gd::PI32> moving;
    gd::Int corner = 0;
    for (const gd::Ref<Surface>& source : base) {
        gd::PI32 found;
        gd::Int n = gd::div(source->vertices.size(), floats);
        for (gd::Int i = 0; i < n; ++i) {
            for (gd::Int k = 0; k < POSITION_FLOATS.size(); ++k) {
                gd::Int ref = refs[(corner + i) * 3 + k];
                if (ref >= 0) {
                    found.append(i * floats + POSITION_FLOATS[k]);
                    found.append(ref);
                }
            }
        }
        corner += gd::div(source->vertices.size(), floats);
        moving.append(found);
    }
    gd::Arr<gd::Arr<gd::Ref<Surface>>> out;
    for (gd::Int pose = 0; pose < count; ++pose) {
        gd::Arr<gd::Ref<Surface>> surfaces;
        for (gd::Int s = 0; s < base.size(); ++s) {
            auto surface = gd::make<Surface>();
            surface->double_sided = base[s]->double_sided;
            surface->vertices = base[s]->vertices.duplicate();
            gd::PI32 found = moving[s];
            for (gd::Int j = 0; j < found.size(); j += 2) {
                gd::Int from = (pose * vertices + found[j + 1]) * 3;
                gd::Int at = found[j];
                fat(surface->vertices, at) = fat(table, from);
                fat(surface->vertices, at + 1) = fat(table, from + 1);
                fat(surface->vertices, at + 2) = fat(table, from + 2);
            }
            surfaces.append(surface);
        }
        out.append(surfaces);
    }
    return out;
}

gd::Arr<gd::Ref<CharacterModel::Surface>> CharacterModel::_surfaces(const gd::Json& list, const gd::PBytes& mesh,
                                                                   gd::Int floats) {
    gd::Arr<gd::Ref<Surface>> out;
    for (const gd::Json& s : list.items()) {
        auto surface = gd::make<Surface>();
        surface->double_sided = to_bool(s["double_sided"]);
        gd::Int start = s["offset"].to_int();
        gd::Int count = s["vertex_count"].to_int();
        surface->vertices = to_float32_array(mesh.slice(start, start + count * floats * 4));
        out.append(surface);
    }
    return out;
}

gd::Int CharacterModel::scale_fixed() const {
    return scale_percent * Fx::ONE / 100;
}

gd::Arr<gd::PI32> CharacterModel::joint_offsets() const {
    gd::Arr<gd::PI32> out;
    out.resize(PART_COUNT);
    for (const gd::Ref<Part>& part : parts) {
        out[part->joint] = gd::PI32{gd::Int(part->offset.x), gd::Int(part->offset.y), -gd::Int(part->offset.z)};
    }
    return out;
}

gd::PI32 CharacterModel::joint_parents() const {
    gd::PI32 out;
    out.resize(PART_COUNT);
    for (const gd::Ref<Part>& part : parts) {
        out[part->joint] = part->parent >= 0 ? parts[part->parent]->joint : -1;
    }
    return out;
}

}  // namespace t3
