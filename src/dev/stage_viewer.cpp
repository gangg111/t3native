// remake/app/stage_viewer.gd (its command line; no keys): see the header.
#ifndef __ANDROID__
#include "dev/stage_viewer.h"

#include <cmath>

#include "content/effect_data.h"
#include "content/fight_content.h"
#include "content/stage_data.h"
#include "core/camera/camera_view.h"
#include "gd/io.h"
#include "gfx/gl.h"
#include "gfx3d/renderer.h"
#include "platform/app.h"
#include "platform/host/host_options.h"
#include "platform/settings.h"
#include "presentation/camera/camera_rig.h"
#include "presentation/fighter/arcade_attachments.h"
#include "presentation/render_quality.h"
#include "presentation/scene/fight_view.h"
#include "presentation/stage/stage_view.h"
#include "presentation/world_space.h"

namespace t3 {

namespace {

class StageViewer : public App {
public:
    static constexpr int STAGES = 15;
    static constexpr gd::Int PROJECTION = 500;
    static constexpr gd::Int TURN_MAX = 0xFFF;
    static constexpr gd::Int SPOT_SPREAD = 800;   // kind 1's two light points, each side of the midpoint

    explicit StageViewer(const HostOptions& o) : o_(o) {}

    bool start(const std::string& data_root, const std::string&) override {
        // The backdrops setting the stage view reads (Settings.session["stage_backdrops"]).
        settings_.values = GameSettings::sanitized(gd::Json::parse("{}"));
        settings_.values.set("stage_backdrops", o_.backdrops == "playstation" ? std::string("playstation") : std::string("arcade"));
        GameSettings::autoload = &settings_;
        // The views that read assets by path themselves (GameApp::start does the same).
        ArcadeAttachments::assets_root = data_root;
        content_ = FightContent::load_from(data_root);
        effect_data_ = EffectData::load_from(gd::path_join(data_root, "effects"));
        view_ = gd::make<FightView>();
        view_->set_as_tree_root(true);
        int stage = std::max(0, std::min(STAGES - 1, o_.stage));
        view_->setup(content_->stage_number(stage), effect_data_, {}, gd::PI32(), gd::PI32());
        view_->stage_view->_notification(StageView::NOTIFICATION_ENTER_TREE);
        return true;
    }

    void resize(int w, int h, const SafeArea&) override { width_ = w, height_ = h; }

    // _physics_process (one step a frame) and _process.
    void frame(double) override {
        double angle = WorldSpace::radians(o_.yaw);
        auto camera = gd::make<CameraView>(o_.pitch & 0xFFF, o_.yaw & 0xFFF,
                                           static_cast<gd::Int>(std::lround(o_.distance * std::sin(angle))), -o_.camera_height,
                                           static_cast<gd::Int>(std::lround(-o_.distance * std::cos(angle))), PROJECTION);
        view_->rig->set_view(camera, true);
        view_->stage_view->follow(Vector3());
        view_->stage_view->set_floor_distance(static_cast<float>(o_.distance));
        view_->stage_view->set_backdrop_turn(o_.turn & TURN_MAX);
        view_->stage_view->step(o_.yaw & 0xFFF);
        _spotlight();
        view_->interpolate(1.0, gfx::Vec2(static_cast<float>(width_), static_cast<float>(height_)));
        if (!renderer_) renderer_ = std::make_unique<gfx3d::Renderer3D>(gfx3d::RenderSettings{1, 4096});
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width_, height_);
        gfx::Color c = view_->clear_colour;
        glClearColor(c.r, c.g, c.b, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        renderer_->render(*view_, *view_->rig->camera, width_, height_, 0);
    }

    // The spotlight floors' light points (FightView._spotlight): under the midpoint, kind 1 under
    // two fighters' places.
    void _spotlight() {
        gd::Int kind = view_->stage_view->floor_kind();
        if (kind <= 0 || kind >= FightView::SPOT_K.size()) {
            view_->stage_view->set_spotlight(0, Vector3(), Vector3(), 0, 0);
            return;
        }
        Vector3 a, b;
        if (kind == 1) {
            a = WorldSpace::point(-SPOT_SPREAD, 0, 0);
            b = WorldSpace::point(SPOT_SPREAD, 0, 0);
        }
        gd::Int limit = kind == 2 ? FightView::SPOT_LIMIT_MIDPOINT : FightView::SPOT_LIMIT;
        double k = static_cast<double>(FightView::SPOT_K[kind]) / (WorldSpace::UNITS_PER_METRE * WorldSpace::UNITS_PER_METRE);
        view_->stage_view->set_spotlight(kind, a, b, static_cast<float>(k), static_cast<float>(limit));
    }

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
    void stop() override { GameSettings::autoload = nullptr; }

private:
    HostOptions o_;
    GameSettings settings_;
    gd::Ref<FightContent> content_;
    gd::Ref<EffectData> effect_data_;
    gd::Ref<FightView> view_;
    std::unique_ptr<gfx3d::Renderer3D> renderer_;
    int width_ = 0, height_ = 0;
};

}  // namespace

App* make_stage_viewer(const HostOptions& options) { return new StageViewer(options); }

}  // namespace t3
#endif
