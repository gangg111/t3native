// remake/content/enbu_data.gd
// The attract demonstration as converted (`imported/enbu/enbu.json`): per demonstration the
// fighters' starting costume slots, the script and the model of each costume slot; the camera
// reel. See tools/remake_import/enbu.py.
#include "content/enbu_data.h"

#include <string>
#include <utility>

#include "content/json_file.h"
#include "gd/json.h"

namespace t3 {

// The costume slots fighter `fighter` wears during the demonstration, the first one first.
gd::PI32 EnbuData::Demo::costume_slots(gd::Int fighter) {
    gd::PI32 out{gd::Int(start_costumes[fighter])};
    for (const gd::PI32& e : events) {
        if (e[1] == COSTUME_EVENTS[fighter] && !out.has(e[2])) {
            out.append(e[2]);
        }
    }
    return out;
}

gd::Ref<EnbuData> EnbuData::load_from(const std::string& path) {
    gd::Json data = JsonFile::read(path);
    gd::Ref<EnbuData> e = gd::make<EnbuData>();
    // data.get(key, default): a missing key gives the default.
    e->bank = data.has("bank") ? data["bank"].str() : std::string("");
    e->camera_reel = data.has("camera_reel") ? JsonFile::ints(data["camera_reel"]) : gd::PI32();
    if (data.has("demonstrations")) {
        for (const gd::Json& d : data["demonstrations"].items()) {
            gd::Ref<Demo> demo = gd::make<Demo>();
            demo->number = d["number"].to_int();
            demo->start_costumes = JsonFile::ints(d["start_costumes"]);
            for (const gd::Json& event : d["script"].items()) {
                demo->events.append(JsonFile::ints(event));
            }
            const gd::Json& models = d["models"];
            for (const std::pair<std::string, gd::Json>& kv : models.members()) {
                demo->models[std::stoll(kv.first)] = kv.second.str();
            }
            e->demos.append(demo);
        }
    }
    return e;
}

}  // namespace t3
