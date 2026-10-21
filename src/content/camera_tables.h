#pragma once
// remake/content/camera_tables.gd
// Constant tables of the camera code, converted from the game executable
// (`imported/tables/camera.json`, camera.md): the octant table of `Atan2Units4096` and the
// director's presets, choice lists, hit cameras, round intro offsets and winner camera choices.
#include <string>

#include "gd/gd.h"

namespace t3 {

class CameraTables {
public:
    // A hit camera script (camera.md#hit-cameras).
    class HitScript {
    public:
        gd::Int kind = -1;          // 0 preset-style framing, 1 contact-point view, -1 none
        gd::Int frames = 0;
        gd::Int blend_in = 0;
        gd::Int blend_out = 0;
        gd::Int start_distance = 0;
        gd::Int end_distance = 0;
        gd::PI32 pitches;           // pitch offsets (<< 6: 18-bit angles)
        gd::PI32 yaws;
    };

    gd::PI32 atan_table;
    gd::Arr<gd::PI32> presets;               // blend in, blend out, pitch offsets[4], yaw offsets[4]
    gd::Int outside_blend_out = 0;           // the word FUN_80067C8C reads for non-preset scripts
    gd::PI32 preset_index;                   // preset index + 1 per preset script value
    gd::Arr<gd::PI32> choices;               // EXE choice list: (kind, weight, script)
    gd::Arr<gd::PI32> hit_rows;              // (bank type, move slot, list id)
    // Untyped Array in GDScript; each element is an Array[PackedInt32Array].
    gd::Arr<gd::Arr<gd::PI32>> hit_lists;    // per list id - 300: records (cumulative weight out of 0x1000, script per rating x4)
    gd::Arr<gd::Ref<HitScript>> hit_scripts;
    gd::Arr<gd::PI32> intro_offsets;
    gd::PI32 intro_offset_ball;
    gd::PI32 attract_styles;                 // the demonstration camera's styles (FUN_800693E8)
    gd::PI32 attract_pitches;                // the circling camera's pitches (FUN_80068E90)
    gd::PI32 attract_speeds;                 // its yaw steps
    gd::PI32 winner_lateral;
    gd::PI32 winner_forward;
    gd::PI32 winner_height;

    static gd::Ref<CameraTables> load_from(const std::string& path);
};

}  // namespace t3
