// remake/presentation/effects/effects_view.gd
// Draws the flipbook effects the simulation requests (see the header).
#include "presentation/effects/effects_view.h"

#include <cmath>

#include "content/texture_packs.h"
#include "core/effects/effect_objects.h"
#include "core/sim_events.h"
#include "presentation/effects/effect_objects_view.h"
#include "presentation/world_space.h"

namespace t3 {

EffectsView::EffectsView() {
    objects = gd::make<EffectObjectsView>();
    flipbooks = gd::make<Node3D>();
    _quad = gd::make<QuadMesh>();
}

gfx::TextureRef EffectsView::flipbook_texture(const gd::Ref<EffectData::Flipbook>& book) {
    return !book->path.empty() ? TexturePacks::texture(book->path) : nullptr;
}

void EffectsView::setup(const gd::Ref<EffectData>& effects) {
    data = effects;
    character = data->character.duplicate();
    objects->setup(data);
    add_child(objects);
    add_child(flipbooks);
    float side = static_cast<float>(SPRITE_UNITS / WorldSpace::UNITS_PER_METRE);
    _quad->set_size(gfx::Vec2(side, side));
}

// The fight's character flipbooks (sets 0 and 1) by the fighters' costume slots.
void EffectsView::set_costumes(const gd::PI32& slots) {
    character.clear();
    for (gd::Int slot : slots) {
        character.append(data->costume(slot));
    }
    objects->set_costumes(character);
}

// One simulation step.
void EffectsView::step(const gd::Ref<SimEvents>& events) {
    if (events->has(SimEvents::Kind::EFFECTS_CLEAR)) {
        clear();
    }
    gd::Ref<SimEvents::Event> draw = events->first(SimEvents::Kind::EFFECTS_DRAW);
    flipbooks->visible = draw != nullptr;
    if (draw != nullptr && draw->a == SimEvents::EFFECTS_ADVANCE) {
        _advance();
    }
    for (const gd::Ref<SimEvents::Event>& e : events->items) {
        if (e->point.is_empty()) {
            continue;
        }
        Vector3 at = WorldSpace::point_i(e->point);
        switch (e->kind) {
            case SimEvents::Kind::EFFECT:
                _character(e->a, at);
                break;
            case SimEvents::Kind::SPARK:
                if (e->a < data->system.size()) {
                    _spawn(data->system[e->a], at, 0);
                }
                break;
            case SimEvents::Kind::HIT_SPARK: {
                // HitSpawnEffect: set 3 index 0 (hit), 1 (no damage) or 2 (guard); a damaging
                // hit from 21 on adds the attacker's character flipbook. Both drift along the hit
                // direction (EffectSpawn).
                Vector3 drift = drift_of(e->vector);
                if (e->a < data->system.size()) {
                    _spawn(data->system[e->a], at, 0, drift);
                }
                if (e->a == 0 && e->b >= EffectObjects::SPARK_DAMAGE) {
                    _character(e->c, at, drift);
                }
                break;
            }
            case SimEvents::Kind::DUST:
            case SimEvents::Kind::LANDING_DUST: {
                gd::Int n = data->dust_offsets.size();
                for (gd::Int i = 0; i < n; ++i) {
                    gd::Vector3i o = data->dust_offsets[i];
                    Vector3 p = WorldSpace::point(static_cast<double>(gd::Int(e->point[0]) + o.x),
                                                  static_cast<double>(data->dust_y + o.y),
                                                  static_cast<double>(gd::Int(e->point[2]) + o.z));
                    _spawn(data->dust, p, i * data->dust_interval);
                }
                break;
            }
            default:
                break;
        }
    }
}

void EffectsView::clear() {
    objects->clear();
    for (const gd::Ref<Sprite>& s : sprites) {
        _release(s);
    }
    for (const gd::Ref<Light>& l : lights) {
        _release_light(l);
    }
    sprites.clear();
    lights.clear();
}

// EffectSpawn's drift: 0x1900 / 128 game units a frame along the hit direction (none without one).
Vector3 EffectsView::drift_of(const gd::PI32& direction) {
    if (direction.size() < 3) {
        return Vector3::ZERO();
    }
    Vector3 d(static_cast<float>(gd::Int(direction[0])), static_cast<float>(gd::Int(direction[1])),
              static_cast<float>(gd::Int(direction[2])));
    double length = std::sqrt(static_cast<double>(d.length_squared()));
    if (length == 0.0) {
        return Vector3::ZERO();
    }
    return WorldSpace::point(d.x, d.y, d.z) * static_cast<float>(DRIFT / 128.0 / length);
}

void EffectsView::_character(gd::Int set_index, const Vector3& at, const Vector3& drift) {
    if (set_index < 0 || set_index >= character.size()) {
        return;
    }
    gd::Ref<EffectData::Flipbook> book = character[set_index];
    if (book != nullptr) {
        _spawn(book, at, 0, drift);
        _light(at, gfx::Color(book->light[0], book->light[1], book->light[2], book->light[3]));
    }
}

void EffectsView::_spawn(const gd::Ref<EffectData::Flipbook>& book, const Vector3& at, gd::Int delay, const Vector3& drift) {
    if (book == nullptr) {
        return;
    }
    gd::Ref<Sprite> s = !_free.is_empty() ? _free.pop_back() : _new_sprite();
    s->book = book;
    s->delay = delay;
    s->velocity = drift;
    s->frame = delay > 0 ? 0 : 1;
    s->material->blend_mode = book->additive ? StandardMaterial3D::BLEND_MODE_ADD : StandardMaterial3D::BLEND_MODE_MIX;
    s->material->albedo_texture = flipbook_texture(book);
    s->material->uv1_scale = Vector3(static_cast<float>(1.0 / static_cast<double>(book->frames)), 1, 1);
    s->node->set_position(at);
    sprites.append(s);
    _paint(s);
}

gd::Ref<EffectsView::Sprite> EffectsView::_new_sprite() {
    gd::Ref<Sprite> s = gd::make<Sprite>();
    s->material = gd::make<StandardMaterial3D>();
    s->material->shading_mode = StandardMaterial3D::SHADING_MODE_UNSHADED;
    s->material->billboard_mode = StandardMaterial3D::BILLBOARD_ENABLED;
    s->material->transparency = StandardMaterial3D::TRANSPARENCY_ALPHA;
    s->material->texture_filter = StandardMaterial3D::TEXTURE_FILTER_LINEAR;
    s->material->depth_draw_mode = StandardMaterial3D::DEPTH_DRAW_DISABLED;
    s->node = gd::make<MeshInstance3D>();
    s->node->mesh = _quad;
    s->node->material_override = s->material;
    s->node->cast_shadow = GeometryInstance3D::SHADOW_CASTING_SETTING_OFF;
    flipbooks->add_child(s->node);
    return s;
}

// An ended sprite goes back to the pool, hidden.
void EffectsView::_release(const gd::Ref<Sprite>& s) {
    s->node->visible = false;
    s->book = nullptr;
    _free.append(s);
}

void EffectsView::_light(const Vector3& at, const gfx::Color& colour) {
    if (colour.r == 0 && colour.g == 0 && colour.b == 0 && colour.a == 1) {   // Color.BLACK
        return;
    }
    gd::Ref<Light> l = gd::make<Light>();
    l->colour = colour;
    if (_free_lights.is_empty()) {
        l->node = gd::make<OmniLight3D>();
        l->node->omni_range = static_cast<float>(LIGHT_RANGE);
        flipbooks->add_child(l->node);
    } else {
        l->node = _free_lights.pop_back();
        l->node->visible = true;
    }
    l->node->light_color = colour;
    l->node->set_position(at);
    lights.append(l);
    _paint_light(l);
}

// An ended light's node goes back to the pool, hidden.
void EffectsView::_release_light(const gd::Ref<Light>& l) {
    l->node->visible = false;
    _free_lights.append(l->node);
}

// One frame of the running sprites and lights; the ended ones leave in order.
void EffectsView::_advance() {
    gd::Int kept = 0;
    gd::Int n_sprites = sprites.size();
    for (gd::Int i = 0; i < n_sprites; ++i) {
        gd::Ref<Sprite> s = sprites[i];
        if (s->delay > 0) {
            s->delay -= 1;
            if (s->delay == 0) {
                s->frame = 1;
            }
        } else {
            s->frame += 1;
            s->node->set_position(s->node->get_position() + s->velocity);
        }
        if (s->frame > s->book->frames) {
            _release(s);
        } else {
            _paint(s);
            sprites[kept] = s;
            kept += 1;
        }
    }
    sprites.resize(kept);
    kept = 0;
    gd::Int n_lights = lights.size();
    for (gd::Int i = 0; i < n_lights; ++i) {
        gd::Ref<Light> l = lights[i];
        l->left -= 1;
        if (l->left <= 0) {
            _release_light(l);
        } else {
            _paint_light(l);
            lights[kept] = l;
            kept += 1;
        }
    }
    lights.resize(kept);
}

void EffectsView::_paint(const gd::Ref<Sprite>& s) {
    s->node->visible = s->frame > 0;
    if (s->frame <= 0) {
        return;
    }
    s->material->uv1_offset = Vector3(static_cast<float>(static_cast<double>(s->frame - 1) / static_cast<double>(s->book->frames)), 0, 0);
    double level = 1.0;
    if (s->book->additive) {
        gd::Int into_fade = s->frame - s->book->frames + FADE_FRAMES;
        if (into_fade > 0) {
            level = (128.0 - static_cast<double>((into_fade * 0x9999) >> 12)) / 128.0;
        }
        // Color(level, level, level) * HDR_BOOST: Godot scales every channel, alpha too.
        float c = static_cast<float>(level * HDR_BOOST);
        s->material->albedo_color = gfx::Color(c, c, c, static_cast<float>(HDR_BOOST));
    } else {
        s->material->albedo_color = gfx::Color(1, 1, 1, 0.5f);
    }
}

void EffectsView::_paint_light(const gd::Ref<Light>& l) {
    l->node->light_energy = static_cast<float>(LIGHT_ENERGY * static_cast<double>(l->left) / static_cast<double>(LIGHT_FRAMES));
}

}  // namespace t3
