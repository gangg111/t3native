#pragma once
// remake/core/camera/camera_director.gd
// The fight camera (camera.md): `CameraDirector` (0x800633C0) with its five camera sources,
// the two-fighter framing, the round intro, throw presets and camera streams, hit cameras and
// the round-end cameras. Integer port of the game's code (the framing and blending follow the
// verified `tools/research/camera_sim.py`); the state is the director's globals, named after the
// documentation with the address in the comment.
//
// Game bugs 1, 2, 6, 53, 54 and 55 are not reproduced (remake-plan.md#original-bugs).
//
// The demonstration fight (mode 6, or any mode while the attract flag is set) has its own
// camera (FUN_800693E8). Tekken Ball frames the ball too (a third target, at pitch and yaw 0);
// Tekken Force has its own side-scrolling camera (force.ovl, TekkenForce).
//
// The intro counter and the winner style persist across matches, as in the game: GameFlow keeps
// one director for the whole session (the flow traces compare both).
#include "content/camera_tables.h"   // CameraTables::HitScript
#include "core/fighter/bank_type.h"  // BankType::OGRE in a constant
#include "gd/gd.h"

namespace t3 {

class FightState;
class FightTables;
class CameraView;
class MoveRow;
class FighterState;
class MotionBank;

class CameraDirector {
public:
    static constexpr gd::Int SOURCE_FIGHT = 0;
    static constexpr gd::Int SOURCE_SCRIPT = 1;           // throw presets and hit cameras
    static constexpr gd::Int SOURCE_STREAM = 2;           // round intro and camera streams
    static constexpr gd::Int SOURCE_OUTPUT = 3;           // the blend result
    static constexpr gd::Int SOURCE_TRANSITION = 4;       // a frozen view fading into the fight camera
    static constexpr gd::Int SOURCES = 5;
    static constexpr gd::Int DISTANCE = 0x158D;           // the fight camera's distance
    static constexpr gd::Int MIN_DISTANCE = 800;
    static constexpr gd::Int MARGIN = 0x96;
    static constexpr gd::Int SPREAD_LIMIT = 0x119;
    static constexpr gd::Int SWING_CAP = 0xDDD0;
    static constexpr gd::Int SWING_KICK = 0x600;
    // Untyped Array of [camera byte, limit] pairs: every element an int, indexed.
    static inline const gd::Arr<gd::Arr<gd::Int>> SWING_LIMITS{{0x27, 0x200}, {0x28, 0x180}, {0x29, 0x80}, {0x2A, 0x40}};
    static constexpr gd::Int STREAM_H = 0x226;            // projection plane of camera stream samples
    static constexpr gd::Int STREAM_WEIGHT_FULL = 0x140;
    static constexpr gd::Int STREAM_BASE = 1LL << 40;     // stream scripts: STREAM_BASE | player << 16 | section-8 offset
    static constexpr gd::Int PRESET_SCRIPTS = 0x1E;
    static constexpr gd::Int CAMERA_BYTE_CUT = 9;         // move camera byte that cancels a cinematic camera
    static constexpr gd::Int CAMERA_BYTE_KEEP = 0x25;
    static constexpr gd::Int CAMERA_BYTE_STREAM = 0x63;
    static constexpr gd::Int CAMERA_BYTE_SKIP = 0x53;     // 'S': bank type 4 tracks its target on odd frames only
    static constexpr gd::Int FIRST_BANK_ID = 0x2B;        // camera ids from here on are in the fighter's bank
    static constexpr gd::Int REJECTED_IDS = 0x19;         // ids 0x19-0x2A have no camera
    static constexpr gd::Int REJECTED_COUNT = 0x12;
    static constexpr gd::Int HIT_LIST_BASE = 300;
    static constexpr gd::Int HIT_DISTANCE_H = 0x2EE;
    static constexpr gd::Int INTRO_ROUND1 = 0x78;
    static constexpr gd::Int INTRO_LATER = 0x35;
    static constexpr gd::Int OGRE_CAMERA_ID = 0x4B;       // the Ogre scene's camera id in Ogre's bank
    static constexpr gd::Int OGRE_BANK_TYPE = BankType::OGRE;

