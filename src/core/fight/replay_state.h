#pragma once
// remake/core/fight/replay_state.gd
// The round-end replay (`ReplayUpdate` 0x800323CC and its hooks): a ring of 300 recorded
// frames per fighter (pose and blend moves and frames, root position and rotation, anchor,
// hand, wing and power-timer values), plus the frame's sounds and the effect log, played back
// at half speed after a KO (the root is interpolated on the odd half frames).
//
// Every hook takes the live value and returns the value to use: while recording it stores the
// value, on the first played frame it also saves the live state (restored when the replay
// ends), while playing it returns the recorded one.
#include "gd/gd.h"

namespace t3 {

class FightSimulation;
class FightState;
class FighterState;
class MoveRow;
class TekkenBall;

class ReplayState {
public:
    static constexpr gd::Int FRAMES = 300;
    static constexpr gd::Int HALF_FRAMES = 600;
    static constexpr gd::Int WRAP = 0x4B00;                // the game adds 0x4B00 before its modulo
    static constexpr gd::Int RECORDING = 7;
    static constexpr gd::Int STARTING = 2;
    static constexpr gd::Int FIRST_FRAME = 3;
    static constexpr gd::Int PLAYING = 1;
    static constexpr gd::Int ENDING = 4;
    static constexpr gd::Int DONE = 5;
    static constexpr gd::Int START_BACK = 0x96;             // the playback starts at most 150 frames back
    static constexpr gd::Int LAST_FRAMES = 0x12A;
    static constexpr gd::Int LOG_ENTRIES = 64;
    static constexpr gd::Int LOG_CYCLE_LIMIT = 0x41;
    static constexpr gd::Int BALL_BACK = 0x1E;              // Tekken Ball replays the last 30 frames
    static constexpr gd::Int BALL_FRAMES = 0x1E;            // the ball's ring (0x7EF8 in the replay buffer)

    // One fighter's recorded frame (0x34 bytes in the game).
    class Frame {
    public:
        gd::Ref<MoveRow> pose_move;           // +0x00
        gd::Ref<MoveRow> blend_src;           // +0x04
        gd::Ref<MoveRow> blend_dst;           // +0x08
        gd::PI32 root{0, 0, 0};               // +0x0C
        gd::PI32 pos{0, 0, 0};                // +0x18
        gd::PI32 angles{0, 0, 0};             // +0x24 root rotation (s16)
        gd::PI32 hands{0, 0};                 // +0x2A vertex variants
        gd::Int src_frame = 0;               // +0x2C
        gd::Int dst_frame = 0;               // +0x2D
        gd::Int pose_frame = 0;              // +0x2E (u8)
        gd::Int blend_frames = 0;            // +0x2F (u8)
        gd::Int wings = 0;                   // +0x30
        gd::Int power = 0;                   // +0x31
        gd::Int blend_bits = 0;              // +0x33: blend mode << 4 | blend active & 0xF
    };

    // One effect log entry (0x1C bytes): a flipbook (mode 0) or a landing dust ring (mode 1).
    class LogEntry {
    public:
        gd::PI32 point{0, 0, 0};
        gd::PI32 direction{0, 0, 0};   // s16 direction (or damage in the third)
        gd::Int mode = 0;
        gd::Int set = 0;
        gd::Int index = 0;
    };

    gd::Int stage = 0x17;                   // 0x8009C040 (ReplayInit(0) sets 0x17)
    gd::Int mode = RECORDING;               // 0x8009C050
    gd::Int bank = 0;                       // 0x8009C054
    gd::Int record = 0;                     // 0x8009C060: the frame being recorded
    gd::Int play = 0;                       // 0x8009C064: the half frame being played
    gd::Int mark = -2;                      // 0x8009C068: where the playback starts (half frames)
    gd::Int sound_delay = 0;                // 0x8009C06C
    gd::Int skip = 0;                       // 0x8009C070
    gd::Int recorded = 0;                   // 0x8009C078: frames recorded this round
    gd::PI32 sounds;                        // per frame: code | kind << 16 | player << 24
    // per fighter: FRAMES × Frame (`Array[Array]` of Frame: gd::Arr<gd::Arr<gd::Ref<Frame>>>)
    gd::Arr<gd::Arr<gd::Ref<Frame>>> frames{gd::Arr<gd::Ref<Frame>>(), gd::Arr<gd::Ref<Frame>>()};
    gd::Arr<gd::Ref<Frame>> saved;          // the live state (0x79E0); [Frame.new(), Frame.new()] in the constructor
    // The effect log (0x8009EBD8 …).
    gd::Int log_on = 0;
    gd::Int log_frame = 0;                  // the frame slot being written
    gd::Int log_read = 0;                   // the frame slot being played
    gd::Int log_cycle = 0;                  // entries written since the frame ring last wrapped
    gd::Int log_write = 0;                  // the next entry
    gd::PI32 log_frames;                    // per frame slot: first entry | count << 16
    gd::Arr<gd::Ref<LogEntry>> log_entries;
    gd::Arr<gd::PBytes> balls;              // Tekken Ball: per frame of the ball's ring, 0x24 bytes

