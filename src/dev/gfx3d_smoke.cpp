// `t3host --scene gfx3d_smoke`: a fixed scene for checking the 3D renderer against Godot (the same
// scene built in tools/godot_smoke/main.gd): a grey floor, a red cube, a sun with shadows and the
// stage environment's ambient light. Its lit, shadowed and cube-top pixels are compared with Godot's.
#include "dev/gfx3d_smoke.h"

#include "gfx/gl.h"
#include "gfx3d/nodes.h"
#include "gfx3d/renderer.h"

namespace t3 {

namespace {

// A quad's two triangles, clockwise seen from the side `normal` points to (Godot's front faces).
void quad(MeshArrays& a, Vector3 p0, Vector3 p1, Vector3 p2, Vector3 p3, Vector3 normal) {
    // p0..p3 counter-clockwise seen from outside: reversed for clockwise.
    for (const Vector3& p : {p0, p2, p1, p0, p3, p2}) {
        a.vertex.push_back(p);
        a.normal.push_back(normal);
    }
}

class Gfx3dSmoke : public App {
public:
    bool start(const std::string&, const std::string&) override {
        root = gd::make<Node3D>();
        root->set_as_tree_root(true);
        camera = gd::make<Camera3D>();
        camera->fov = 50;
        camera->near = 0.05f;
        camera->far = 100;
        camera->transform = Transform3D(Basis(), Vector3(0, 3, 6)).looking_at(Vector3(0, 0.5f, 0));
        auto env = gd::make<Environment>();
        env->background_mode = Environment::BG_COLOR;
        env->background_color = gfx::Color::black();
        env->ambient_light_source = Environment::AMBIENT_SOURCE_COLOR;
        env->ambient_light_color = gfx::Color(0.3f, 0.3f, 0.3f);
        env->ambient_light_energy = 0.55f;
        camera->environment = env;
        root->add_child(camera);

        auto floor_mesh = gd::make<ArrayMesh>();
        MeshArrays f;
        quad(f, {-10, 0, 10}, {10, 0, 10}, {10, 0, -10}, {-10, 0, -10}, {0, 1, 0});
        floor_mesh->add_surface_from_arrays(Primitive::TRIANGLES, f);
        auto floor_mat = gd::make<StandardMaterial3D>();
        floor_mat->albedo_color = gfx::Color(0.6f, 0.6f, 0.6f);
        floor_mesh->surface_set_material(0, floor_mat);
        auto floor_node = gd::make<MeshInstance3D>();
        floor_node->mesh = floor_mesh;
        root->add_child(floor_node);

        auto cube_mesh = gd::make<ArrayMesh>();
        MeshArrays c;
        float h = 0.5f;
        quad(c, {-h, h, h}, {h, h, h}, {h, h, -h}, {-h, h, -h}, {0, 1, 0});
        quad(c, {-h, -h, -h}, {h, -h, -h}, {h, -h, h}, {-h, -h, h}, {0, -1, 0});
        quad(c, {-h, -h, h}, {h, -h, h}, {h, h, h}, {-h, h, h}, {0, 0, 1});
        quad(c, {h, -h, -h}, {-h, -h, -h}, {-h, h, -h}, {h, h, -h}, {0, 0, -1});
        quad(c, {h, -h, h}, {h, -h, -h}, {h, h, -h}, {h, h, h}, {1, 0, 0});
        quad(c, {-h, -h, -h}, {-h, -h, h}, {-h, h, h}, {-h, h, -h}, {-1, 0, 0});
        cube_mesh->add_surface_from_arrays(Primitive::TRIANGLES, c);
        auto cube_mat = gd::make<StandardMaterial3D>();
        cube_mat->albedo_color = gfx::Color(0.9f, 0.3f, 0.2f);
        cube_mesh->surface_set_material(0, cube_mat);
        auto cube = gd::make<MeshInstance3D>();
        cube->mesh = cube_mesh;
        cube->transform.origin = {0, 0.5f, 0};
        root->add_child(cube);

        auto sun = gd::make<DirectionalLight3D>();
        sun->light_energy = 1.1f;
        sun->shadow_enabled = true;
        sun->directional_shadow_max_distance = 30;
        sun->transform.basis = Basis::looking_at(Vector3(-1, -2, -0.5f), Vector3(0, 0, -1));
        root->add_child(sun);
        renderer = std::make_unique<gfx3d::Renderer3D>(gfx3d::RenderSettings{1, 4096});
        return true;
    }
    void resize(int w, int h, const SafeArea&) override { width = w, height = h; }
    void frame(double) override { renderer->render(*root, *camera, width, height, 0); }
    void graphics_lost() override { gfx3d::Renderer3D::graphics_lost(); }
    void pointer(PointerEvent, const Pointer&) override {}
    void key(int, bool) override {}
    void pad(int, PadButton, bool) override {}
    void pad_axis(int, int, float) override {}
    void back() override {}
    void paused(bool) override {}
    void audio(float* out, int frames, int) override {
        for (int i = 0; i < frames * 2; ++i) out[i] = 0;
    }
    void stop() override {}

private:
    gd::Ref<Node3D> root;
    gd::Ref<Camera3D> camera;
    std::unique_ptr<gfx3d::Renderer3D> renderer;
    int width = 0, height = 0;
};

}  // namespace

App* make_gfx3d_smoke() { return new Gfx3dSmoke(); }

}  // namespace t3
