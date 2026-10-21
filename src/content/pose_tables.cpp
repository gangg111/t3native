#include "content/pose_tables.h"

#include "content/json_file.h"

namespace t3 {

gd::Ref<PoseTables> PoseTables::load_from(const std::string& path) {
    gd::Json data = JsonFile::read(path);
    auto t = gd::make<PoseTables>();
    t->sin_table = JsonFile::ints(data["sin"]);
    t->sqrt_table = JsonFile::ints(data["sqrt"]);
    t->euler_slots = JsonFile::ints(data["euler_slots"]);
    t->limb_basis = JsonFile::ints(data["limb_basis"]);
    t->skeleton_offsets = _triples(JsonFile::ints(data["skeleton_offsets"]));
    t->limb_offsets = _triples(JsonFile::ints(data["limb_offsets"]));
    return t;
}

gd::Arr<gd::PI32> PoseTables::_triples(const gd::PI32& values) {
    gd::Arr<gd::PI32> out;
    for (gd::Int i = 0; i < values.size(); i += 3) out.append(gd::PI32{values[i], values[i + 1], values[i + 2]});
    return out;
}

}  // namespace t3
