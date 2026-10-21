#pragma once
// remake/content/effect_data.gd
// Flipbook strips and the landing dust layout (`imported/effects/effects.json`,
// tools/remake_import/effects.py).
#include <array>
#include <string>

#include "gd/gd.h"
#include "gd/json.h"

namespace t3 {

class EffectData {
public:
    class Flipbook {
    public:
        std::string name;
        std::string path;
        // GDScript `var texture: Texture2D` (a getter over TexturePacks) is presentation only and
        // is not translated: the native renderer resolves `path` itself.
        gd::Int frames = 0;
        bool additive = false;
        std::array<float, 4> light{0.0f, 0.0f, 0.0f, 1.0f};   // Color: the dynamic light of character sets; black for none
    };

    gd::Arr<gd::Ref<Flipbook>> system;      // set 3: hit, hit without damage, guard, dust
    gd::Arr<gd::Ref<Flipbook>> character;   // sets 0 and 1 (the demonstration's own copies)
    std::string directory;
    gd::Json objects;                       // the effect objects' page, palettes, models and UV tables (JSON object as read)
    gd::Json _costume_entries;              // costume slot (string) -> flipbook entry of the fight (JSON object)
    gd::Dict<gd::Int, gd::Ref<Flipbook>> _costumes;   // costume slot -> Flipbook, loaded when first used
    gd::Arr<gd::Vector3i> dust_offsets;
    gd::Int dust_y = 0;
    gd::Int dust_interval = 0;
    gd::Ref<Flipbook> dust;

    static gd::Ref<EffectData> load_from(const std::string& dir);

    // The character flipbook a costume slot uploads in a fight (sets 0 and 1), null if none.
    gd::Ref<Flipbook> costume(gd::Int slot) const;

    static gd::Ref<Flipbook> _flipbook(const std::string& dir, const gd::Json& f);
};

}  // namespace t3
