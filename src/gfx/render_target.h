#pragma once
// A colour texture with its framebuffer: what a Godot SubViewport renders its 2D content into
// (Viewport.get_texture()). The canvas draws into it between bind() and unbind(); its rows run
// bottom-up as GL's (row 0 is the bottom of what was drawn).
#include "gfx/texture.h"

namespace t3::gfx {

class RenderTarget {
public:
    RenderTarget(int width, int height);
    ~RenderTarget();
    RenderTarget(const RenderTarget&) = delete;
    RenderTarget& operator=(const RenderTarget&) = delete;

    int width() const { return texture_ ? texture_->width() : 0; }
    int height() const { return texture_ ? texture_->height() : 0; }
    const TextureRef& texture() const { return texture_; }
    unsigned fbo() const { return fbo_; }
    // Draws into the target from here on (the framebuffer and its viewport).
    void bind();
    // Back to the window's framebuffer.
    static void unbind();
    // The GL context was lost: the framebuffer's name is forgotten, never deleted (as it is anyway
    // once the context generation moved on).
    void forget() { fbo_ = 0; }
    // Made under an older GL context: the owner makes a new target.
    bool stale() const { return generation_ != gl_generation(); }

private:
    TextureRef texture_;
    unsigned fbo_ = 0;
    unsigned generation_ = gl_generation();
};

}  // namespace t3::gfx
