#pragma once
// remake/content/arcade_stage_data.gd
// A stage's arcade version (stages/<letter>/arcade/, tools/remake_import/arcade.py): the
// polygonal scene, the tiled sky and the floor of the System 12 game, which the remake draws
// instead of the PlayStation's panorama when the Stage backdrops setting asks for them. See
// docs/research/arcade/stages.md.
#include <string>

#include "gd/gd.h"
#include "gd/json.h"

namespace t3 {

class ArcadeStageData {
public:
    static inline const std::string FILE = "arcade.json";
    static inline const std::string FADE_FILE = "arcade_floor_fade.json";   // in stages/, beside floor_fade.json
    static inline const std::string SINE_FILE = "arcade_sine.json";         // in stages/: the program's sine table (the props)

    std::string directory;
    gd::Packed<float> scene;              // vertex_floats per corner (arcade.py's scene.bin), PackedFloat32Array
    gd::Int vertex_floats = 0;
    std::string scene_texture;
    // The animated rectangle of the scene's atlas (stages 6 and 12, FUN_801E2114): x, y, w, h in
    // atlas texels (empty: none), its frames' picture (one frame below the other), their count and
    // the game frames each copy lasts.
    gd::PI32 scene_animation_rect;
    std::string scene_animation_texture;
    gd::Int scene_animation_frames = 0;
    gd::Int scene_animation_period = 0;
    std::string props_kind = "";          // ArcadeProps.CAROUSEL (stage 3), HELICOPTER (11) or none
    gd::Arr<gd::Packed<float>> props_meshes;   // per prop, scene.bin's format about its origin
    double scene_scale = 0.0;             // the scene is its TMD scaled by this about the fighters' midpoint ...
    double scene_y_scale = 0.0;           // ... and vertically by this too
    gd::Int scene_height = 0;             // game units below the midpoint
    gd::PI32 scene_extent;                // min x, max x, min z, max z (scene units)
    gd::Json sky;                         // arcade.json "sky" (stages.md#sky), JSON object as read
    gd::Int floor_kind = 0;
    gd::Int floor_shade = 0;
    gd::Int floor_fade = 0;
    gd::Int floor_tile_size = 0;
    std::string floor_pattern;
    std::string floor_pattern_smooth;

    static gd::Ref<ArcadeStageData> load_from(const std::string& stage_dir);
};

}  // namespace t3
