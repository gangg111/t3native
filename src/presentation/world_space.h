#pragma once
// remake/presentation/world_space.gd: conversion from the game's world units to Godot space.
//
// The game uses x right, y down and z forward, in units of about one millimetre.
// Godot uses metres with y up and the camera looking along -z. The conversion
// (x, y, z) -> (x, -y, -z) / 1000 is a rotation by 180 degrees about x, so it keeps the
// handedness and the on-screen winding of the game's polygons.
//
// Vector3 / Vector4 are Godot's single-precision types (gfx3d/math3d.h), only forward-declared here so
// that views with their own local 3D helpers can use WorldSpace::radians without gfx3d's names.
#include "gd/gd.h"

namespace t3 {

class JointFrame;
struct Vector3;
struct Vector4;

class WorldSpace {
public:
    static constexpr double UNITS_PER_METRE = 1000.0;
    static constexpr double ANGLE_UNITS = 4096.0;   // the game's angle units per turn

    static Vector3 point(double x, double y, double z);
    static Vector3 point_i(const gd::PI32& v);
    // A game angle (4096 units per turn) in radians.
    static double radians(gd::Int angle);
    // The three rows of a joint's affine matrix in Godot space, applied to points in the
    // game's part-local units: G = C.[R / 4096 | t] / 1000 with C = diag(1, -1, -1).
    static gd::Arr<Vector4> joint_rows(const gd::Ref<JointFrame>& frame);
};

}  // namespace t3