    ReplayState();

    // ReplayInit(0) at a mode start: the replay's stage word; started from the attract
    // demonstration (game state 6) the replay mode is 0.
    void init_attract(bool from_demonstration);

    // ReplayInit(2) at a round start.
    void reset();

    // FUN_8004A6A4 (FUN_800777AC at a round start): the effect log starts empty.
    void log_reset();

    // FUN_8004A700: logs an effect (mode 0: a flipbook of `set` and `index`; mode 1: a dust ring).
    // At most 64 entries per cycle of the frame ring; the entries themselves are a ring of 64.
    void log_effect(gd::Int set, gd::Int index, const gd::PI32& point, const gd::PI32& direction, gd::Int log_mode);

    // FUN_8004A8AC: the effect log's next frame slot.
    void log_advance();

    // FUN_8004A954's read side: the entries of the frame slot being played, then the next slot.
    gd::Arr<gd::Ref<LogEntry>> log_take();

    // ReplayRecordSound (0x80033780): while recording, a sound goes to the frame slot `record +
    // sound_delay` (several sounds of one frame spread over the following slots).
    void record_sound(gd::Int kind, gd::Int player, gd::Int code);

    // FUN_80032030 at the first KO: the playback starts 30 frames before the move of the
    // opponent (`opp`, the fighter that landed the KO) began.
    void start_ko(const gd::Ref<FighterState>& opp);

    // ReplayStart (0x8003209C) with `back` frames (0: from the KO mark). Without a mark (a time
    // out) there is nothing to play.
    void start(const gd::Ref<FightState>& fight, gd::Int back);

    // ReplayUpdate (0x800323CC): the recording and playback step of the frame; non-zero while the
    // replay plays (the replay's first frame, STARTING, swaps the roots and sets the camera).
    gd::Int update(const gd::Ref<FightState>& fight, const gd::Ref<FightSimulation>& sim);

    // FUN_800338E0: on the even half frames, the recorded sounds of the frame.
    void _play_sounds(const gd::Ref<FightState>& fight, const gd::Ref<FightSimulation>& sim);

    // ==== hooks

    // The replay buffer of a record: player 1's for every record but the first (the game compares
    // with 0x800A96F0), so Tekken Force's third record shares the second's.
    static gd::Int _side(const gd::Ref<FighterState>& f);

    bool _playing() const;

    gd::Ref<Frame> _frame_of(gd::Int p) const;

    // ReplayRecordPose (0x80032DE8, after the transition blend) and ReplayPlayPose (0x80032EC8,
    // at the start of FighterAnimate).
    void record_pose(const gd::Ref<FighterState>& f);

    void play_pose(const gd::Ref<FighterState>& f);

    void _store_pose(const gd::Ref<Frame>& r, const gd::Ref<FighterState>& f);

    // ReplayRootRotation (0x80032C0C): the root rotation angles (−tilt x, heading − 0x8000, −tilt z).
    gd::PI32 root_rotation(const gd::Ref<FighterState>& f, const gd::PI32& angles);

    // ReplayRootPosition (0x80032838): the root (interpolated on odd half frames) and the anchor.
    void root_position(const gd::Ref<FighterState>& f);

    // ReplayHandPose (0x800332D8), FUN_80033158 (the wings) and ReplayPowerTimer (0x80033600):
    // one byte each.
    gd::Int hand_pose(const gd::Ref<FighterState>& f, gd::Int value, gd::Int channel);

    gd::Int wings(const gd::Ref<FighterState>& f, gd::Int value);

    gd::Int power_timer(const gd::Ref<FighterState>& f);

    gd::Int _byte(const gd::Ref<FighterState>& f, gd::Int value, gd::Int field, gd::Int index);

    static void _set_byte(const gd::Ref<Frame>& r, gd::Int field, gd::Int index, gd::Int value);

    static gd::Int _get_byte(const gd::Ref<Frame>& r, gd::Int field, gd::Int index);

    // FUN_800339F4: Tekken Ball's ball in the replay: recorded while the replay records (its
    // position, angles, squash axis and amount, render kind and timer), played back while it plays;
    // a charged ball played back starts its glow again.
    void ball(const gd::Ref<TekkenBall>& tb);
};

}  // namespace t3
