#include "traces.h"

#include "gd/io.h"
#include "platform/log.h"

namespace t3test {

gd::Arr<PoseAnim> read_pose(const std::string& path) {
    gd::PBytes data = gd::read_file(path);
    gd::Arr<PoseAnim> out;
    if (data.size() < 12 || data.slice(0, 4).get_string_from_ascii() != "T3PT" || data.decode_u32(4) != 1) {
        t3::Log::error("Traces: " + path + " is not a version 1 pose trace");
        return out;
    }
    gd::Int count = data.decode_u32(8);
    gd::Int pos = 12;
    for (gd::Int a = 0; a < count; ++a) {
        PoseAnim anim;
        anim.offset = data.decode_u32(pos);
        gd::Int frames = data.decode_u32(pos + 4);
        pos += 8;
        for (gd::Int f = 0; f < frames; ++f) {
            gd::PI32 pose;
            pose.resize(POSE_CHANNELS);
            for (gd::Int c = 0; c < POSE_CHANNELS; ++c) pose[c] = data.decode_s16(pos + 2 * c);
            pos += 2 * POSE_CHANNELS;
            gd::Arr<gd::PI32> frame_slots;
            for (gd::Int s = 0; s < SLOTS; ++s) {
                gd::PI32 m;
                m.resize(9);
                for (gd::Int i = 0; i < 9; ++i) m[i] = data.decode_s16(pos + 2 * i);
                pos += 18;
                frame_slots.append(m);
            }
            anim.poses.append(pose);
            anim.slots.append(frame_slots);
        }
        out.append(anim);
    }
    return out;
}

}  // namespace t3test