    // The director's sub-state within CameraPhase.FIGHT (camera.md#cinematic-cameras).
    enum Cinematic : gd::Int {
        IDLE = 1,                  // fight camera; starts a throw or hit camera
        PRESET = 2,                // a throw preset at full weight until the throw ends
        PRESET_IN = 3,             // blending from the fight camera into the preset
        PRESET_OUT = 4,            // blending back to the fight camera
        TRANSITION = 5,            // a frozen view fading into the fight camera
        STREAM = 6,                // a camera stream, weighted by its own channel
        HIT = 8,                   // a hit camera for its hold time
    };

    // Per-source framing state (0x800A06D0 + 0x30 * source).
    class SourceState {
    public:
        gd::Int phase = 0;                 // +0x00 yaw swing: 0 idle, 1 swinging
        gd::Int moved = 0;                 // +0x02 the camera moved more than about 31 units
        gd::Int cleared_a = 0;             // +0x04
        gd::Int cleared_b = 0;             // +0x06
        gd::Int frames = 0;                // +0x08
        gd::Int angle = 0;                 // +0x0C swing offset
        gd::Int speed = 0;                 // +0x10
        gd::Int pitch = 0;                 // +0x14
        gd::Int pitch_target = 0;          // +0x18
        gd::Int pitch_step = 0;            // +0x1C
        gd::Int error = 0;                 // +0x20 last axis error
        gd::Int depth_step = 0;            // +0x24
        gd::Int depth = 0;                 // +0x28 vertical back-off
        gd::Int lateral = 0;               // +0x2C horizontal back-off
    };

    // The working state of one CameraFrameFighters call: the targets in view space around the orbit
    // centre (`u` across, `v` up, `z` along the view, `depth` along the ground) and the camera's
    // offsets from it (`lateral` across, `back` along the view, `vertical` up).
    class Framing {
    public:
        gd::Int pitch = 0;
        gd::Int yaw = 0;
        gd::Int distance = 0;
        gd::Int restart = 0;
        gd::Int src = 0;
        gd::Int sp = 0;                    // sine and cosine of -pitch and -yaw
        gd::Int cp = 0;
        gd::Int sy = 0;
        gd::Int cy = 0;
        gd::Int mx = 0;                    // the orbit centre
        gd::Int mz = 0;
        gd::Int cam_y = 0;
        gd::PI32 u{0, 0};
        gd::PI32 v{0, 0};
        gd::PI32 z{0, 0};
        gd::PI32 depth{0, 0, 0};           // both targets, then the camera
        gd::PI32 ball;                     // Tekken Ball: u, depth, v, world y
        gd::Int e2 = 0;                    // the vertical view coordinate of the ground point
        gd::Int back = 0;
        gd::Int lateral = 0;
        gd::Int lift = 0;
        gd::Int scale = 0;
    };

    // _look_point's return value: GDScript returns the untyped Array [r, look].
    struct LookPoint {
        gd::Int r = 0;
        gd::PI32 look;
    };

    gd::Ref<FightState> fight;
    gd::Ref<FightTables> t;
    gd::Ref<CameraTables> ct;

    // ---- view and projection
    gd::Ref<CameraView> view;          // g_camera 0x800A8A88 (angles in 4096 units); CameraView.new(0, 0, 0, 0, 0, 500) in the constructor
    gd::Int h = 500;                       // 0x800A919C projection plane used by the framing
    gd::Int h_min = 500;                   // 0x800A919E
    gd::Int h_max = 500;                   // 0x800A91A0
    gd::Int stream_rescale = 1;            // 0x800A91A2
    gd::Int overhead = 0;                  // 0x800B08D4 overhead KO camera: fighters drawn closer

    // ---- sources and weights
    gd::Arr<gd::PI32> sources;             // 0x800A0650: x, y, z, pitch, yaw (18-bit), H
    gd::Arr<gd::Ref<SourceState>> source_state;
    gd::PI32 weights{0, 0, 0, 0, 0};       // 0x800A9140 (4.12)
    gd::PI32 prev_weights{0, 0, 0, 0, 0};  // 0x800A914A
    gd::Int height_term = 0;               // 0x80098754 lift of source 1
    gd::Arr<gd::PI32> targets;             // 0x800AE170: x, y, z, screen x per fighter
    gd::Arr<gd::PI32> prev_targets;        // 0x800AE1A0

