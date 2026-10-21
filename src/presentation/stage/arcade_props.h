#pragma once
// remake/presentation/stage/arcade_props.gd
// The arcade stages' animated props (docs/research/arcade/stages.md#props), ported
// from tools/research/arcade_props_sim.py, which matches the arcade's own routines frame for frame
// (verify_arcade_props.py; tests/presentation/test_arcade_props.gd replays its trace here).
//
// Stage 3 turns its carousel (FUN_801E59FC, FUN_801E5AD0). Stage 11 flies a helicopter around
// the arena (FUN_801E3DE0 at a round's start, FUN_801E3D0C every frame it is shown) in one of
// four modes: 0 hovers and, once the camera has turned far, circles to stay in view; 1-3 fly
// scripted figures. Arcade bug #63 (game-bugs.md) kept mode 0 from ever circling; the circling
// runs here, as meant.
//
// The state is the game's: SVECTOR angles and positions (scene units, y down) per prop, and the
// helicopter's 16-bit variables.
#include <string>

#include "gd/gd.h"
#include "platform/pad_state.h"

namespace t3 {

class ArcadeProps {
public:
    static inline const std::string CAROUSEL = "carousel";
    static inline const std::string HELICOPTER = "helicopter";
    static constexpr gd::Int PROPS = 4;
    static constexpr gd::Int CAROUSEL_TURN = -5;
    static constexpr gd::Int CAROUSEL_FRAMES = 0x88;
    static constexpr gd::Int CAROUSEL_START = 0x955;
    static constexpr gd::Int WARMUP = 0x78;
    static constexpr gd::Int ROTOR = 0x29F;
    static constexpr gd::Int RADIUS = 0x1068;
    static constexpr gd::Int NEAR_TURN = 0x17C;
    static constexpr gd::Int FAR_TURN = 0x352;
    static inline const gd::Arr<gd::Int> NEAR_PROFILE{7, 507, 460, 11};   // top step, orbit, brake distance, turn rate
    static inline const gd::Arr<gd::Int> FAR_PROFILE{15, 1016, 793, 16};
    static constexpr gd::Int ANGLE_MASK = 0xFFF;
    // The arcade's input bits of the four attack buttons (FUN_8018F070 picks a pose with them in the
    // order FighterStartWinPose picks it with □ △ ✕ ○): LP, RP, LK, RK.
    static inline const gd::Dict<gd::Int, gd::Int> ARCADE_BUTTONS = [] {
        gd::Dict<gd::Int, gd::Int> d;
        d.set(PadState::SQUARE, 0x200);
        d.set(PadState::TRIANGLE, 0x100);
        d.set(PadState::CROSS, 0x40);
        d.set(PadState::CIRCLE, 0x20);
        return d;
    }();
    static constexpr gd::Int QUARTER = 0x400;

    std::string kind = "";
    gd::PI32 sine;                      // the arcade's 4096-entry table (stages/arcade_sine.json)
    gd::Arr<gd::PI32> angles;
    gd::Arr<gd::PI32> positions;
    gd::Arr<gd::Int> profile{0, 0, 0, 0};
    gd::Int accelerate = 0;
    gd::Int reverse = 0;
    gd::Int bob_dir = 0;
    gd::Int yaw_timer = 0;
    gd::Int yaw_dir = 0;
    gd::Int tilt_timer = 0;
    gd::Int tilt_dir = 0;
    gd::Int fa14 = 0;
    gd::Int phase_timer = 0;
    gd::Int flags = 0;
    gd::Int start_yaw = 0;
    gd::Int orbit = 0;
    gd::Int orbit_target = 0;
    gd::Int turn_left = 0;
    gd::Int bank = 0;
    gd::Int speed = 0;
    gd::Int step_size = 0;
    gd::Int turn_step = 0;
    gd::Int turn_rate = 0;
    gd::Int mode = 0;
    gd::Int warmup = 0;
    gd::Int state = 0;
    gd::Int timer = 0;
    gd::Int camera_ref = 0;
    gd::Int camera_last = 0;
    gd::Int slide_x = 0;
    gd::Int slide_z = 0;
    gd::Int axis = 0;
    gd::Int slide = 0;
    gd::Int bob_timer = 0;
    gd::Int radius = 0;
    gd::Int carousel_timer = 0;

    explicit ArcadeProps(const std::string& prop_kind = "", const gd::PI32& sine_table = gd::PI32());

    // The arcade's input word for a PlayStation pad's held buttons (the attack buttons only).
    static gd::Int arcade_buttons(gd::Int held);
    static gd::Int s16(gd::Int v);

    // The round's start: FUN_801E59FC or FUN_801E3DE0 (`buttons`: the first player's held buttons
    // in the arcade's input word, `counter` its frame counter; their low bits pick the mode).
    void start(gd::Int buttons = 0, gd::Int counter = 0);
    // One frame: the carousel turns always; the helicopter only on the frames it is shown, with the
    // camera's yaw (4096 units, the arcade's 0x802C6BD8).
    void step(gd::Int camera_yaw = 0);
    // The state as verify_arcade_props.py's frame_words: angles, positions, the variables (16-bit,
    // the port's field order), the manoeuvre profile.
    gd::PI32 words() const;

    // ---- stage 3 ----
    void _carousel_start();
    void _carousel_step();

    // ---- stage 11 ----
    void _helicopter_start(gd::Int buttons, gd::Int counter);
    void _helicopter_step(gd::Int camera_yaw);
    // FUN_801E3F68.
    void _watch_camera(gd::Int yaw);
    // x, z of the helicopter on the orbit at `angle` (sin and cos of -angle), rounded toward zero.
    void _orbit_position(gd::Int angle, gd::Int r = RADIUS);
    static gd::Int _scaled(gd::Int v, gd::Int r);
    // FUN_801E40C0.
    void _fly();
    // FUN_801E42DC (side 1) and its mirror FUN_801E4674 (side -1).
    void _manoeuvre(gd::Int side);
    // FUN_801E4A0C (mode 1).
    void _figure_1();
    // FUN_801E4E04 (mode 2).
    void _figure_2();
    // FUN_801E54B4 (mode 3).
    void _figure_3();
};

}  // namespace t3
