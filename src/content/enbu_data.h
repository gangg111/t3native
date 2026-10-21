#pragma once
// remake/content/enbu_data.gd
// The attract demonstration as converted (`imported/enbu/enbu.json`): per demonstration the
// fighters' starting costume slots, the script and the model of each costume slot; the camera
// reel. See tools/remake_import/enbu.py.
#include <string>

#include "gd/gd.h"

namespace t3 {

class EnbuData {
public:
    class Demo {
    public:
        static inline const gd::Arr<gd::Int> COSTUME_EVENTS{0, 1};   // event kinds that change fighter 0 / 1's costume

        gd::Int number = 0;
        gd::PI32 start_costumes;
        gd::Arr<gd::PI32> events;                // [frame, kind, a, b, c] script events
        gd::Dict<gd::Int, std::string> models;   // costume slot → model folder name

        // The costume slots fighter `fighter` wears during the demonstration, the first one first.
        gd::PI32 costume_slots(gd::Int fighter);
    };

    std::string bank = "";
    gd::PI32 camera_reel;
    gd::Arr<gd::Ref<Demo>> demos;

    static gd::Ref<EnbuData> load_from(const std::string& path);
};

}  // namespace t3
