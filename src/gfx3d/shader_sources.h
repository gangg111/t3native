#pragma once
// The shaders of t3native/shaders/ (the remake's, by their `res://` paths), built into the program
// by cmake/embed_shaders.cmake.
#include <map>
#include <string>

namespace t3::gfx3d {

const std::map<std::string, std::string>& shader_sources();

}  // namespace t3::gfx3d
