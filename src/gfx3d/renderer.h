#pragma once
// Draws a tree of 3D nodes (nodes.h) through a Camera3D, as Godot's renderer does for the remake's
// scenes: the sun's shadow map (the first visible DirectionalLight3D with shadows, one orthographic
// cascade over the camera's view up to its maximum distance), the opaque pass (alpha scissor
// included), the transparent pass sorted back to front, into a linear multisampled buffer, resolved
// and encoded to sRGB (the linear tone mapper) into the target framebuffer. The 2D canvas draws over
// it afterwards.
#include <memory>

#include "gfx3d/nodes.h"

namespace t3::gfx3d {

struct RenderSettings {
    int msaa = 4;              // samples (1: none)
    int shadow_size = 2048;    // the shadow map's side
    float scale = 1.0f;        // Viewport.scaling_3d_scale: the 3D view at this share of the target's
                               // size, stretched bilinearly (SCALING_3D_MODE_BILINEAR)
};

class Renderer3D {
public:
    explicit Renderer3D(RenderSettings settings = {});
    ~Renderer3D();
    Renderer3D(const Renderer3D&) = delete;
    Renderer3D& operator=(const Renderer3D&) = delete;

    // Renders `root` (its visible nodes) through `camera` into framebuffer `target_fbo` (0: the
    // window), over its `width` x `height` pixels. Sets camera.viewport_size.
    void render(Node3D& root, Camera3D& camera, int width, int height, unsigned target_fbo = 0);

    // The GL context was lost: every GL object is forgotten and made again on the next render.
    static void graphics_lost();

    struct Stats {
        int draws = 0, shadow_draws = 0, programs_failed = 0;
    };
    const Stats& stats() const { return stats_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    RenderSettings settings_;
    Stats stats_;
};

}  // namespace t3::gfx3d
