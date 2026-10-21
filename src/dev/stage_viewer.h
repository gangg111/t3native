#pragma once
// `t3host --scene stages` (the remake's app/stage_viewer.gd): one stage's FightView without
// fighters, from a free camera about the fighters' midpoint, so that a stage (the arcade's scene,
// sky and floor, or the PlayStation's) can be compared with the remake's viewer in Godot:
//   godot --path remake --main-scene res://app/stage_viewer.tscn -- --stage=8 --yaw=400 ...
//   t3host --scene stages --stage 8 --yaw 400 ... --backdrops arcade --screenshot X.png --frames 30
namespace t3 {

struct HostOptions;
class App;

App* make_stage_viewer(const HostOptions& options);

}  // namespace t3
