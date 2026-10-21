// remake/presentation/fighter/fighter_view.gd
#include "presentation/fighter/fighter_view.h"

#include "content/texture_packs.h"
#include "core/fighter/fighter_hands.h"
#include "core/fighter/joint_frame.h"
#include "gd/io.h"
#include "gfx/image.h"
#include "presentation/fighter/arcade_attachments.h"
#include "presentation/fighter/arcade_face.h"
#include "presentation/fighter/arcade_poses.h"
#include "presentation/fighter/curved_mesh.h"
#include "presentation/render_quality.h"
#include "presentation/stage/stage_lighting.h"
#include "platform/settings.h"
#include "presentation/world_space.h"

namespace t3 {

namespace {

constexpr uint32_t STAGE_LIGHTING_FIGHTER_LAYER = static_cast<uint32_t>(StageLighting::FIGHTER_LAYER);
const gd::Arr<std::string>& RENDER_QUALITY_TEXTURE_SETTINGS = RenderQuality::TEXTURE_SETTINGS;
gd::Ref<Shader> render_quality_shader(const gd::Ref<Shader>& base) { return RenderQuality::shader(base); }

// The picture of TexturePacks.texture(path) (Texture2D.get_image(): gfx::Texture keeps no CPU
// copy). TODO: a chosen pack's replacement, once TexturePacks has them.
gfx::Image texture_image(const std::string& path) { return gfx::load_png(path); }

// Godot's Mesh.ArrayCustomFormat and ArrayFormat shifts (_format()).
constexpr gd::Int MESH_ARRAY_CUSTOM_RGBA_HALF = 3;
constexpr gd::Int MESH_ARRAY_CUSTOM_RGBA_FLOAT = 7;
constexpr gd::Int MESH_ARRAY_FORMAT_CUSTOM0_SHIFT = 13;
constexpr gd::Int MESH_ARRAY_FORMAT_CUSTOM1_SHIFT = 16;
constexpr gd::Int MESH_ARRAY_FORMAT_CUSTOM2_SHIFT = 19;
constexpr gd::Int MESH_ARRAY_FORMAT_CUSTOM3_SHIFT = 22;

}  // namespace

const gd::Ref<Shader>& FighterView::SHADER() {
    static const gd::Ref<Shader> shader = Shader::load("res://presentation/fighter/fighter.gdshader");
    return shader;
}

const gd::Ref<Shader>& FighterView::SHADER_DOUBLE_SIDED() {
    static const gd::Ref<Shader> shader = Shader::load("res://presentation/fighter/fighter_double_sided.gdshader");
    return shader;
}

FighterView::FighterView() {
    mesh_instance = gd::make<MeshInstance3D>();
    mesh_instance->layers = STAGE_LIGHTING_FIGHTER_LAYER;
    add_child(mesh_instance);
    // Settings.changed.connect(_on_setting_changed): the app calls _on_setting_changed.
}

void FighterView::setup(const gd::Ref<CharacterModel>& character) {
    model = character;
    materials.clear();
    _eye_shape = CharacterModel::OPEN_SHAPE;
    _swapped = false;
    _swap_seen = 0;
    _overhead = false;
    _gaze_cache = gd::PI32{0, 0};
    _gaze_shifts = gd::PI32{GazeAtlas::NO_SHIFT, GazeAtlas::NO_SHIFT};
    face = model->arcade_eyes != nullptr ? gd::make<ArcadeFace>(model->arcade_eyes, ArcadeFace::next_seed()) : nullptr;
    _hidden_joints = -1;
    _load_textures();
    for (const gd::Ref<Shader>& shader : gd::Arr<gd::Ref<Shader>>{SHADER(), SHADER_DOUBLE_SIDED()}) {
        gd::Ref<ShaderMaterial> material = gd::make<ShaderMaterial>();
        material->shader = render_quality_shader(shader);
        material->set_shader_parameter("albedo_texture", texture_open);
        materials.append(material);
    }
    _apply_lighting();
    _albedo = texture_open;
    _arcade = !model->arcade_attachments.is_empty() ? gd::make<ArcadeAttachments>(model) : nullptr;
    _build_meshes();
    _previous_rows = std::vector<Vector4>();
}

// The body, hand and wing meshes: the converter's triangles, or with the Shading setting's
// "curved" the grids CurvedMesh lifts onto curved patches.
void FighterView::_build_meshes() {
    gd::Arr<gd::Ref<MeshInstance3D>> old = hand_instances.duplicate();
    old.append_array(wing_instances);
    for (const auto& node : old) {
        remove_child(node.get());   // node.queue_free()
    }
    hand_instances.clear();
    hand_meshes.clear();
    hand_channels.clear();
    wing_instances.clear();
    wing_meshes.clear();
    wing_channels.clear();
    _curved = GameSettings::read_text("shading") == "curved" ? CurvedMesh::of(model) : nullptr;
    mesh_instance->mesh = _mesh(model->surfaces);
    for (const auto& hand : model->hands) {
        _add_variants(hand, hand_instances, hand_meshes, hand_channels);
    }
    for (const auto& wing : model->wings) {
        _add_variants(wing, wing_instances, wing_meshes, wing_channels);
    }
    _apply_shading();
}

// A mesh instance showing the first variant of `group`, with a mesh per variant (the group's
// `select` repeats the meshes of equal variants), added to the given lists (its pose value's index
// in `channels`).
void FighterView::_add_variants(const gd::Ref<CharacterModel::VariantGroup>& group,
                                const gd::Arr<gd::Ref<MeshInstance3D>>& instances,
                                const gd::Arr<gd::Arr<gd::Ref<ArrayMesh>>>& meshes_of, const gd::PI32& channels) {
    gd::Arr<gd::Ref<ArrayMesh>> meshes;
    for (const auto& variant : group->variants) {
        gd::Arr<gd::Ref<CharacterModel::Surface>> surfaces;
        surfaces.append_array(variant);   // surfaces.assign(variant)
        meshes.append(_mesh(surfaces));
    }
    gd::Arr<gd::Ref<ArrayMesh>> shown = meshes;
    if (!group->select.is_empty()) {
        shown = gd::Arr<gd::Ref<ArrayMesh>>();
        for (gd::Int entry : group->select) {
            shown.append(meshes[entry]);
        }
    }
    gd::Ref<MeshInstance3D> instance = gd::make<MeshInstance3D>();
    instance->mesh = shown[0];
    instance->layers = STAGE_LIGHTING_FIGHTER_LAYER;
    add_child(instance);
    instances.append(instance);
    meshes_of.append(shown);
    channels.append(group->channel);
}

void FighterView::_load_textures() {
    texture_open = TexturePacks::texture(gd::path_join(model->directory, model->texture));
    eye_textures.clear();
    for (const auto& kv : model->eye_textures) {
        gd::Int shape = kv.first;
        eye_textures[shape] = TexturePacks::texture(gd::path_join(model->directory, model->eye_textures[shape]));
    }
    texture_swap = model->swap_texture != "" ? TexturePacks::texture(gd::path_join(model->directory, model->swap_texture))
                                             : nullptr;
    gaze_atlas = nullptr;
    if (!(model->gaze.is_null() || model->gaze.size() == 0)) {   // not model.gaze.is_empty()
        // GazeAtlas.new(model.gaze, texture_open, texture_swap or texture_open, the sheet): their pictures.
        std::string open_path = gd::path_join(model->directory, model->texture);
        std::string swap_path = texture_swap != nullptr ? gd::path_join(model->directory, model->swap_texture) : open_path;
        gaze_atlas = gd::make<GazeAtlas>(model->gaze, texture_image(open_path), texture_image(swap_path),
                                         texture_image(gd::path_join(model->directory, model->gaze["sheet"].str())));
    }
}

// The Textures or Texture pack setting changed: the shaders and atlases again.
void FighterView::_on_setting_changed(const std::string& key) {
    if (model != nullptr && key == "shading") {
        _apply_shading();
        if ((GameSettings::read_text("shading") == "curved") != (_curved != nullptr)) {
            _build_meshes();
            set_hand_variants(_hand_variants);
            set_wing_variants(_wing_variants);
            show_between(1.0);
        }
    }
    if (model == nullptr || !RENDER_QUALITY_TEXTURE_SETTINGS.has(key)) {
        return;
    }
    _load_textures();
    gd::Arr<gd::Ref<Shader>> shaders{SHADER(), SHADER_DOUBLE_SIDED()};
    gd::Int n = materials.size();
    for (gd::Int i = 0; i < n; ++i) {
        materials[i]->shader = render_quality_shader(shaders[i]);
    }
    _apply_lighting();
    _apply_shading();
    _albedo = nullptr;
    _hidden_joints = -1;
    set_eye_shape(_eye_shape);
    show_between(1.0);
}

gd::Ref<ArrayMesh> FighterView::_mesh(const gd::Arr<gd::Ref<CharacterModel::Surface>>& surfaces) {
    gd::Ref<ArrayMesh> mesh = gd::make<ArrayMesh>();
    for (const auto& surface : surfaces) {
        if (_curved != nullptr) {
            // CurvedMesh.format(): MeshArrays carries the component counts itself.
            mesh->add_surface_from_arrays(Primitive::TRIANGLES, _curved->arrays(surface));
        } else {
            if (!array_cache.has(surface)) {
                array_cache[surface] = _arrays(surface->vertices, model->vertex_floats);
            }
            // _format(): MeshArrays carries the component counts itself.
            mesh->add_surface_from_arrays(Primitive::TRIANGLES, array_cache[surface]);
        }
        mesh->surface_set_material(mesh->get_surface_count() - 1, materials[surface->double_sided ? 1 : 0]);
    }
    return mesh;
}

// The vertex variants a fighter's hand channels select: the PlayStation's models' by
// FighterHands.variant, an arcade model's hands (or jaw) by the arcade's own numbers of the
// channels' values, and an arcade True Ogre's wings and tail by the wing sequence the simulation
// steps (the arcade's tables, the same variants) and his level channel (ArcadePoses).
void FighterView::set_hands(const gd::Ref<FighterHands>& hands) {
    bool arcade = model != nullptr && model->arcade;
    _step_palette(hands->variant);
    set_hand_variants(arcade ? ArcadePoses::hand_variants(hands) : hands->variant);
    set_wing_variants(ArcadePoses::wing_variants(hands));
}

// The vertex variant each hand channel shows (FighterHands.variant).
void FighterView::set_hand_variants(const gd::PI32& variants) {
    _hand_variants = variants;
    _show_variants(hand_instances, hand_meshes, hand_channels, variants);
}

// The vertex variant each wing group shows: the wings' (channel 0), the tail's (channel 1).
void FighterView::set_wing_variants(const gd::PI32& variants) {
    _wing_variants = variants;
    _show_variants(wing_instances, wing_meshes, wing_channels, variants);
}

void FighterView::_show_variants(const gd::Arr<gd::Ref<MeshInstance3D>>& instances,
                                 const gd::Arr<gd::Arr<gd::Ref<ArrayMesh>>>& meshes_of, const gd::PI32& channels,
                                 const gd::PI32& variants) {
    gd::Int n = instances.size();
    for (gd::Int i = 0; i < n; ++i) {
        gd::Arr<gd::Ref<ArrayMesh>> meshes = meshes_of[i];
        gd::Int v = gd::clampi(variants[channels[i]], 0, meshes.size() - 1);
        instances[i]->mesh = meshes[v];
    }
}

// The stage light rig the fighter is lit by (StageLighting.fighter_parameters).
void FighterView::set_lighting(const gd::Dict<std::string, ShaderValue>& parameters) {
    _lighting = parameters;
    _apply_lighting();
}

// The back colour the fighter is lit with this frame (StageLighting.fighter_back_colour: the
// pick-up flash, practice's FREEZE SIGNAL, a burning body), in GTE units of 4096, replacing the
// stage's (set_lighting) from the first call on.
void FighterView::set_back_colour(const Vector3& colour) {
    if (colour == _back_colour) {
        return;
    }
    _back_colour = colour;
    _apply_lighting();
}

void FighterView::_apply_lighting() {
    for (const auto& material : materials) {
        for (const auto& kv : _lighting) {
            material->set_shader_parameter(kv.first, kv.second);
        }
        if (_back_colour != NO_BACK_COLOUR) {
            material->set_shader_parameter("back_colour", _back_colour);
        }
    }
}

// The Shading setting's uniforms (fighter_skin.gdshaderinc): the game's own normals or the
// smooth ones, and the curved grids' corners.
void FighterView::_apply_shading() {
    for (const auto& material : materials) {
        material->set_shader_parameter("smooth_normals", GameSettings::read_text("shading") != "original");
        material->set_shader_parameter("smooth_crease_cos", static_cast<float>(model->smooth_crease_cos));
        material->set_shader_parameter("curved", _curved != nullptr);
        material->set_shader_parameter("corner_data", _curved != nullptr ? _curved->texture : gfx::TextureRef());
        material->set_shader_parameter("corner_texels", static_cast<int>(CurvedMesh::CORNER_TEXELS));
    }
}

// The PlayStation's blink: the closed-eye atlas while `closed`.
void FighterView::set_eyes_closed(bool closed) {
    set_eye_shape(closed ? CharacterModel::CLOSED_SHAPE : CharacterModel::OPEN_SHAPE);
}

// An arcade model's face for one simulation step (ArcadeFace.step).
void FighterView::step_face(bool held, bool down) { set_eye_shape(face->step(held, down)); }

// An attack shout of voice id `code` (SimEvents SHOUT) changes an arcade model's face.
void FighterView::shout(gd::Int code) {
    if (face != nullptr) {
        face->shout(code);
    }
}

// The atlas with eye shape `shape` over the eyes (the eyes as loaded when the model lacks it).
void FighterView::set_eye_shape(gd::Int shape) {
    _eye_shape = shape;
    _show_atlas();
}

// PartSelectVertexVariant's palette swap for Gon: it runs for every row of both hand channels
// with that channel's variant (the head's rows 19 and 20 are channel 0's for Gon), and when the
// variant differs from the last it swapped for (fighter +0x1291, 0 when the fight is set up)
// copies his head's palette from the swapped one for the model's `swap_variants`, else back
// from the one FUN_800342a0 saved: channel 1's variant, called last, decides.
void FighterView::_step_palette(const gd::PI32& variants) {
    if (texture_swap == nullptr || _overhead) {
        return;
    }
    for (gd::Int variant : variants) {
        if (variant != _swap_seen) {
            _swap_seen = variant;
            _swapped = model->swap_variants.has(variant);
        }
    }
    _show_atlas();
}

// GonEyesSetOffset: Gon's eyes' look direction (`FightState` body.gon_gaze, -17 to 17): eye 0 takes
// the shifts from 0 up, eye 1 those below, the other staying at 0; an eye copies its strip when its
// shift differs from the one it last copied (so one that was never copied keeps the strip as loaded
// while the look stays at 0).
void FighterView::set_gaze(gd::Int offset) {
    if (gaze_atlas == nullptr) {
        return;
    }
    gd::PI32 wanted{offset >= 0 ? offset : 0, offset < 0 ? offset : 0};
    for (gd::Int eye = 0; eye < 2; ++eye) {
        if (wanted[eye] != _gaze_cache[eye]) {
            _gaze_cache[eye] = wanted[eye];
            _gaze_shifts[eye] = wanted[eye];
        }
    }
    _show_atlas();
}

// Under the overhead KO camera (0x800B08D4: `overhead`) PartSelectVertexVariant leaves Gon's
// palette alone; FUN_80034354 swaps it, once, on his moves with flag 0x40000 (`react_chain`), and
// nothing copies it back before the next fight is set up. Call before set_hands.
void FighterView::set_overhead(bool overhead, bool react_chain) {
    _overhead = overhead;
    if (overhead && react_chain && texture_swap != nullptr && _swap_seen != OVERHEAD_SEEN) {
        _swap_seen = OVERHEAD_SEEN;
        _swapped = true;
        _show_atlas();
    }
}

// The atlas for the eye shape, Gon's head palette and his eyes' look direction: the swapped palette
// over the eye shapes (Gon has none), the pasted patches over either.
void FighterView::_show_atlas() {
    gfx::TextureRef texture;
    if (gaze_atlas != nullptr) {
        texture = gaze_atlas->show(_swapped, _gaze_shifts);
    } else {
        texture = _swapped ? texture_swap : eye_textures.get(_eye_shape, texture_open);
    }
    if (texture == _albedo) {
        return;
    }
    _albedo = texture;
    for (const auto& material : materials) {
        material->set_shader_parameter("albedo_texture", texture);
    }
}

// The wind on an arcade model's swinging attachments (ArcadeWind.vector), felt from the next
// set_joints on.
void FighterView::set_wind(const gd::PI32& vector) {
    if (_arcade != nullptr) {
        _arcade->wind = vector;
    }
}

// Hides the faces skinned to the joints of bit mask `mask` (a part the game stops drawing).
void FighterView::set_hidden_joints(gd::Int mask) {
    if (mask == _hidden_joints) {
        return;
    }
    _hidden_joints = mask;
    for (const auto& material : materials) {
        material->set_shader_parameter("hidden_joints", static_cast<int>(mask));
    }
}

// Applies the world joint frames of a simulation step (game units), and an arcade model's
// attachment joints built from them. The view node itself stays at the origin: the joints carry
// the fighter's position. `snap` drops interpolation, and an arcade model's attachments start
// again from their rest (the fighter appears: their last step is stale).
void FighterView::set_joints(const gd::Arr<gd::Ref<JointFrame>>& joints, bool snap) {
    gd::Arr<gd::Ref<JointFrame>> all = joints;
    if (_arcade != nullptr) {
        if (snap) {
            _arcade->reset();
        }
        all = _arcade->update(joints);
    }
    std::vector<Vector4> rows;
    rows.resize(static_cast<size_t>(3 * CharacterModel::JOINT_COUNT));
    AABB bounds;
    gd::Int n = all.size();
    for (gd::Int j = 0; j < n; ++j) {
        gd::Arr<Vector4> r = WorldSpace::joint_rows(all[j]);
        rows[static_cast<size_t>(3 * j)] = r[0];
        rows[static_cast<size_t>(3 * j + 1)] = r[1];
        rows[static_cast<size_t>(3 * j + 2)] = r[2];
        if (j >= joints.size() && !_arcade->has_joint(j)) {
            continue;   // an attachment the model does not have, at the origin
        }
        Vector3 origin(r[0].w, r[1].w, r[2].w);
        bounds = j == 0 ? AABB(origin, Vector3::ZERO()) : bounds.expand(origin);
    }
    _previous_rows = snap || _rows.empty() ? rows : _rows;
    _rows = rows;
    _bounds = bounds.grow(BOUNDS_MARGIN);
    show_between(1.0);
}

// Draws the pose between the previous step (0) and the current one (1).
void FighterView::show_between(double weight) {
    if (_rows.empty()) {
        return;
    }
    std::vector<Vector4> rows = _rows;
    if (weight < 1.0 && _previous_rows.size() == _rows.size()) {
        rows = std::vector<Vector4>();
        rows.resize(_rows.size());
        gd::Int n = static_cast<gd::Int>(_rows.size());
        for (gd::Int i = 0; i < n; ++i) {
            size_t k = static_cast<size_t>(i);
            rows[k] = _previous_rows[k].lerp(_rows[k], static_cast<float>(weight));
        }
    }
    for (const auto& material : materials) {
        material->set_shader_parameter("bone_rows", rows);
    }
    mesh_instance->custom_aabb = _bounds;
    gd::Arr<gd::Ref<MeshInstance3D>> others = hand_instances.duplicate();
    others.append_array(wing_instances);
    for (const auto& h : others) {
        h->custom_aabb = _bounds;
    }
}

gd::Int FighterView::_format() {
    return (MESH_ARRAY_CUSTOM_RGBA_FLOAT << MESH_ARRAY_FORMAT_CUSTOM0_SHIFT) |
           (MESH_ARRAY_CUSTOM_RGBA_FLOAT << MESH_ARRAY_FORMAT_CUSTOM1_SHIFT) |
           (MESH_ARRAY_CUSTOM_RGBA_HALF << MESH_ARRAY_FORMAT_CUSTOM2_SHIFT) |
           (MESH_ARRAY_CUSTOM_RGBA_FLOAT << MESH_ARRAY_FORMAT_CUSTOM3_SHIFT);
}

// Splits the converter's interleaved corners (pa, pb, wb, n, uv, bones, the smooth normal's
// halves s_own and s_other, bone_other, the third term pc, wc, bone_c) into mesh arrays: CUSTOM1.w
// is bone_c + wc, UV2 and CUSTOM3.w hold pc; s_own in half precision.
// (MeshArrays holds CUSTOM2's halves as plain floats: the renderer packs the formats.)
MeshArrays FighterView::_arrays(const gd::Packed<float>& v, gd::Int stride) {
    const std::vector<float>& f = v.vec();
    gd::Int count = gd::div(v.size(), stride);
    MeshArrays arrays;
    std::vector<Vector3>& positions = arrays.vertex;
    std::vector<Vector3>& normals = arrays.normal;
    std::vector<gfx::Vec2>& uvs = arrays.tex_uv;
    std::vector<float>& custom0 = arrays.custom[0];
    std::vector<float>& custom1 = arrays.custom[1];
    std::vector<float>& custom2 = arrays.custom[2];   // PackedByteArray of halves (HALF_RGBA_BYTES per vertex)
    std::vector<float>& custom3 = arrays.custom[3];
    std::vector<gfx::Vec2>& uv2s = arrays.tex_uv2;
    size_t c = static_cast<size_t>(count);
    positions.resize(c);
    normals.resize(c);
    uvs.resize(c);
    custom0.resize(4 * c);
    custom1.resize(4 * c);
    custom2.resize(4 * c);   // HALF_RGBA_BYTES * count bytes: four halves per vertex
    custom3.resize(4 * c);
    uv2s.resize(c);
    for (gd::Int vi = 0; vi < count; ++vi) {
        size_t i = static_cast<size_t>(vi);   // the GDScript's `i`, as a vector index
        size_t o = static_cast<size_t>(vi * stride);
        positions[i] = Vector3(f[o], f[o + 1], f[o + 2]);
        custom0[4 * i] = f[o + 3];
        custom0[4 * i + 1] = f[o + 4];
        custom0[4 * i + 2] = f[o + 5];
        custom0[4 * i + 3] = f[o + 6];
        normals[i] = Vector3(f[o + 7], f[o + 8], f[o + 9]);
        uvs[i] = gfx::Vec2(f[o + 10], f[o + 11]);
        custom1[4 * i] = f[o + 12];
        custom1[4 * i + 1] = f[o + 13];
        custom1[4 * i + 2] = f[o + 14];
        custom1[4 * i + 3] = static_cast<float>(static_cast<double>(f[o + 27]) + f[o + 26]);
        uv2s[i] = gfx::Vec2(f[o + 23], f[o + 24]);
        size_t h = 4 * i;   // HALF_RGBA_BYTES * i bytes: encode_half at h, h + 2, h + 4, h + 6
        custom2[h] = f[o + 15];
        custom2[h + 1] = f[o + 16];
        custom2[h + 2] = f[o + 17];
        custom2[h + 3] = f[o + 21];
        custom3[4 * i] = f[o + 18];
        custom3[4 * i + 1] = f[o + 19];
        custom3[4 * i + 2] = f[o + 20];
        custom3[4 * i + 3] = f[o + 25];
    }
    arrays.custom_components = {4, 4, 4, 4};
    return arrays;
}

}  // namespace t3
