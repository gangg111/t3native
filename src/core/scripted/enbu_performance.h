#pragma once
// remake/core/scripted/enbu_performance.gd
// The attract demonstration's performance (`enbu.ovl` FUN_800D3D64, one call per frame;
// modes.md#enbu-attract-demonstration).
//
// The first step sets both fighters up at the origin facing +x on their starting costumes and
// starts the camera reel; the next five steps wait for the music; then every step runs the due
// script events, steps each scripted move one pose frame (with its frame events), steps the
// camera reel and advances the fade. The step that runs the end event returns true. Each script
// frame's background (FUN_8006DAB4) turns the panorama with the camera (BackdropTurn): GameFlow
// hands the game's own turn state, round state and camera in.
//
// Game bug #49 (fighter 0's frame events fire twice while fighter 1 plays a move) is not
// reproduced: each fighter's events fire once.
#include "gd/gd.h"
#include "content/enbu_data.h"     // EnbuData::Demo
#include "core/sim_events.h"       // SimEvents::Event

namespace t3 {

class MotionSet;
class FighterTables;
class PoseTables;
class CharacterModel;
class CameraReel;
class CameraView;
class BackdropTurn;
class ScriptedFighter;

class EnbuPerformance {
public:
    enum Phase : gd::Int { SETUP, MUSIC_WAIT, SCRIPT };

    static constexpr gd::Int MUSIC_WAIT_FRAMES = 4;   // the wait counts down from 4 to −1
    static constexpr gd::Int FACING = 0x4000;         // both fighters face +x (facing and heading)
    static constexpr gd::Int FADE_MAX = 0x100;
    static constexpr gd::Int FADE_BASE = 0x100;       // FUN_8004E2E8's neutral level; the fade brightens above it
    static constexpr gd::Int EVENT_END = -1;
    static constexpr gd::Int EVENT_COSTUME_0 = 0;
    static constexpr gd::Int EVENT_COSTUME_1 = 1;
    static constexpr gd::Int EVENT_MOVE_0 = 2;
    static constexpr gd::Int EVENT_MOVE_1 = 3;
    static constexpr gd::Int EVENT_FADE = 5;

    gd::Ref<EnbuData::Demo> demo;
    gd::Ref<MotionSet> motions;
    gd::Ref<FighterTables> fighter_tables;
    gd::Ref<PoseTables> pose_tables;
    gd::Dict<gd::Int, gd::Ref<CharacterModel>> models;   // costume slot → CharacterModel
    gd::Ref<CameraReel> reel;
    gd::Int projection = 500;                // H: CameraReset sets 500 and clamps to [500, 500]

    gd::Int phase = Phase::SETUP;
    gd::Int wait = 0;
    gd::Int frame = 0;                       // script frame counter (0x8010C41C)
    gd::Int cursor = 0;                      // next script event
    gd::Arr<gd::Ref<ScriptedFighter>> fighters;   // [ScriptedFighter.new(), ScriptedFighter.new()], made in the constructor
    bool fade_on = false;
    gd::Int fade_level = 0;
    gd::Int fade_speed = 0;
    gd::Int fade_drawn = -1;                 // the FUN_8004E2E8 level drawn this frame, −1 for none
    gd::Ref<CameraView> camera;              // after this step's reel step (g_camera)
    gd::Ref<CameraView> camera_drawn;        // the view this frame's picture uses (CameraFrame)
    bool drawn = false;                      // the stage and fighters were drawn this frame
    gd::Ref<SimEvents> events;               // SimEvents.new(), made in the constructor
    bool finished = false;
    gd::Ref<BackdropTurn> backdrop;          // BackdropTurn.new(), made in the constructor: the panorama's turn (GameFlow: the fight's)
    gd::Int round_state = 0;                 // 0x80097350 as the last fight left it
    gd::Ref<CameraView> setup_view;          // CameraView.new(0, 0, 0, 0, 0, 500), made in the constructor: g_camera when the performance starts

    EnbuPerformance(const gd::Ref<EnbuData::Demo>& demonstration, const gd::Ref<MotionSet>& motion_set,
        const gd::Ref<FighterTables>& tables, const gd::Ref<PoseTables>& poses,
        const gd::Dict<gd::Int, gd::Ref<CharacterModel>>& costume_models, const gd::Ref<CameraReel>& camera_reel);

    // One frame; true on the frame that ran the end event. `music_ready`: FUN_8006BCC8's answer
    // this frame (the music wait only counts down once the music is ready).
    bool step(bool music_ready = true);
    void _setup();
    bool _script_frame();
    void _run_event(const gd::PI32& e);
    void _step_fighter(const gd::Ref<ScriptedFighter>& sf);
    // FighterSetupModel: the fighter now uses the model of its costume slot. The joint blocks
    // keep the last frame's joints until the next animated frame.
    void _bind_model(const gd::Ref<ScriptedFighter>& sf);
    // FUN_8003AA6C → FighterAnimate: hands, pose, root and joints of the current frame.
    void _animate(const gd::Ref<ScriptedFighter>& sf);
    // Where an event happens: the joint's translation, or the root for dust. MoveEvents runs
    // before FighterAnimate, so these are the joints of the fighter's previous animated frame.
    gd::PI32 _event_point(const gd::Ref<ScriptedFighter>& sf, const gd::Ref<SimEvents::Event>& e);
};

}  // namespace t3