    // ---- director
    gd::Int state = Cinematic::IDLE;       // 0x800A9138
    gd::Int enabled = 1;                   // 0x800A913C: the fight camera runs (round-end cameras clear it)
    gd::Int intro_timer = 0;               // 0x800A913E
    gd::Int cinematic = 1;                 // 0x800A9154
    gd::Int thrower = 0;                   // 0x800A9156
    gd::Int side = 0;                      // 0x800A9158
    gd::Int wait = -1;                     // 0x800A915A
    gd::Int count = -1;                    // 0x800A915C
    gd::Int count_total = -1;              // 0x800A915E
    gd::Int throw_player = -1;             // 0x800A9160
    gd::Int switch_mode = 0;               // 0x800A9162: 1 after a switch, or a preset that cuts
    gd::Int hit_count = -1;                // 0x800A9164
    gd::Int hit_total = -1;                // 0x800A9166
    gd::Int cam_script = -1;               // 0x800A9168: preset (< 0x1E), stream (STREAM_BASE | ...) or -1
    gd::Int script_kind = 0;               // 0x800A916C: preset index + 1, -1 stream, 0 none
    gd::Int choice_index = 0;              // 0x800A916E
    gd::Ref<MoveRow> script_move;          // 0x800A9170: the thrower's move the camera was chosen for
    gd::Int preset_pitch = 0;              // 0x800A9174
    gd::Int preset_yaw = 0;                // 0x800A9178
    gd::Int stream = -1;                   // 0x800A917C: the playing stream script, -1 none
    gd::Int stream_frames = 0;             // 0x800A9180
    gd::Int stream_frame = -1;             // 0x800A9182
    gd::Int mirror = 0;                    // 0x800A9184
    gd::PI32 stream_origin{0, 0, 0};       // 0x800A918C
    gd::Int stream_restart = 0;            // 0x800A91A6
    gd::Int skip_side = -1;                // 0x800A91A8
    gd::PI32 stream_sample{0, 0, 0, 0, 0, 0, 0, 0};   // 0x800A07D0: eye, target, weight, H
    gd::Ref<CameraTables::HitScript> hit_script;      // 0x800A07E0
    gd::PI32 hit_point{0, 0, 0};           // 0x800A07F0

    // ---- round intro
    gd::PI32 intro_base{0, 0, 0, 0, 0, 0}; // 0x800A0850
    gd::Int intro_length = 0;              // 0x800A0868
    gd::Int intro_index = 0;               // 0x800A086A
    gd::Int intro_counter = 0;             // 0x8009892C

    // ---- round end
    gd::Int winner_style = 0;              // 0x800A0844
    gd::Ref<FighterState> round_end_fighter;   // 0x800A0840
    gd::PI32 winner_a{0, 0, 0, 0, 0, 0, 0, 0, 0};   // 0x8009EA48: pitch, yaw, roll, -, -, eye
    gd::PI32 winner_b{0, 0, 0, 0, 0, 0, 0, 0, 0};   // 0x8009EA70
    gd::PI32 winner_drift{0, 0};                    // 0x8009EA98
    gd::PI32 loser{0, 0, 0, 0, 0, 0, 0};            // 0x800A0890: eye, target, roll
    gd::Int loser_height = 0;              // 0x800A08B0
    gd::Int loser_yaw = 0;                 // 0x800A08B4

    // ---- replay camera
    gd::Int replay_on = 0;                 // 0x800A0875
    gd::Int replay_reset = 0;              // 0x800A0876
    gd::Int replay_distance = 0;           // 0x800A0870
    gd::Int replay_pitch = 0;              // 0x800A087A
    gd::Int replay_style = 0;              // 0x800A0874
    gd::Int replay_yaw = 0;                // 0x800A0878
    gd::Int replay_swing = 0;              // 0x800A087E
    gd::Int replay_swing_to = 0;           // 0x800A0880
    gd::Int replay_track = -1;             // 0x800A0838
    gd::PI32 replay_targets{0, 0, 0, 0, 0, 0};   // 0x800A0820: x, y, z per fighter

