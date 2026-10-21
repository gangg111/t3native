#include "content/motion_bank.h"

#include "content/json_file.h"
#include "content/move_row.h"
#include "gd/io.h"

namespace t3 {

namespace {
// PackedInt64Array(row) from a JSON array: each float converted like Variant float -> int.
gd::PI64 ints64(const gd::Json& row) {
    gd::PI64 out;
    for (const gd::Json& v : row.items()) out.append(v.to_int());
    return out;
}

gd::Int key_int(const std::string& key) { return std::stoll(key); }
}  // namespace

gd::Ref<MotionBank> MotionBank::load_named(const std::string& folder, const std::string& bank_name) {
    std::string base = gd::path_join(folder, bank_name);
    gd::Json data = JsonFile::read(base + ".json.gz");
    auto bank = gd::make<MotionBank>();
    bank->name = data.get("name").is_string() ? data.get("name").str() : "";
    bank->type_code = data.has("type") ? data.get("type").to_int() : -1;
    for (const gd::Json& row : data.get("moves").items()) bank->moves.append(ints64(row));
    bank->move_index = ints64(data.get("move_index"));
    for (const auto& [key, entry] : data.get("anims").members()) {
        gd::Int first = entry[0].to_int();
        gd::Int frames = entry[1].to_int();
        bank->anims.set(key_int(key), gd::Vector2i(first, frames));
        if (entry.size() >= 4) {
            gd::Int first_byte = entry[2].to_int();
            gd::Int tag = entry[3].to_int();
            bank->anim_bytes.set(key_int(key), gd::Vector2i(first_byte, tag));
        }
    }
    bank->poses = DataFile::read(base + ".poses.bin.gz");
    bank->events = JsonFile::ints(data.get("events"));
    for (const gd::Json& choice : data.get("camera_choices").items()) bank->camera_choices.append(JsonFile::ints(choice));
    for (const auto& [key, value] : data.get("camera_streams").members()) {
        gd::PI32 s = JsonFile::ints(value);
        bank->camera_streams.set(key_int(key), gd::Vector3i(s[0], s[1], s[2]));
    }
    std::string camera_path = base + ".camera.bin";
    if (gd::file_exists(camera_path)) bank->camera_samples = gd::read_file(camera_path);
    for (const gd::Json& row : data.get("branches").items()) bank->branches.append(JsonFile::ints(row));
    bank->sounds = JsonFile::ints(data.get("sounds"));
    bank->empty_slots = JsonFile::ints(data.get("empty_slots"));
    std::string attacks_path = base + ".attacks.bin";
    if (gd::file_exists(attacks_path)) bank->attacks = gd::read_file(attacks_path);
    return bank;
}

gd::Arr<gd::Ref<MoveRow>> MotionBank::link(const gd::Ref<MotionBank>& common, const gd::PI32& builtins) {
    gd::Arr<gd::Ref<MoveRow>> rows;
    for (gd::Int r = 0; r < moves.size(); ++r) {
        rows.append(MoveRow::link(shared_from_this(), r, common, builtins));
    }
    return rows;
}

gd::PI32 MotionBank::pose(gd::Int offset, gd::Int frame) const {
    gd::Vector2i entry = anims.at(offset);
    gd::Int index = entry.x + gd::clampi(frame, 0, entry.y - 1);
    gd::PI32 out;
    out.resize(POSE_CHANNELS);
    gd::Int base = index * POSE_BYTES;
    for (gd::Int c = 0; c < POSE_CHANNELS; ++c) out[c] = poses.decode_s16(base + 2 * c);
    return out;
}

gd::PI32 MotionBank::camera_sample(gd::Int offset, gd::Int frame) const {
    gd::Vector3i entry = camera_streams.at(offset);
    gd::Int base = (entry.x + gd::clampi(frame, 0, entry.y - 1)) * CAMERA_BYTES;
    gd::PI32 out;
    out.resize(CAMERA_CHANNELS);
    for (gd::Int c = 0; c < CAMERA_CHANNELS; ++c) out[c] = camera_samples.decode_s16(base + 2 * c);
    return out;
}

}  // namespace t3
