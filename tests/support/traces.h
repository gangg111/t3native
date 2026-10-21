#pragma once
// remake/tests/support/traces.gd: the pose traces exported by tools/research/export_traces.py.
#include <string>

#include "gd/gd.h"

namespace t3test {

constexpr gd::Int POSE_CHANNELS = 49;
constexpr gd::Int SLOTS = 18;

// One animation of a pose trace: per frame the pose vector and the 18 slot rotations.
struct PoseAnim {
    gd::Int offset = 0;
    gd::Arr<gd::PI32> poses;
    gd::Arr<gd::Arr<gd::PI32>> slots;   // per frame: 18 x PackedInt32Array(9)
};

gd::Arr<PoseAnim> read_pose(const std::string& path);

}  // namespace t3test