    // ---- the Ogre scene's reel (FUN_800673E4(2))
    gd::Int reel_stream = -1;              // the stream script of Ogre's camera id 0x4B
    gd::Int reel_frame = 0;                // its next frame (FUN_800666E0)
    gd::Int reel_left = 0;                 // 0x800A07C2: samples left before the reel ends
    gd::PI32 reel_sample;                  // 0x800A07D0: the raw sample the next step shows

    // The demonstration fight's camera (FUN_800693E8).
    gd::Int attract_state = 0;             // 0x800A913A: 0 set up, 1 running
    gd::Int attract_choice = 0;            // 0x800A086C: the style counter (0-3)
    gd::Int attract_style = 0;             // 0x800A086E: the style and its step (10, 11, 20, 21, ...)
    gd::Int attract_timer = 0;             // 0x80098950: frames left in a style
    gd::Int orbit_kind = 0;                // 0x800A0810
    gd::Int orbit_frame = 0;               // 0x800A0814
    gd::Int orbit_yaw = 0;                 // 0x800A0818
    gd::Int circle_pitch = 0;              // 0x80098944
    gd::Int circle_yaw = 0;                // 0x80098948 (18-bit)
    gd::Int circle_speed = 1;              // 0x8009894C

    explicit CameraDirector(const gd::Ref<FightState>& fight_state);

    // ==== director

