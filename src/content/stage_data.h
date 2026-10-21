#pragma once
// remake/content/stage_data.gd
// A converted stage: the panorama mesh (objects on the 6 x 6 grid), its atlas and the
// floor pattern. See docs/research/code/stages.md.
#include <array>
#include <string>

#include "gd/gd.h"
#include "gd/json.h"

namespace t3 {

class ArcadeStageData;

class StageData {
public:
    static constexpr gd::Int BASE_NEUTRAL = 128;   // the vertex colour that leaves a texel unchanged (0x80)

    gd::Int number = 0;
    std::string letter;
    std::string directory;
    gd::Int cell = 0;
    gd::Int panorama_height = 0;
    gd::Int floor_kind = 0;
    gd::Int floor_shade = 0;              // kind 0's distance shading factor (FloorDrawGrid; 0: none)
    gd::Int floor_fade = 0;               // the distance the shading table spans (game units)
    gd::Int floor_tile_size = 0;
    gd::Arr<gd::Json> floor_tiles;        // Array[Dictionary]: JSON objects as read
    std::string floor_pattern;            // the 10 x 10 pattern as drawn by the game
    std::string floor_pattern_smooth;     // the same with the tile seams smoothed
    gd::Int vertex_floats = 0;
    gd::PI32 panorama_extent;             // min x, max x, min z, max z of the panorama objects
    gd::Packed<float> panorama;           // PackedFloat32Array (raw floats via vec())
    // Tekken Ball's and Tekken Force's stages have a screen-space tile map instead of a panorama:
    // picture, tile (pixels), width and height (cells). (JSON object as read)
    gd::Json tile_map;
    // Tekken Force's item pictures (the levels with items): "picture" and "shadow" file names. (JSON object)
    gd::Json item;
    // The light rig (stages.md#lighting): back colour level, the fighters' base colour (their vertex
    // colours start from it; 0x80 = 1 per channel), main light colour and direction (4096-unit pitch
    // and yaw). `ambient` is the back colour's level in bytes (SetBackColor's argument) and
    // `ambient_level` the record's word it comes from, in GTE units of 4096 (`ambient << 4` plus the
    // low bits the flash ramp still sees).
    gd::Int ambient = 0;
    gd::Int ambient_level = 0;
    std::array<float, 3> base_colour{1.0f, 1.0f, 1.0f};   // Vector3, Vector3.ONE
    std::array<float, 4> light_colour{0.0f, 0.0f, 0.0f, 1.0f};   // Color (r, g, b, a)
    gd::Int light_pitch = 0;
    gd::Int light_yaw = 0;
    // View coverage: per direction around +x (counter-clockwise from above) the highest elevation
    // in degrees the backdrop covers without gaps, seen from `coverage_eye`.
    gd::PI32 coverage;
    gd::PI32 coverage_eye;
    // The arcade version of the stage (converted with --arcade), or null.
    gd::Ref<ArcadeStageData> arcade;

    static gd::Ref<StageData> load_from(const std::string& dir);
    static std::array<float, 4> _colour(const gd::Json& value);
};

}  // namespace t3
