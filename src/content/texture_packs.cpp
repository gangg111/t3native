#include "content/texture_packs.h"

#include <unordered_map>

namespace t3 {

namespace {
std::unordered_map<std::string, gfx::TextureRef>& textures() {
    static std::unordered_map<std::string, gfx::TextureRef> t;
    return t;
}
}  // namespace

gfx::TextureRef TexturePacks::texture(const std::string& path) {
    auto& t = textures();
    auto it = t.find(path);
    if (it != t.end()) return it->second;
    gfx::TextureRef tex = gfx::load_texture(path);
    t[path] = tex;
    return tex;
}

std::string TexturePacks::chosen() { return ""; }

gfx::TextureRef TexturePacks::vram_sprite(const VramImage&, gd::Int, gd::Int, gd::Int, gd::Int, gd::Int, gd::Int, gd::Int,
                                          gd::Int, gd::Int) {
    return nullptr;   // no pack chosen (see the header's TODO)
}

void TexturePacks::clear() {
    textures().clear();
    generation += 1;
}

}  // namespace t3
