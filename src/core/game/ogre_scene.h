#pragma once
// remake/core/game/ogre_scene.gd
// The Ogre scene (arcade.ovl FUN_800B4168 set-up, FUN_800B4528 per frame; modes.md#match-flow),
// written from the verified port `tools/research/ogre_scene_sim.py`: the defeated human's fighter is
// reloaded as the stage-9 boss, both fighters stand at the origin, and a script in the Enbu
// event format plays: music track 5, the two scripted moves (Ogre lifts the boss), a fade to
// white from frame 192, the end at 256; then a white screen for two more frames. The camera
// plays the reel of Ogre's camera id 0x4B (CameraDirector.ogre_start), then CameraDirector
// takes over.
#include "gd/gd.h"
#include "core/game/flow_part.h"

namespace t3 {

class GameFlow;

class OgreScene : public FlowPart {
public:
    static constexpr gd::Int EVENT_END = -1;
    static constexpr gd::Int EVENT_MOVE_0 = 2;
    static constexpr gd::Int EVENT_MOVE_1 = 3;
    static constexpr gd::Int EVENT_FADE = 5;
    static constexpr gd::Int EVENT_MUSIC = 6;
    static constexpr gd::Int MUSIC_OGRE = 5;
    static constexpr gd::Int BLACK_LEVEL = 0xD0;
    static constexpr gd::Int FADE_MAX = 0x100;
    static constexpr gd::Int FADE_BASE = 0x100;
    static constexpr gd::Int CLOSING_FRAMES = 2;
    static constexpr gd::Int LETTERBOX_FROM = 5;
    static constexpr gd::Int FACING = 0x4000;
    static inline const gd::Arr<gd::Int> JIN_SLOTS{0x12, 0x13};      // the boss when the player is Heihachi
    static inline const gd::Arr<gd::Int> HEIHACHI_SLOTS{0x1A, 0x1B};
    static constexpr gd::Int LADDER_BOSS_COSTUME = 0x90 + 4 * 8 + 1;   // mode context: ladder entry 8's costume

    enum : gd::Int { START, SCRIPT, CLOSING };

    // A fighter's scripted move (10 bytes in the game).
    class ScriptedMove {
    public:
        gd::Int active = 0;
        gd::Int slot = 0;
        gd::Int end = 0;
        gd::Int frame = 0;
    };

    gd::Int state = START;                   // 0x800B4F18
    gd::Int frame = 0;                       // 0x800B4F1C
    gd::Arr<gd::PI32> events;
    gd::Int cursor = 0;                      // 0x800B4F14: the next event
    bool fade_on = false;
    gd::Int fade_level = 0;
    gd::Int fade_speed = 0;
    bool black = false;                      // 0x800B4F50: the stage is no longer drawn
    bool letterbox = false;                  // from frame 5 only the top 100 lines are cleared
    gd::Int fade_drawn = -1;                 // this frame's FUN_8004E2E8 level, −1 none
    bool reel_over = false;                  // 0x800B4F48: the camera reel has ended
    gd::Arr<gd::Ref<ScriptedMove>> moves;    // 0x800B4F34: [ScriptedMove.new(), ScriptedMove.new()], made in the constructor

    // FlowPart's _init(flow): OgreScene has no _init of its own; the constructor makes `moves`.
    explicit OgreScene(GameFlow* flow);

    // FUN_800B4168: the scene's set-up.
    void setup();
    // FUN_800B4528: one frame; true when the scene is over.
    bool step();
    // The scene is left: the fight's fighters are drawn again.
    void end();
    // One event; true at the end of the script.
    bool _event(const gd::PI32& e);
    // The reel until it ends, then the director (with g_cameraPhase as the fight left it).
    void _camera();
    // Each scripted move one pose frame on (its frame events without sounds), then animated.
    void _step_moves();
};

}  // namespace t3
