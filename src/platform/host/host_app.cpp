// The development host's app: the game with the host's command line.
#ifndef __ANDROID__
#include "app/game_app.h"
#include "dev/gfx3d_smoke.h"
#include "dev/stage_viewer.h"
#include "platform/host/host_options.h"

namespace t3 {

App* make_app(const HostOptions& o) {
    if (o.scene == "gfx3d_smoke") return make_gfx3d_smoke();
    if (o.scene == "stages") return make_stage_viewer(o);
    GameOptions options;
    options.replay = o.replay;
    options.replay_frames = o.replay_frames;
    options.seed = o.seed;
    options.speed = o.speed < 1 ? 1 : o.speed;
    options.touch = o.touch || !o.touch_edit.empty();
    options.touch_edit = o.touch_edit;
    options.replay_writes = o.replay_writes;
    options.pads = o.pads;
    options.skip_movies = o.no_movies;
    options.capturing = !o.screenshot.empty();
    return new GameApp(options);
}

}  // namespace t3
#endif
