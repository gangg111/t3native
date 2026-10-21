#pragma once
// remake/core/fighter/fighter_hands.gd
// The hand and face channels of a fighter: `HandFaceCommand` (0x800346F8) sets a target shape
// and `FighterHandPoses` (0x80034BC4) moves towards it every animated frame and picks the
// vertex variant of each hand part (3dmk-models.md#vertex-variants).
//
// Fields: `+0x127C` current, `+0x1280` target, `+0x1284` rate per channel; `+0x128E` the face
// speed and `+0x1290` the face (shapes 10 and 11: open or closed eyes). True Ogre (character
// 0x14) animates one level `+0x1296` (target `+0x1298`, rate `+0x129A`) and his wings
// (`+0x128B` frame, `+0x128C` phase, `+0x128D` first step) instead of two hands.
#include "gd/gd.h"

namespace t3 {

class FighterTables;

class FighterHands {
public:
    static constexpr gd::Int CHANNELS = 2;
    static constexpr gd::Int FIST = 0x200;              // channel value of the closed fist; shapes 2–7 lie above it
    static constexpr gd::Int FACE_OPEN = 10;
    static constexpr gd::Int FACE_CLOSED = 11;
    static constexpr gd::Int OGRE_LEVEL = 0x100;

    gd::PI32 current{0, 0};
    gd::PI32 target{0, 0};
    gd::PI32 rate{0, 0};
    gd::PI32 variant{0, 0};   // the vertex variant each hand part shows
    gd::Int face = 0;         // 0 open eyes, 1 closed (shapes 10 / 11)
    gd::Int face_speed = 0;
    gd::Int ogre_level = 0;
    gd::Int ogre_target = 0;
    gd::Int ogre_rate = 0;
    gd::Int wing_frame = 0;
    gd::Int wing_phase = 0;
    gd::Int wing_first = 0;
    gd::Int wing_variant = 0;

    // HandFaceCommand for `hands` (bit 0 channel 0, bit 1 channel 1), a shape and a speed: shapes
    // 0 and 1 blend between open hand and fist over `speed + 1` frames, other shapes are set at once.
    void command(gd::Int hands, gd::Int shape, gd::Int speed, gd::Int char_id = 0);

    // FighterHandPoses for an animated frame (`air_free`: the fighter's +0x128A).
    void update(gd::Int char_id = 0, gd::Int air_free = 0, const gd::Ref<FighterTables>& tables = nullptr);

    void _approach(gd::Int c);

    // True Ogre's wings: folded (phase 0) they open while he is airborne outside reactions; open
    // (phase 1) they beat, one step per frame (two on the first).
    void _update_wings(gd::Int air_free, const gd::Ref<FighterTables>& tables);

    static gd::Int variant_of(gd::Int value);
};

}  // namespace t3