    // CameraDirector for the fight frame; `fight.camera_phase` is g_cameraPhase.
    void step(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // The demonstration fight's branch of CameraDirector: set up once (CameraReset, FUN_80069388,
    // FUN_800661C0, FUN_800669CC, FUN_800699F0), then FUN_800693E8 every frame.
    void _attract_step(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // Tekken Force's branch of CameraDirector: set up once (CameraReset, FUN_80069388, FUN_800661C0,
    // FUN_800669CC, FUN_800699F0, then force.ovl's FUN_800B545C; phase 2), then the side-scrolling
    // camera (FUN_800B5504) as source 0 every frame.
    void _force_step(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_80063364: the camera starts again (practice after its replay): CameraReset, the style
    // counter and the sources, and the replay's own state.
    void restart(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_80069388: the style of the counter (modulo 4) and the sources set up again.
    void _attract_next_style(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    static gd::Int _c_rem4(gd::Int v);

    // FUN_800693E8: four styles of 720 frames each: the fight camera (10), an orbit (20, 40) and a
    // circling camera (30); each style's first frame starts it.
    void _attract_frame(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_80069038: source 0 orbiting behind fighter 0 (start with `kind` < 0: ~kind is the orbit
    // kind, 0 level, 1 rising and sweeping sideways), looking at the fighters' midpoint.
    void _orbit(const gd::Ref<FighterState>& f, gd::Int kind);

    // The table index of an angle as the code forms it: (v rounded toward zero to 64ths) & 0xFFF.
    static gd::Int _index64(gd::Int v);

    // FUN_80068E90: source 0 circling the fighters at a random pitch and direction (start with
    // `restart` < 0), framed at 1.1 times the fight camera's distance.
    void _circle(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1, gd::Int restart);

    // CameraReset (0x80062CF8).
    void reset();

    // The round start (FUN_8002AB68): FUN_800661C0, then FUN_8006867C(0) (the intro offset of the
    // counter as it is, the fight camera placed from scratch and kept as the intro's base).
    void round_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_800661C0: the projection plane, both target sets, cleared source states and the
    // start positions of sources 0 and 1.
    void _init_sources(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // The round intro (phase 1): returns true when it has faded into the fight camera.
    bool _intro_frame(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // Phase 2: the fight camera with throw, stream and hit cameras (camera.md#cinematic-cameras).
    void _fight_frame(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // State IDLE: a running throw starts its preset or stream camera (CameraThrowStart); otherwise
    // CameraHitChoose may start a hit camera.
    void _cinematic_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // State STREAM: the camera stream. Returns false when the director stops for this frame.
    bool _stream_frame_state(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // A preset is cancelled by a move camera byte 9 while it waits to blend in.
    void _cancel();

    // State 5: source 4 freezes the blended view and fades into the fight camera.
    void _transition_start();

    // The side flag's screen order: fighter 0 left of fighter 1. On equal screen positions the
    // game compares a third target's screen x with the previous frame's first one (bug #53); the
    // remake compares the previous frame's positions.
    bool _screen_order();

    static gd::Int _camera_byte(const gd::Ref<FighterState>& f);

    // CameraThrowStart (0x80067988): the thrower's camera; its blend-in length, 0 for a stream,
    // -1 for none.
    gd::Int _throw_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1, gd::Int thrower_index,
                         gd::Int side_flag);

    // The common part of CameraThrowStart and FUN_80067B14.
    gd::Int _choose_for(const gd::Ref<FighterState>& thr, gd::Int side_flag, gd::Int player);

    // CameraChoose (0x80067548): the script for a camera id (-1 none); sets the choice index.
    // A random number is drawn even when the id has no camera.
    gd::Int _choose(gd::Int id, gd::Int side_flag, gd::Int player);

    // The preset index + 1 of a script, -1 for a stream, 0 for anything else (FUN_800677C8).
    gd::Int _kind_of(gd::Int value);

    // FUN_800664A0: a preset's random pitch and yaw offset around the fighters' axis.
    void _preset_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_80067DE4: (pitch << 5, yaw offset << 6) of preset `index`, one random pick each.
    gd::Vector2i _preset_random(gd::Int index);

    // FUN_80066430: the direction between the targets in 18-bit units, turned by 180 degrees when
    // fighter 1 is left of fighter 0 on screen.
    gd::Int _axis_yaw(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // CameraThrowEnds (0x80067818) for the running thrower (as the director calls it).
    bool _thrower_done(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    bool _throw_ends(const gd::Ref<FighterState>& thr, const gd::Ref<FighterState>& vic);

    // Frames from the fighter's pose frame to the last event `kind` (high byte) of its move.
    static gd::Int _event_distance(const gd::Ref<FighterState>& f, gd::Int kind);

    // FUN_80067C8C: the blend-out length of the running script, 5 for a cut when the preset
    // camera is already close to the fight camera's direction.
    gd::Int _blend_out(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // `v + limit < 2 * limit + 1` as the game's unsigned test: -limit <= v <= limit.
    static bool _within(gd::Int v, gd::Int limit);

    // FUN_80066568: source 1 for a preset (or the stream step for a stream). Returns the
    // stream's weight, 0 otherwise.
    gd::Int _script_camera(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1, gd::Int restart,
                           gd::Int distance, gd::Int thrower_index);

    // FUN_800670E4: a stream (re)start or the switch at the thrower's next move, then the
    // stream step (FUN_800669D8). `restart_script` 0: no restart. -1 when the switch went to preset
    // framing.
    gd::Int _stream_step(gd::Int restart_script, const gd::Ref<FighterState>& thr, const gd::Ref<FighterState>& vic);

    // FUN_800676CC: the same choice index in the list of the thrower's new camera id (>= 0x2B).
    gd::Int _bank_script(const gd::Ref<FighterState>& thr);

    // FUN_8006312C / FUN_8006320C: the camera of the thrower's next move of a multi-part throw.
    // `start_stream` (FUN_8006320C) also starts a stream around the victim.
    void _switch_camera(gd::Int side_flag, const gd::Ref<FighterState>& thr, const gd::Ref<FighterState>& vic,
                        bool start_stream);

    // FUN_800666E0 with a script: selects the stream; returns its yaw mode.
    gd::Int _stream_start(gd::Int value);

    gd::Ref<MotionBank> _stream_bank(gd::Int value);

    // The Ogre scene's camera set-up (FUN_800B4528's first frame, modes.md#match-flow): CameraReset,
    // FUN_80069388, FUN_800661C0, then the reel of Ogre's camera id 0x4B (FUN_800673E4(2)): the
    // stream state cleared, source 2 at full weight, and the reel's first step at once.
    void ogre_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_800673E4(0) on the Ogre reel: FUN_80066D00 makes source 2 of the current sample (halved,
    // rescaled to the projection plane) and takes the next, then CameraUseSource(2). True on the step
    // that shows the stream's last sample: the director takes over from the next frame.
    bool reel_step();

    void _reel_take();

    // FUN_800666E0 sampling: the next frame into `stream_sample` (positions * 20/32, weight / 8
    // clamped to 0...320 and 320 from the last frame on, H 550).
    // Past the last frame the game decodes beyond the stream; those samples only ever feed an
    // unused source (the weight is then 320), so the remake repeats the last frame.
    void _stream_sample();

    // FUN_800669D8: source 2 from the current sample (placed around the stream origin, turned by
    // the stream yaw, mirrored for the other side, rescaled to the projection plane), then the
    // next sample. Returns the used sample's weight.
    gd::Int _stream_view();

    // CameraHitChoose (0x80067FD4): the index of the fighter whose clean hit gets a hit camera,
    // -1 for none.
    gd::Int _hit_choose(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_80068244: source 1 for a hit camera.
    void _hit_camera(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1, gd::Int restart,
                     gd::Int distance);

    // FUN_8006867C(1): the next intro offset and length; the fight camera placed from scratch.
    void _intro_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_8006879C: source 2 of the intro frame; returns its weight. The eye is the fight
    // camera's plus the offset eased from 2x to 0, looking at the fight camera's x and y at z 0.
    gd::Int _intro_step();

    // FUN_80068998: the winner camera of a round that does not end the match; the two styles
    // alternate.
    void _winner_start(const gd::Ref<FighterState>& f);

    void _winner_step();

    // FUN_800466F8: style A starts 3953 units away at a random angle 60-120 degrees off the facing.
    void _winner_a_start(const gd::Ref<FighterState>& f);

    // FUN_80046854: the eye approaches by 8 units per frame while farther than 2513; the view
    // turns with 1/32 smoothing.
    void _winner_a_step(const gd::Ref<FighterState>& f);

    // FUN_80046A38: style B picks a lateral offset, a forward distance and a height, and a side.
    void _winner_b_start(const gd::Ref<FighterState>& f);

    // FUN_80046CB8: the eye drifts backwards along the facing, is pushed out to 2308 units, yaw
    // follows with 1/32 and pitch with 1/16 smoothing, and the eye sinks when looking up.
    void _winner_b_step(const gd::Ref<FighterState>& f);

    // FUN_80046508: the view from `eye` at the winner's root, its pitch limited by the head and
    // the floor, the eye pushed out to 5800 units when closer (into `out`).
    gd::PI32 _winner_view(const gd::Ref<FighterState>& f, const gd::PI32& eye, const gd::PI32& out);

    // FUN_80046284: pitch (signed) and yaw - 90 degrees of the eye-to-target vector, in 18-bit units.
    gd::PI32 _aim(const gd::PI32& eye, const gd::PI32& target);

    // FUN_80046180: the same in 4096 units.
    gd::PI32 _aim4096(const gd::PI32& eye, const gd::PI32& target);

    // FUN_800463A4 (18-bit angles) / FUN_80046458 (4096 units): a round-end view becomes the
    // camera. The projection plane is left as it is.
    void _use_round_end_view(const gd::PI32& v, bool angles18);

    // FUN_80068A44: the loser camera of the match-deciding round, set up at the result (by the
    // round flow). `alive`: time out (the loser keeps health); otherwise the overhead KO view.
    void loser_start(const gd::Ref<FighterState>& f, bool alive);

    // FUN_80068C44: the loser camera each frame. `down`: the loser's health is 0.
    void _loser_step(const gd::Ref<FighterState>& f, bool down);

    // `(v * 6) / 128` rounded toward zero.
    static gd::Int _smooth6(gd::Int v);

    // One step of the camera generator 0x800A95F4; its upper half.
    gd::Int _camera_random();

    // FUN_80069824 at the replay's start: an orbit from a random side (two styles alternate).
    void replay_start(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_800569D0: a random value below `n` from rand() and the AI generator 0x800AE168.
    gd::Int _ai_random(gd::Int n);

    // FUN_800696FC: the replay camera each frame: source 1 framing both fighters from the orbit
    // yaw, swinging and closing in slowly.
    void replay_step(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // FUN_800695EC: the replay's targets, the roots (at head height at most), taken every fourth
    // frame while bank type 4 tracks on odd frames only ('S' camera byte).
    void _replay_targets(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1);

    // CameraTrackFighters (0x80064944): one target per fighter (the move anchor, or the root while
    // launched, in throws or for move flag bit 29; head height - 0x122).
    void track(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1, gd::Int force_root);

    // CameraFight (0x8006636C): source 0 from the yaw spring and the framing.
    void _camera_fight(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1, gd::Int restart,
                       gd::Int distance);

    // CameraYawSpring (0x80064AF8): (reset for the framing, yaw).
    gd::Vector2i _yaw_spring(const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1, gd::Int restart,
                             gd::Int src);

    // The yaw swing of the fight source: a kick when the camera falls behind the fighters' axis, then
    // a damped spring within +-SWING_CAP (narrower while a move's camera byte asks for it); returns
    // the yaw offset.
    gd::Int _swing(const gd::Ref<SourceState>& st, const gd::Ref<FighterState>& f0, const gd::Ref<FighterState>& f1,
                   gd::Int err, gd::Int sign);

    // The swing limit (in 1/256 of SWING_CAP) asked for by either fighter's move camera byte, or -1.
    static gd::Int _swing_limit(gd::Int b0, gd::Int b1);

    // The pitch target grows with the yaw error; the fight source's pitch follows it while it moves.
    void _pitch_follow(const gd::Ref<SourceState>& st, gd::Int err);

    // CameraFrameFighters (0x800650F8): source `src` framing both targets (camera.md#fight-camera).
    void _frame_fighters(gd::Int pitch, gd::Int yaw, gd::Int restart, gd::Int distance, gd::Int src);

    // The point the camera orbits: the targets' midpoint, offset by the camera's current position
    // turned into the new yaw (or placed at `distance` on a restart).
    void _orbit_centre(const gd::Ref<Framing>& fr);

    // Both targets (and the ball) in view space, the ground point below their middle, and the depth
    // correction that puts the camera `distance` behind it.
    void _project_targets(const gd::Ref<Framing>& fr);

    // Moves the camera back until both targets fit across the screen with a margin, and sideways to
    // centre them (state `lateral` of the source settles it over frames).
    void _fit_horizontal(const gd::Ref<Framing>& fr);

    // Raises the camera so the higher target (or the ball) stays below the top of the screen (state
    // `depth` of the source eases it); returns the camera's vertical offset.
    gd::Int _fit_vertical(const gd::Ref<Framing>& fr);

    // Script cameras looking down lift with the target's height, at most 0x1C a frame.
    void _script_lift(const gd::Ref<Framing>& fr, gd::Int height);

    // The source's new position and angles from the offsets around the orbit centre; unless it
    // restarts, the angles turn by the wrapped difference.
    void _place_source(const gd::Ref<Framing>& fr, gd::Int vertical);

    // CameraBlend (0x80062E5C): the positive-weight sources into source 3 and the view.
    void _blend();

    static gd::Int _bit_index(gd::Int mask);

    // CameraUseSource (0x80064368): source i becomes source 3 and the view.
    void _use_source(gd::Int i);

    // CameraLookPoint (0x80063FE8): the mean distance r to the targets and the point r along
    // the view. Bug #1 is not reproduced: the distance to target 1 uses target 1's z.
    LookPoint _look_point(const gd::PI32& rec);

    static gd::Int _distance(const gd::PI32& target, const gd::PI32& rec);

    // CameraBlendPair (0x80064458): `a` and `b` (weight w of `a`) blended around their look
    // points into source 3 and the view. Bug #6: a non-positive H keeps source 3's H.
    void _blend_pair(const gd::PI32& a, const gd::PI32& b, gd::Int w);

    // FUN_800641D4: a source at `eye` looking at `target` with projection plane `plane` (the
    // previous value `keep` when not positive).
    gd::PI32 _look_source(const gd::PI32& eye, const gd::PI32& target, gd::Int plane, gd::Int keep);

    // ==== arithmetic

    // An 18-bit angle as a 4096-entry table index: `(a [+ 0x3F if negative]) >> 6 & 0xFFF`.
    static gd::Int _index18(gd::Int a);

    static gd::Int _s18(gd::Int v);

    // C division; a zero divisor is taken as 1 (bug #2 is not reproduced).
    static gd::Int _div(gd::Int a, gd::Int b);

    static gd::Int _div8(gd::Int v);

    static gd::Int _half(gd::Int v);

    static gd::Int _sar(gd::Int v, gd::Int n);

    static gd::Int _settle(gd::Int s);
};

}  // namespace t3
