// remake/presentation/scene/fight_view.gd
// Draws a fight (see the header).
#include "presentation/scene/fight_view.h"

#include <cmath>
#include <functional>

#include "content/effect_data.h"
#include "content/fight_tables.h"
#include "content/game_ram.h"
#include "content/move_row.h"
#include "content/stage_data.h"
#include "core/camera/camera_director.h"
#include "core/fighter/fighter_hands.h"
#include "core/camera/backdrop_turn.h"
#include "core/camera/camera_view.h"
#include "core/camera/view_matrix.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fighter/character.h"
#include "core/fighter/fighter_body.h"
#include "core/fighter/fighter_input.h"
#include "core/fighter/fighter_state.h"
#include "core/fighter/move_flag.h"
#include "core/force/tekken_force.h"
#include "core/game/game_mode.h"
#include "core/game/mode_region.h"
#include "core/math/fx.h"
#include "core/sim_events.h"
#include "gfx/gl.h"
#include "presentation/ball/ball_view.h"
#include "presentation/camera/camera_rig.h"
#include "presentation/effects/effect_objects_view.h"
#include "presentation/effects/effects_view.h"
#include "presentation/fighter/fighter_view.h"
#include "presentation/force/force_items_view.h"
#include "presentation/stage/stage_lighting.h"
#include "presentation/stage/stage_view.h"
#include "presentation/stage/tiled_panorama.h"
#include "presentation/world_space.h"

