// remake/presentation/effects/effect_objects_view.gd
// Draws the effect object pool as the game does (see the header).
#include "presentation/effects/effect_objects_view.h"

#include "content/json_file.h"
#include "core/effects/effect_pool.h"
#include "gd/io.h"
#include "presentation/effects/effects_view.h"
#include "presentation/world_space.h"

namespace t3 {

const gd::Ref<Shader>& EffectObjectsView::ADD_SHADER() {
    static const gd::Ref<Shader> shader = Shader::load("res://presentation/effects/effect_object.gdshader");
    return shader;
}

const gd::Ref<Shader>& EffectObjectsView::HALF_SHADER() {
    static const gd::Ref<Shader> shader = Shader::load("res://presentation/effects/effect_object_half.gdshader");
    return shader;
}

EffectObjectsView::EffectObjectsView() {
    additive = gd::make<MultiMeshInstance3D>();
    half = gd::make<MultiMeshInstance3D>();
    ball_glow = gd::make<MeshInstance3D>();
    _ball_material = gd::make<StandardMaterial3D>();
}

void EffectObjectsView::setup(const gd::Ref<EffectData>& data) {
    const gd::Json& d = data->objects;
    double m = 1.0 / WorldSpace::UNITS_PER_METRE;
    const gd::Json& quads = d.get("quads");
    gd::Int n_quads = quads.is_array() ? quads.size() : 0;
    for (gd::Int qi = 0; qi < n_quads; ++qi) {
        const gd::Json& q = quads[qi];
        if (q.size() < 4) {
            quad_corners.append(gfx::Color(0, 0, 0, 1));   // model 0: none (show_pool skips it); Godot's Color()
            continue;
        }
        gd::PI32 a = JsonFile::ints(q[0]);
        gd::PI32 b = JsonFile::ints(q[3]);
        quad_corners.append(gfx::Color(static_cast<float>(gd::Int(a[0]) * m), static_cast<float>(gd::Int(a[1]) * m),
                                       static_cast<float>(gd::Int(b[0]) * m), static_cast<float>(gd::Int(b[1]) * m)));
    }
    const gd::Json& own = d.get("sprites");
    if (own.is_object()) {
        for (const auto& kv : own.members()) {
            sprites.set(static_cast<gd::Int>(std::stoll(kv.first)), JsonFile::ints(kv.second));   // String.to_int
        }
    }
    const gd::Json& tables = d.get("uv_tables");
    gd::Int n_tables = tables.is_array() ? tables.size() : 0;
    for (gd::Int ti = 0; ti < n_tables; ++ti) {
        const gd::Json& t = tables[ti];
        gd::Int size = JsonFile::number(t["size"]);
        gd::Arr<gd::PI32> frames;
        for (const gd::Json& f : t["frames"].items()) {
            gd::PI32 uv = JsonFile::ints(f);
            frames.append(gd::PI32{static_cast<int32_t>(gd::Int(uv[0])), static_cast<int32_t>(gd::Int(uv[1])),
                                   static_cast<int32_t>(size)});
        }
        uv_frames.append(frames);
    }
    ball_glow_frames = JsonFile::ints(d.get("ball_glow_frames"));
    const gd::Json& rows = d.get("clut_rows");
    gd::Int n_rows = rows.is_array() ? rows.size() : 0;
    for (gd::Int i = 0; i < n_rows; ++i) {
        clut_rows.set(JsonFile::number(rows[i]), i);
    }
    gfx::TextureRef page = nullptr;
    gfx::TextureRef palettes = nullptr;
    if (d.has("page")) {
        // load(...) as Texture2D: the converted pictures as they are (no texture pack).
        page = gfx::load_texture(gd::path_join(data->directory, d["page"].str()));
        palettes = gfx::load_texture(gd::path_join(data->directory, d["palettes"].str()));
    }
    _prepare(additive, ADD_SHADER(), page, palettes);
    _prepare(half, HALF_SHADER(), page, palettes);
    gd::Ref<QuadMesh> quad = gd::make<QuadMesh>();
    ball_glow->mesh = quad;
    _ball_material->shading_mode = StandardMaterial3D::SHADING_MODE_UNSHADED;
    _ball_material->blend_mode = StandardMaterial3D::BLEND_MODE_ADD;
    _ball_material->billboard_mode = StandardMaterial3D::BILLBOARD_ENABLED;
    _ball_material->transparency = StandardMaterial3D::TRANSPARENCY_ALPHA;
    _ball_material->no_depth_test = false;
    _ball_material->texture_filter = StandardMaterial3D::TEXTURE_FILTER_LINEAR;
    ball_glow->material_override = _ball_material;
    ball_glow->cast_shadow = GeometryInstance3D::SHADOW_CASTING_SETTING_OFF;
    ball_glow->visible = false;
    add_child(ball_glow);
}

void EffectObjectsView::_prepare(const gd::Ref<MultiMeshInstance3D>& node, const gd::Ref<Shader>& shader,
                                 const gfx::TextureRef& page, const gfx::TextureRef& palettes) {
    gd::Ref<MultiMesh> mm = gd::make<MultiMesh>();
    mm->transform_format = MultiMesh::TRANSFORM_3D;
    mm->use_colors = true;
    mm->use_custom_data = true;
    mm->mesh = gd::make<QuadMesh>();
    mm->set_instance_count(static_cast<int>(MAX_OBJECTS));
    mm->visible_instance_count = 0;
    node->multimesh = mm;
    gd::Ref<ShaderMaterial> material = gd::make<ShaderMaterial>();
    material->shader = shader;
    material->set_shader_parameter("page", page);
    material->set_shader_parameter("palettes", palettes);
    if (shader == ADD_SHADER()) {
        material->set_shader_parameter("hdr_boost", static_cast<float>(EffectsView::HDR_BOOST));
    }
    node->material_override = material;
    node->cast_shadow = GeometryInstance3D::SHADOW_CASTING_SETTING_OFF;
    // The instances are placed in view space by the shader: the node's bounds cannot be known.
    node->custom_aabb = AABB(Vector3(-1000, -1000, -1000), Vector3(2000, 2000, 2000));
    add_child(node);
}

// The players' character flipbooks (the charged ball's glow).
void EffectObjectsView::set_costumes(const gd::Arr<gd::Ref<EffectData::Flipbook>>& books) {
    costumes = books;
}

void EffectObjectsView::clear() {
    additive->multimesh->visible_instance_count = 0;
    half->multimesh->visible_instance_count = 0;
    ball_glow->visible = false;
}

// The sprite of an object this frame: (u0, v0, frame size) on the object page, or empty.
gd::PI32 EffectObjectsView::sprite_of(const gd::Ref<EffectObjects::Obj>& o) {
    if (o->uv_table == EffectObjects::UvTable::MODEL || o->uv < 0) {
        gd::PI32 own = sprites.get(o->model, gd::PI32());
        if (own.is_empty()) {
            // An animated model before its first frame: the table's first frame.
            if (o->uv_table == EffectObjects::UvTable::MODEL) {
                return gd::PI32();
            }
            return _table_frame(o->uv_table, 0);
        }
        return own;
    }
    return _table_frame(o->uv_table, o->uv);
}

gd::PI32 EffectObjectsView::_table_frame(gd::Int table, gd::Int frame) {
    gd::Arr<gd::PI32> frames = uv_frames[table - 1];
    return frames[gd::clampi(frame, 0, frames.size() - 1)];
}

// The pool after a simulation step.
void EffectObjectsView::show_pool(const gd::Ref<EffectPool>& pool) {
    gd::Int counts[2] = {0, 0};
    ball_glow->visible = false;
    for (const gd::Ref<EffectObjects::Obj>& o : pool->objects) {
        if (o->gone || (o->flags & EffectObjects::VISIBLE) == 0 || o->model <= 0 || o->model >= quad_corners.size()) {
            continue;
        }
        if (o->uv_table == EffectObjects::UvTable::BALL_CHARACTER) {
            _show_ball_glow(o);
            continue;
        }
        gd::PI32 s = sprite_of(o);
        if (s.is_empty() || !clut_rows.has(o->clut)) {
            continue;
        }
        gd::Ref<MultiMeshInstance3D> target = o->tpage == EffectObjects::TPAGE_HALF ? half : additive;
        gd::Int k = target == half ? 1 : 0;
        gd::Int i = counts[k];
        if (i >= MAX_OBJECTS) {
            continue;
        }
        counts[k] = i + 1;
        gd::Ref<MultiMesh> mm = target->multimesh;
        mm->set_instance_transform(static_cast<int>(i),
                                   Transform3D(Basis(), WorldSpace::point(static_cast<double>(o->x), static_cast<double>(o->y),
                                                                          static_cast<double>(o->z))));
        gd::Int row = clut_rows.at(o->clut);
        mm->set_instance_color(static_cast<int>(i), gfx::Color(static_cast<float>(gd::Int(s[0])), static_cast<float>(gd::Int(s[1])),
                                                               static_cast<float>(gd::Int(s[2])), static_cast<float>(row)));
        mm->set_instance_custom_data(static_cast<int>(i), corners(o->model));
    }
    additive->multimesh->visible_instance_count = static_cast<int>(counts[0]);
    half->multimesh->visible_instance_count = static_cast<int>(counts[1]);
}

// A model's corners 0 and 3 as (x0, y0, x1, y1) in metres.
gfx::Color EffectObjectsView::corners(gd::Int model) {
    return quad_corners[model];
}

// The charged ball's glow on the ball's quad, additively.
void EffectObjectsView::_show_ball_glow(const gd::Ref<EffectObjects::Obj>& o) {
    gd::Int player = gd::clampi(o->a, 0, 1);
    if (player >= costumes.size() || costumes[player] == nullptr) {
        return;
    }
    gd::Ref<EffectData::Flipbook> book = costumes[player];
    gd::Int frame = (o->uv >= 0 && o->uv < ball_glow_frames.size()) ? gd::Int(ball_glow_frames[o->uv]) : -1;
    if (frame < 0 || frame >= book->frames) {
        return;
    }
    gfx::Color c = corners(o->model);
    gd::Ref<QuadMesh> quad = std::dynamic_pointer_cast<QuadMesh>(ball_glow->mesh);
    quad->set_size(gfx::Vec2(c.b - c.r, c.a - c.g));
    _ball_material->albedo_texture = EffectsView::flipbook_texture(book);
    _ball_material->uv1_scale = Vector3(static_cast<float>(1.0 / static_cast<double>(book->frames)), 1, 1);
    _ball_material->uv1_offset = Vector3(static_cast<float>(static_cast<double>(frame) / static_cast<double>(book->frames)), 0, 0);
    ball_glow->set_position(WorldSpace::point(static_cast<double>(o->x), static_cast<double>(o->y), static_cast<double>(o->z)));
    ball_glow->visible = true;
}

}  // namespace t3
