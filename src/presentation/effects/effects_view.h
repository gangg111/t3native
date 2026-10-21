#pragma once
// remake/presentation/effects/effects_view.gd
// Draws the flipbook effects the simulation requests (effects.md#flipbook-ring): camera-facing
// sprites with HDR additive or 50 % blending, the character sets' decaying point light, and
// the six-puff landing dust ring. The simulation's EFFECTS_DRAW says each step whether they are
// drawn and whether they advance a frame; sprite and light nodes are pooled, sprites share one quad mesh.
//
// Sizes follow FUN_80077158: a 32-texel frame covers 32·0x2E0B/H world units at the game's
// projection distance H = 500 (754 units). Additive sprites fade over their last 10 frames.
#include "content/effect_data.h"
#include "gd/gd.h"
#include "gfx/canvas.h"
#include "gfx/texture.h"
#include "gfx3d/nodes.h"

namespace t3 {

class SimEvents;
class EffectObjectsView;

class EffectsView : public Node3D {
public:
    static constexpr double SPRITE_UNITS = 32.0 * 0x2E0B / 500.0;
    static constexpr gd::Int FADE_FRAMES = 10;
    static constexpr gd::Int LIGHT_FRAMES = 48;     // FUN_8003A160's countdown
    static constexpr double LIGHT_RANGE = 3.0;
    static constexpr double LIGHT_ENERGY = 1.0;
    static constexpr double HDR_BOOST = 1.6;        // additive sprites are brighter than white for the glow
    static constexpr gd::Int DRIFT = 0x1900;        // EffectSpawn: the drift speed (position << 7 units a frame)

    class Sprite {
    public:
        gd::Ref<MeshInstance3D> node;
        gd::Ref<StandardMaterial3D> material;
        gd::Ref<EffectData::Flipbook> book;
        gd::Int frame = 0;                          // 1-based once running; 0 before its start delay ends
        gd::Int delay = 0;
        Vector3 velocity;                           // the drift per frame (Godot units)
    };

    class Light {
    public:
        gd::Ref<OmniLight3D> node;
        gd::Int left = LIGHT_FRAMES;
        gfx::Color colour;
    };

    gd::Ref<EffectData> data;
    gd::Arr<gd::Ref<EffectData::Flipbook>> character;   // sets 0 and 1: the fighters' flipbooks
    gd::Arr<gd::Ref<Sprite>> sprites;
    gd::Arr<gd::Ref<Light>> lights;
    gd::Ref<EffectObjectsView> objects;
    gd::Ref<Node3D> flipbooks;                      // the sprites and lights, hidden on steps that do not draw them
    gd::Arr<gd::Ref<Sprite>> _free;                 // ended sprites, reused by the next spawns
    gd::Arr<gd::Ref<OmniLight3D>> _free_lights;     // ended lights, hidden, reused by the next ones
    gd::Ref<QuadMesh> _quad;

    // Creates objects, flipbooks and _quad (declaration order).
    EffectsView();
    void setup(const gd::Ref<EffectData>& effects);
    // The fight's character flipbooks (sets 0 and 1) by the fighters' costume slots.
    void set_costumes(const gd::PI32& slots);
    // One simulation step: ends every effect on EFFECTS_CLEAR, advances the running ones when
    // EFFECTS_DRAW says so, starts the step's new ones, and shows them only if the step draws them.
    void step(const gd::Ref<SimEvents>& events);
    void clear();
    // EffectSpawn's drift: 0x1900 / 128 game units a frame along the hit direction (none without one).
    static Vector3 drift_of(const gd::PI32& direction);
    void _character(gd::Int set_index, const Vector3& at, const Vector3& drift = Vector3::ZERO());
    void _spawn(const gd::Ref<EffectData::Flipbook>& book, const Vector3& at, gd::Int delay,
                const Vector3& drift = Vector3::ZERO());
    gd::Ref<Sprite> _new_sprite();
    // An ended sprite goes back to the pool, hidden.
    void _release(const gd::Ref<Sprite>& s);
    void _light(const Vector3& at, const gfx::Color& colour);
    // An ended light's node goes back to the pool, hidden.
    void _release_light(const gd::Ref<Light>& l);
    // One frame of the running sprites and lights; the ended ones leave in order.
    void _advance();
    void _paint(const gd::Ref<Sprite>& s);
    void _paint_light(const gd::Ref<Light>& l);

    // Flipbook.texture (a getter over TexturePacks in the remake, not translated in EffectData): the
    // strip, or the chosen texture pack's replacement of it; null without a path.
    static gfx::TextureRef flipbook_texture(const gd::Ref<EffectData::Flipbook>& book);
};

}  // namespace t3
