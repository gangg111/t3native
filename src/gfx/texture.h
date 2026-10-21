#pragma once
// A GL texture made from an RGBA8 image (Godot's ImageTexture / Texture2D for the views).
#include <memory>
#include <string>
#include <vector>

#include "gfx/image.h"

namespace t3::gfx {

// Counts GL contexts (1 for the first): a GL object made under an older one is forgotten, never
// deleted (its name may belong to an object of the new context). Advanced when the context is lost
// (Renderer3D::graphics_lost, gfx3d::gl_generation() is this counter).
unsigned& gl_generation();
// Native: the textures uploaded and the programs linked so far (the performance log's spike notes).
unsigned& gl_uploads();
unsigned& gl_programs();

class Texture {
public:
    // `keep`: the texture keeps a copy of the image and makes itself again after a context loss.
    explicit Texture(const Image& image, bool mipmaps = false, bool keep = false);
    // An empty RGBA8 texture of w x h (a render target's colour, Godot's ViewportTexture).
    Texture(int width, int height);
    // An RGBA32F texture of w x h from `rgba` (4 floats a texel; Image.FORMAT_RGBAF), read with
    // texelFetch (not filterable). It keeps the floats and makes itself again after a context loss.
    Texture(int width, int height, std::vector<float> rgba);
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    // Replaces the pixels (same size), e.g. a movie frame.
    void update(const Image& image);
    // Builds the mipmaps once, for a sampler that filters with them (the 3D shaders' *_mipmap hints).
    void ensure_mipmaps();
    // The GL name to bind. A texture of a lost context made from a file (load_texture) or with a kept
    // image is made again here; any other is 0 (nothing: its owner makes a new one, see stale()).
    unsigned id() const;
    int width() const { return width_; }
    int height() const { return height_; }
    bool has_mipmaps() const { return mipmaps_; }
    // Made under an older GL context (and not able to make itself again).
    bool stale() const { return generation_ != gl_generation() && path_.empty() && kept_.empty() && floats_.empty(); }
    // The PNG it was read from (load_texture), else empty.
    const std::string& path() const { return path_; }

    // The binds of stale textures so far (0 when every view made its textures again).
    static unsigned stale_binds();

private:
    friend std::shared_ptr<Texture> load_texture(const std::string& path);
    void _upload(const Image& image) const;
    bool _live() const;   // current, or made again; false: stale
    mutable unsigned id_ = 0;
    mutable unsigned generation_ = gl_generation();
    int width_ = 0, height_ = 0;
    mutable bool mipmaps_ = false;
    std::string path_;
    Image kept_;
    std::vector<float> floats_;   // an RGBA32F texture's texels
};

using TextureRef = std::shared_ptr<Texture>;

// A texture from a PNG file, cached by path (null when it cannot be read).
TextureRef load_texture(const std::string& path);
// Forgets the cache (the pictures changed).
void clear_texture_cache();

}  // namespace t3::gfx