namespace t3 {

namespace {

bool same_colour(const gfx::Color& a, const gfx::Color& b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

// Godot's wrapi(value, min, max).
gd::Int wrapi(gd::Int value, gd::Int min, gd::Int max) { return min + gd::posmod(value - min, max - min); }

// StageLighting.fighter_parameters as the Dictionary FighterView.set_lighting takes.
gd::Dict<std::string, ShaderValue> parameters_dict(const StageLighting::FighterParameters& p) {
    gd::Dict<std::string, ShaderValue> d;
    d.set("base_colour", p.base_colour);
    d.set("back_colour", p.back_colour);
    d.set("light_colour", p.light_colour);
    d.set("fill_colour", p.fill_colour);
    return d;
}

}  // namespace

FightView::FightView() {
    stage_view = gd::make<StageView>();
    lighting = gd::make<StageLighting>();
    effects = gd::make<EffectsView>();
    rig = gd::make<CameraRig>();
}

void FightView::setup(const gd::Ref<StageData>& stage, const gd::Ref<EffectData>& effect_data,
                      const gd::Arr<gd::Ref<CharacterModel>>& models, const gd::PI32& costume_slots, const gd::PI32& char_ids,
                      const gd::Ref<StageData>& light_stage) {
    stage_view->setup(stage);
    add_child(stage_view);
    gd::Ref<StageData> lights = light_stage != nullptr ? light_stage : stage;
    _ambient_level = lights->ambient_level;
    lighting->setup(lights, rig->camera);
    add_child(lighting);
    effects->setup(effect_data);
    effects->set_costumes(costume_slots);
    add_child(effects);
    add_child(rig);
    bool true_ogre = char_ids.has(Character::TRUE_OGRE);
    _floor_kind = stage_view->floor_kind();
    _stage_number = stage->number;
    _true_ogre = true_ogre;
    // The arcade draws the wind's direction with rand(); here it follows from the match, so that
    // runs (screenshots, replays) repeat. hash([stage.number, costume_slots]): Godot's Array hash is
    // not reproduced; any hash of the same values repeats as well.
    uint64_t h = std::hash<gd::Int>{}(stage->number);
    for (gd::Int slot : costume_slots) h = h * 31 + std::hash<gd::Int>{}(slot);
    _wind_rng.set_seed(static_cast<gd::Int>(h & 0xFFFFFFFFULL));
    _wind.start(stage->number, _wind_rng);
    // stages.md#clear-colour: no panorama in True Ogre fights; stage 11's blue otherwise (the
    // arcade's stages have their sky instead, also gone in True Ogre fights).
    stage_view->panorama->visible = !true_ogre;
    clear_colour = (stage->number == CLEAR_STAGE && !true_ogre && stage_view->arcade == nullptr) ? CLEAR_STAGE_COLOUR
                                                                                                    : gfx::Color::black();
    lighting->environment->background_color = clear_colour;
    stage_view->apply_environment(lighting->environment, true_ogre);
    // Where the clear colour is the sky (stage 11) the view may extend upwards without limit:
    // above the panorama the game shows that colour too.
    rig->coverage = same_colour(clear_colour, gfx::Color::black()) ? stage_view->coverage() : gd::PI32();
    _fighter_lighting = parameters_dict(StageLighting::fighter_parameters(lights));
    _costume_slots = costume_slots.duplicate();
    for (const gd::Ref<CharacterModel>& model : models) {
        fighters.append(_new_fighter(model));
    }
}

gd::Ref<FighterView> FightView::_new_fighter(const gd::Ref<CharacterModel>& model) {
    gd::Ref<FighterView> view = gd::make<FighterView>();
    view->array_cache = _array_cache;
    view->setup(model);
    view->set_lighting(_fighter_lighting);
    add_child(view);
    return view;
}

// A record's model swapped in during the fight: only its view is built again.
void FightView::replace_fighter(gd::Int i, const gd::Ref<CharacterModel>& model, gd::Int costume_slot) {
    gd::Ref<FighterView> old = fighters[i];
    gd::Ref<FighterView> view = _new_fighter(model);
    view->visible = false;
    move_child(view.get(), old->get_index());
    fighters[i] = view;
    remove_child(old.get());
    // old.queue_free(): the last reference goes with `old`.
    _costume_slots[i] = static_cast<int32_t>(costume_slot);
    effects->set_costumes(_costume_slots);
}

// Tekken Ball's and Tekken Force's parts.
void FightView::setup_modes(const gd::Ref<StageData>& stage, const gd::Ref<FightTables>& tables, const gd::Ref<GameRam>& volley) {
    if (stage->tile_map.size() != 0) {
        // SubViewport (UPDATE_ALWAYS, 2D only): the render target is made by draw_backdrop.
        tiled = gd::make<TiledPanorama>();
        tiled->setup(stage, tables);
        gd::Ref<StandardMaterial3D> material = gd::make<StandardMaterial3D>();
        material->shading_mode = StandardMaterial3D::SHADING_MODE_UNSHADED;
        // material.albedo_texture = _backdrop_viewport.get_texture(): set as draw_backdrop makes it.
        material->cull_mode = StandardMaterial3D::CULL_DISABLED;
        material->disable_receive_shadows = true;
        material->disable_fog = true;
        // Native: the render target's rows run bottom-up (GL's), Godot's viewport texture's top-down:
        // the texture is read flipped vertically. Its one level is sampled linearly (a viewport
        // texture has no mipmaps).
        material->uv1_scale = Vector3(1, -1, 1);
        material->uv1_offset = Vector3(0, 1, 0);
        material->texture_filter = StandardMaterial3D::TEXTURE_FILTER_LINEAR;
        _backdrop_material = material;
        _backdrop = gd::make<MeshInstance3D>();
        _backdrop->mesh = gd::make<ImmediateMesh>();
        _backdrop->material_override = material;
        _backdrop->cast_shadow = GeometryInstance3D::SHADOW_CASTING_SETTING_OFF;
        add_child(_backdrop);
        rig->coverage = gd::PI32();
    }
    if (stage->item.size() != 0) {
        items = gd::make<ForceItemsView>();
        items->setup(stage);
        add_child(items);
    }
    if (volley != nullptr) {
        ball = gd::make<BallView>();
        ball->setup(volley);
        add_child(ball);
    }
}

// The backdrop quad over the whole view at BACKDROP_DEPTH of the far plane.
void FightView::_place_backdrop(gfx::Vec2 window) {
    gfx::Vec2 size(static_cast<float>(gd::maxi(static_cast<gd::Int>(window.x), 1)),
                   static_cast<float>(gd::maxi(static_cast<gd::Int>(window.y), 1)));
    if (_viewport_size.x != size.x || _viewport_size.y != size.y) {
        _viewport_size = size;
    }
    gd::Ref<Camera3D> camera = rig->camera;
    float depth = static_cast<float>(CameraRig::FAR * BACKDROP_DEPTH);
    Vector3 corners[4];
    const gfx::Vec2 at[4] = {gfx::Vec2(0, 0), gfx::Vec2(1, 0), gfx::Vec2(0, 1), gfx::Vec2(1, 1)};
    for (int c = 0; c < 4; ++c) {
        corners[c] = camera->project_position(gfx::Vec2(at[c].x * window.x, at[c].y * window.y), depth);
    }
    const gfx::Vec2 uvs[4] = {gfx::Vec2(0, 0), gfx::Vec2(1, 0), gfx::Vec2(0, 1), gfx::Vec2(1, 1)};
    gd::Ref<ImmediateMesh> mesh = std::dynamic_pointer_cast<ImmediateMesh>(_backdrop->mesh);
    mesh->clear_surfaces();
    mesh->surface_begin(Primitive::TRIANGLES);
    for (int k : {0, 1, 2, 1, 3, 2}) {
        mesh->surface_set_uv(uvs[k]);
        mesh->surface_add_vertex(corners[k]);
    }
    mesh->surface_end();
}

// After a simulation step.
void FightView::apply(const gd::Ref<FightSimulation>& sim) {
    gd::Int n_fighters = fighters.size();
    for (gd::Int i = 0; i < n_fighters; ++i) {
        gd::Ref<FighterState> f = sim->fight->fighters[i];
        gd::Ref<FighterView> view = fighters[i];
        bool shown = f->body != nullptr && f->active != 0 && (sim->fight->undrawn_mask & (1LL << i)) == 0;
        bool appearing = shown && !view->visible;
        view->visible = shown;
        if (!shown) {
            continue;
        }
        view->set_wind(_wind.vector);
        view->set_joints(f->body->draw_joints.slice(0, FighterView::JOINT_SLOTS), appearing);
        view->set_hidden_joints(f->body->stick_shown ? 0 : (1LL << FighterView::STICK_JOINT));
        view->set_overhead(sim->camera->overhead != 0, (f->pose_move->flags & MoveFlag::REACT_CHAIN) != 0);
        view->set_hands(f->hands);
        view->set_gaze(f->body->gon_gaze);
        for (const gd::Ref<SimEvents::Event>& e : sim->events->items) {
            if (e->kind == SimEvents::Kind::SHOUT && e->b == f->player_index) {
                view->shout(e->a);
            }
        }
        if (view->face != nullptr) {
            view->step_face(Blinker::held(f->pose_move->flags), Blinker::lying(f->pose_move->flags, f->pose_move->state));
        } else {
            view->set_eyes_closed(f->hands->face == 1);
        }
        gfx::Color freeze = StageLighting::NO_SIGNAL;
        if (gd::Int(sim->fight->mode) == GameMode::PRACTICE) {
            const Color8& s = sim->fight->signal_colour[f->player_index];
            freeze = gfx::Color::c8(static_cast<int>(s.r), static_cast<int>(s.g), static_cast<int>(s.b), static_cast<int>(s.a));
        }
        view->set_back_colour(StageLighting::fighter_back_colour(_ambient_level, sim->fight->flash_level[i], freeze));
    }
    _wind.step();
    effects->step(sim->events);
    effects->objects->show_pool(sim->fight->effects);
    gd::Ref<CameraView> v = sim->camera->view;
    gd::Ref<CameraView> view = gd::make<CameraView>(v->pitch, v->yaw, v->x, v->y, v->z, v->h);
    rig->set_view(view, _is_cut(view));
    _last_view = view;
    _overhead = sim->camera->overhead != 0;
    gd::Ref<FighterState> f0 = sim->fight->fighters[0];
    gd::Ref<FighterState> f1 = sim->fight->fighters[1];
    stage_view->follow(WorldSpace::point(static_cast<double>(f0->root_x + f1->root_x) / 2.0, 0,
                                         static_cast<double>(f0->root_z + f1->root_z) / 2.0));
    _spotlight(sim);
    stage_view->set_floor_distance(static_cast<float>(_floor_distance(sim)));
    stage_view->set_backdrop_turn(sim->fight->backdrop->angle);
    stage_view->step(v->yaw, sim->fight->rounds_played, sim->fight->round_state, sim->pads->physical[0]);
    if (tiled != nullptr) {
        tiled->step(sim);
        stage_view->set_floor_edge(tiled->floor_edge);
    }
    if (ball != nullptr) {
        ball->apply(sim);
    }
    if (items != nullptr) {
        items->apply(sim);
    }
}

// FloorDrawGrid: where the floor's distance shading starts.
double FightView::_floor_distance(const gd::Ref<FightSimulation>& sim) {
    gd::Ref<CameraView> view = sim->camera->view;
    if (gd::Int(sim->fight->mode) == GameMode::FORCE) {
        return std::fabs(static_cast<double>(view->z));
    }
    gd::Ref<FighterState> f0 = sim->fight->fighters[0];
    gd::Ref<FighterState> f1 = sim->fight->fighters[1];
    gd::Int dx = view->x - Fx::div_trunc(f0->root_x + f1->root_x, 2);
    gd::Int dz = view->z - Fx::div_trunc(f0->root_z + f1->root_z, 2);
    return std::sqrt(static_cast<double>(dx * dx + dz * dz));
}

// FloorSetup's kind and FUN_80049AE0's light points of this step.
void FightView::_spotlight(const gd::Ref<FightSimulation>& sim) {
    gd::Ref<FightState> fight = sim->fight;
    gd::Int kind = _floor_kind;
    if (gd::Int(fight->mode) == GameMode::FORCE) {
        if (gd::Int(fight->region->fight_index) == FORCE_CAMERA_LEVEL) {
            kind = 4;
        }
    } else if (_true_ogre && kind == 0) {
        kind = 2;
    }
    if (kind <= 0 || kind >= SPOT_K.size()) {
        stage_view->set_spotlight(0, Vector3::ZERO(), Vector3::ZERO(), 0, 0);
        return;
    }
    gd::Ref<FighterState> f0 = fight->fighters[0];
    gd::Ref<FighterState> f1 = fight->fighters[1];
    Vector3 a = WorldSpace::point(static_cast<double>((f0->root_x + f1->root_x) / 2), 0,
                                  static_cast<double>((f0->root_z + f1->root_z) / 2));
    Vector3 b = Vector3::ZERO();
    gd::Int limit = SPOT_LIMIT;
    switch (kind) {
        case 1:
            a = WorldSpace::point(static_cast<double>(f0->root_x), 0, static_cast<double>(f0->root_z));
            b = WorldSpace::point(static_cast<double>(f1->root_x), 0, static_cast<double>(f1->root_z));
            break;
        case 2:
            limit = (_true_ogre && DARK_STAGES.has(_stage_number)) ? SPOT_LIMIT_DARK : SPOT_LIMIT_MIDPOINT;
            break;
        case 3: {
            gd::Ref<FighterState> p = fight->force != nullptr ? fight->force->player() : f0;
            a = WorldSpace::point(static_cast<double>(p->root_x), 0, static_cast<double>(p->root_z));
            break;
        }
        case 4:
            a = WorldSpace::point(static_cast<double>(sim->camera->view->x), 0, static_cast<double>(FORCE_CAMERA_Z));
            break;
        default:
            break;
    }
    gd::Int spot_k = SPOT_K[kind];
    double k = static_cast<double>(spot_k) / (WorldSpace::UNITS_PER_METRE * WorldSpace::UNITS_PER_METRE);
    stage_view->set_spotlight(kind, a, b, static_cast<float>(k), static_cast<float>(limit));
}

// Every rendered frame: the poses and camera between the last two steps.
void FightView::interpolate(double weight, gfx::Vec2 window) {
    for (const gd::Ref<FighterView>& view : fighters) {
        if (view->visible) {
            view->show_between(weight);
        }
    }
    rig->place(weight, window);
    stage_view->place_sky(rig->camera, rig->frame_tan);
    // FUN_80036254: under the overhead KO camera the fighters are drawn ViewMatrix.OVERHEAD_DEPTH
    // units nearer to it (along the view axis).
    Vector3 toward = _overhead ? rig->camera->get_global_basis().z() *
                                     static_cast<float>(ViewMatrix::OVERHEAD_DEPTH / WorldSpace::UNITS_PER_METRE)
                               : Vector3::ZERO();
    for (const gd::Ref<FighterView>& view : fighters) {
        view->set_position(toward);
    }
    if (tiled != nullptr) {
        tiled->show_between(weight, rig->frame_rect);
        _place_backdrop(window);
    }
    if (ball != nullptr) {
        ball->show_between(weight);
    }
    if (items != nullptr) {
        gfx::Rect2 f = rig->frame_rect;
        items->show_between(weight, rig->camera,
                            gfx::Rect2(f.position.x * window.x, f.position.y * window.y, f.size.x * window.x, f.size.y * window.y));
    }
}

bool FightView::_is_cut(const gd::Ref<CameraView>& view) {
    if (_last_view == nullptr) {
        return true;
    }
    gd::Ref<CameraView> a = _last_view;
    double moved = Vector3(static_cast<float>(view->x - a->x), static_cast<float>(view->y - a->y),
                           static_cast<float>(view->z - a->z)).length();
    gd::Int turned = gd::maxi(gd::absi(wrapi(view->yaw - a->yaw, -2048, 2048)), gd::absi(wrapi(view->pitch - a->pitch, -2048, 2048)));
    return moved > CUT_DISTANCE || turned > CUT_ANGLE;
}

// Native: the SubViewport's update.
void FightView::draw_backdrop(gfx::Canvas& canvas) {
    if (tiled == nullptr) {
        return;
    }
    int w = static_cast<int>(_viewport_size.x), h = static_cast<int>(_viewport_size.y);
    if (_backdrop_viewport == nullptr || _backdrop_viewport->stale() || _backdrop_viewport->width() != w || _backdrop_viewport->height() != h) {
        _backdrop_viewport = std::make_shared<gfx::RenderTarget>(w, h);
        _backdrop_material->albedo_texture = _backdrop_viewport->texture();
    }
    _backdrop_viewport->bind();
    // A SubViewport clears to transparent black before it draws.
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    canvas.begin(w, h);
    if (tiled->visible) {
        tiled->draw(canvas, _viewport_size);
    }
    canvas.flush();
    gfx::RenderTarget::unbind();
}

void FightView::graphics_lost() {
    if (_backdrop_viewport != nullptr) {
        _backdrop_viewport->forget();
        _backdrop_viewport = nullptr;
    }
}

}  // namespace t3
