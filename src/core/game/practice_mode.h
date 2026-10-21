#pragma once
// remake/core/game/practice_mode.gd
// PRACTICE MODE (practice.ovl, mode 5; modes.md#practice): the practice state S (0x800B9180,
// kept as bytes), its set-up at each round start (FUN_800B2D74), and the frame hook FightFrame
// calls last (FUN_800B3084): the pause menu (practice_sim.py's verified menu_cursor/menu_input),
// the training dummy's scripted input (FUN_800B4684, FUN_800B87C8), the attack data
// (FUN_800B3808), the freeze signal (FUN_800B4600, FUN_800B3520, FUN_800B6CF4), the key ring and
// the combo guide (FUN_800B7A88 … FUN_800B86F0), and the replay settings (FUN_800B8D50).
//
// The fight reads `pad_swap` (the dummy on CONTROLLER takes the other pad) and records each
// fighter's pad word through `record_pad` (FUN_800B4800, FUN_800B7AEC).
//
// The hit markers' screen points (FUN_80036D28) are left to the presentation (PracticeView),
// which projects the world point kept per marker with the fight camera.
#include <string>

#include "gd/gd.h"
#include "core/game/flow_part.h"
#include "core/fight/round_state.h"
#include "platform/pad_state.h"

namespace t3 {

class GameFlow;
class ByteBlock;
class FighterState;

class PracticeMode : public FlowPart {
public:
    static constexpr gd::Int BASE = 0x800B9180LL;
    static constexpr gd::Int SIZE = 0x1F8;
    static constexpr gd::Int RING = 0xB0;                   // 0x800B9230: 50 key-ring words
    static constexpr gd::Int RING_WORDS = 0x32;
    static constexpr gd::Int REPLAY = 0x178;                // 0x800B92F8: replay position, word, count
    static constexpr gd::Int MARKS = 0x184;                 // 0x800B9304: 50 u16 move slots of the replayed guide
    static constexpr gd::Int HEADS = 0x1E8;                 // 0x800B9368: per player the ring head
    static constexpr gd::Int CHECKED = 0x1F0;               // 0x800B9370: per player
    static constexpr gd::Int PLAYER_DATA = 0x14;            // per player 0x20 bytes of attack data
    // S fields.
    static constexpr gd::Int HELD = 0x00;
    static constexpr gd::Int PRESSED = 0x02;
    static constexpr gd::Int REPEAT = 0x04;
    static constexpr gd::Int MAPPED_HELD = 0x06;
    static constexpr gd::Int MAPPED_PRESSED = 0x08;
    static constexpr gd::Int SCRIPT_EDGE = 0x0C;
    static constexpr gd::Int SCRIPT_PREVIOUS = 0x0E;
    static constexpr gd::Int REPLAY_FRAMES = 0x10;
    static constexpr gd::Int COMBO_FRAMES = 0x12;
    static constexpr gd::Int MARKERS = 0x54;
    static constexpr gd::Int MARKER_INDEX = 0x74;
    static constexpr gd::Int MODE_SELECT = 0x75;
    static constexpr gd::Int INTRO_DELAY = 0x76;
    static constexpr gd::Int SUB = 0x77;
    static constexpr gd::Int CURSOR_FREE = 0x78;
    static constexpr gd::Int CURSOR_VS = 0x79;
    static constexpr gd::Int CURSOR_COMBO = 0x7A;
    static constexpr gd::Int COMBO_PLAYER = 0x7B;
    static constexpr gd::Int COMBO_TYPE = 0x7C;
    static constexpr gd::Int DUMMY = 0x7D;
    static constexpr gd::Int CPU_DIFFICULTY = 0x7E;
    static constexpr gd::Int CPU_LEVEL = 0x7F;
    static constexpr gd::Int DUMMY_KIND = 0x80;             // 0 CONTROLLER, 1 scripted, 2 CPU
    static constexpr gd::Int REPLAY_SETTING = 0x81;
    static constexpr gd::Int GUIDE_STATE = 0x82;
    static constexpr gd::Int FREEZE_SIGNAL = 0x83;
    static constexpr gd::Int PLAYER = 0x84;
    static constexpr gd::Int OTHER = 0x85;
    static constexpr gd::Int SWAP = 0x86;
    static constexpr gd::Int GUIDE = 0x87;
    static constexpr gd::Int CAN_ACT = 0x88;                // per player: the freeze signal's result
    static constexpr gd::Int PAUSED = 0x8A;
    static constexpr gd::Int PAUSED_BEFORE = 0x8B;
    static constexpr gd::Int ATTACK_DATA = 0x9D;
    static constexpr gd::Int COUNTER_ATTACKS = 0x9E;
    static constexpr gd::Int COMMAND_LIST = 0x9F;
    static constexpr gd::Int KEY_DISPLAY = 0xA0;
    static constexpr gd::Int GUIDE_LENGTH = 0xA1;
    static constexpr gd::Int GUIDE_PROMPT = 0xA2;
    static constexpr gd::Int PROMPT = 0xA3;
    static constexpr gd::Int PROMPT_TIMER = 0xA4;
    static constexpr gd::Int CHANGED = 0xA7;
    static constexpr gd::Int RESUMED_INPUT = 0xA8;
    static constexpr gd::Int MENU_DELAY = 0xA9;
    static constexpr gd::Int ACTING = 0xAA;                 // per player (FUN_800B4818)
    static constexpr gd::Int RESUMING = 0xAC;
    // Per-player attack data (+0x14 + 0x20·player).
    static constexpr gd::Int D_TOTAL = 0x00;
    static constexpr gd::Int D_COMBO_DAMAGE = 0x04;
    static constexpr gd::Int D_SHOWN_DAMAGE = 0x08;
    static constexpr gd::Int D_HIT_DAMAGE = 0x0C;
    static constexpr gd::Int D_HIT_SHARE = 0x0E;
    static constexpr gd::Int D_MARKER = 0x10;
    static constexpr gd::Int D_HITS = 0x12;
    static constexpr gd::Int D_COMBO = 0x14;
    static constexpr gd::Int D_NEW_BEST = 0x15;
    static constexpr gd::Int D_ENDED = 0x16;
    static constexpr gd::Int D_PREVIOUS = 0x17;
    static constexpr gd::Int D_LAST = 0x18;
    static constexpr gd::Int D_DROPPED = 0x19;
    static constexpr gd::Int D_COUNTER = 0x1A;
    static constexpr gd::Int D_CLEAN = 0x1B;
    static constexpr gd::Int D_HIT_ID = 0x1C;
    static constexpr gd::Int D_BEST = 0x1D;
    static constexpr gd::Int D_THROW = 0x1E;
    static constexpr gd::Int D_THROW_BEFORE = 0x1F;
    // The input guide and recorder (GUIDE_STATE): the prompt, recording, the combo guide's play
    // and the replay of a recording (the prompts of the practice HUD).
    // Not the RULES.md unscoped enum: GuideState and Item share names with each other and with the
    // S-field constants (REPLAY, ATTACK_DATA, GUIDE, ...), so each enum is wrapped in a struct of
    // the same name. `GuideState::IDLE` / `Item::REPLAY` are gd::Int constants as in GDScript.
    struct GuideState {
        enum : gd::Int {
            IDLE = 0, READY = 1, RECORD_WAIT = 2, RECORDING = 3, GUIDE_STAND = 4, GUIDE_PLAY = 5, REPLAY = 6,
            REPLAY_END = 7, GUIDE_MODE = 8, MOVE_END = 9,
        };
    };
    // The menu items (practice_rows' ids).
    struct Item {
        enum : gd::Int {
            RESUME = 0, ATTACK_DATA = 1, COUNTER_ATTACKS = 2, FREEZE_SIGNAL = 3, REPLAY = 4, KEY_DISPLAY = 5,
            GUIDE = 6, COMMAND_LIST = 7, CPU_DIFFICULTY = 9, CPU_LEVEL = 10, CHARACTER_SELECT = 11, EXIT = 12,
            DUMMY = 13, MODE_SELECT = 14, COMBO_PLAYER = 15, COMBO_TYPE = 16,
        };
    };
    // Any of these starts a recording: the directions, face and shoulder buttons.
    static constexpr gd::Int RECORD_KEYS = PadState::DIRECTIONS | PadState::FACE_BUTTONS | PadState::L1 | PadState::R1
        | PadState::L2 | PadState::R2;
    static inline const gd::Arr<gd::Int> ITEM_ROW_CURSORS{CURSOR_FREE, CURSOR_VS, CURSOR_COMBO};
    static inline const gd::Arr<gd::Int> ITEM_ROW_LAST{8, 8, 7};
    static constexpr gd::Int SOUND_MOVE = 0x546C;
    static constexpr gd::Int SOUND_OK = 0x4CEB;
    static constexpr gd::Int CHOOSE = 0x9F0;
    static constexpr gd::Int REQUEST_SELECT = RoundState::END;              // back to character select
    static constexpr gd::Int REQUEST_LEAVE = RoundState::PRACTICE_EXIT;    // the main menu
    static constexpr gd::Int GUIDE_WAIT_FRAMES = 0x1E;
    static constexpr gd::Int REPLAY_MIN = 0x5A;
    static constexpr gd::Int REPLAY_MAX = 300;
    static constexpr gd::Int MANUAL = 4;
    static inline const gd::Arr<gd::Int> PLAYER_SOUNDS{0x80, 0x4C70, 0x10, 0x4C71, 0x40, 0x4C72, 0x20, 0x4C73};   // button, sound

    gd::Ref<ByteBlock> s;                      // ByteBlock.new(SIZE), made in the constructor
    gd::PI32 guide_wait{0, 0};                 // 0x800B916C: per player
    gd::PI32 pads{0, 0};                       // 0x800B9174: per fighter the pad word of the frame
    gd::Arr<gd::PI32> marker_points{gd::PI32{0, 0, 0}, gd::PI32{0, 0, 0},
        gd::PI32{0, 0, 0}, gd::PI32{0, 0, 0}};   // the hit markers' world points

    // FlowPart's _init(flow): PracticeMode has no _init of its own; the constructor makes `s`.
    explicit PracticeMode(GameFlow* flow);   // FlowPart holds the flow as a plain pointer (flow_part.h)

    // Several byte fields at once: offset, value, offset, value …
    void _puts(const gd::Arr<gd::Int>& pairs);
    gd::Int b(gd::Int at);
    gd::Int sb(gd::Int at);
    void put(gd::Int at, gd::Int v);
    gd::Int pd(gd::Int player, gd::Int at);

    // ---- the fight's hooks

    // FUN_800B4800: the dummy on CONTROLLER reads the other pad.
    bool pad_swap();
    // FUN_800B7AEC: a fighter's pad word of the frame (the key display's source).
    void record_pad(gd::Int index, gd::Int word);
    // FUN_800B2D74 (FUN_8002AB68 in mode 5): the settings of a new practice session's round.
    void setup();
    // AiReset (0x800596B0) of the dummy with the page's CPU DIFFICULTY and LEVEL.
    void _ai_reset(const gd::Ref<FighterState>& f);
    // FUN_8002BE54: every fighter's input cleared and its input mode set.
    void _input_modes(gd::Int mode);
    void _signal_clear();
    // FUN_800B3084 (FightFrame's last step in mode 5): returns the paused flag (0x800958DC).
    gd::Int frame();
    // OK: the menu closes at the next pause check.
    void resume();
    // The remake's Escape on the menu (not while COMMAND LIST is shown, which Escape's Start turns
    // off): as OK. False when the menu is not taking input.
    bool escape();
    // FUN_800B2BC4: leaving the pause menu applies the dummy and the page's settings.
    void _resume(const gd::Ref<FighterState>& me, const gd::Ref<FighterState>& dummy);

    // ---- the training dummy (FUN_800B4684, FUN_800B87C8 and its guards)
    void _dummy_control(const gd::Ref<FighterState>& me, const gd::Ref<FighterState>& dummy);
    // FUN_800B87C8: the dummy's scripted word for the TRAINING DUMMY setting.
    void dummy_input(const gd::Ref<FighterState>& dummy, const gd::Ref<FighterState>& me, gd::Int setting);
    // FUN_800B8904: lying down, the dummy holds up; true when it may act. A stance slot empty in
    // both banks has no row (bug #61, not reproduced).
    bool _standing_ok(const gd::Ref<FighterState>& f);
    // FUN_800B8AEC: UKEMI MAE (LK+RK) or OKU (LP+RP) on alternate frames, during a reaction with
    // an air window and a tech-roll branch.
    void _tech_roll(const gd::Ref<FighterState>& f, bool forward);
    // FUN_800B8B94: a branch row of the move leads to the tech roll (transition 0x1C).
    bool _has_tech_roll(const gd::Ref<FighterState>& f);
    // FUN_800B899C: the guide's replay may start once the fighter stands in its stance (a turned
    // fighter first turns back).
    bool _ready_to_replay(const gd::Ref<FighterState>& f);

    // ---- the freeze signal (FUN_800B4600, FUN_800B3520, FUN_800B6CF4)
    void _freeze_signal(const gd::Ref<FighterState>& me, const gd::Ref<FighterState>& dummy);
    // The back light colours: green while a fighter can act, red while it cannot.
    void _signal_colours();

    // ---- the key ring and the combo guide

    // FUN_800B7A88: the ring emptied (its flag bits kept).
    void key_ring_reset(gd::Int player);
    // FUN_800B8244: the ring's replay restarts at its first entry.
    void replay_reset(gd::Int player);
    // FUN_800B82F4: the recording ends (its last entry counts once), and the replay rewinds.
    void _recording_end(gd::Int player);
    void _replay_rewind(gd::Int player);
    // Untyped Array in GDScript: the bank type's list of FlowData.practice_combos, each combo a
    // Dictionary {"steps": key-ring words, "moves": move slots}.
    gd::Arr<gd::Dict<std::string, gd::PI32>> _combos(gd::Int player);
    // FUN_800B8108: the combo count of the combo player's character.
    gd::Int combo_count();
    // FUN_800B7FB4: combo `combo` of the player's character into the ring as the guide.
    gd::Int load_guide(gd::Int player, gd::Int combo);
    // FUN_800B85A0: the guide's move slots from MARKS, their count at REPLAY + 8.
    gd::Int _guide_moves(gd::Int player, gd::Int combo);
    // FUN_800B8764.
    void _guide_moves_reset();
    // FUN_800B7B08: the fighter's pad word logged into the ring (a repeat counts on).
    void _key_log(gd::Int player);
    // FUN_800B7968: true when a new input comes after the ring has been idle for 30 frames.
    bool _ring_idle(gd::Int player);
    // FUN_800B83A4: the guide's next scripted word for the player (false once the ring is played).
    bool _guide_play(gd::Int player);
    // FUN_800B8510: the move the player starts at its active frame, logged after the guide's moves.
    void _guide_move_log(gd::Int player);
    // FUN_800B86F0: the combo player landed a clean hit, or is in a throw transition.
    bool _guide_hit();

    // ---- the attack data (FUN_800B3808)
    void _attack_data(const gd::Ref<FighterState>& me, const gd::Ref<FighterState>& dummy);
    // The COMBO TRAINING guide and the FREE page's recorded replay (FUN_800B3808's state machine).
    void _guide_step(const gd::Ref<FighterState>& me, gd::Int cp);
    // READY: the prompt; in combo training Select plays the guide, in FREE Select replays the
    // recorded input and down + Select records a new one.
    void _guide_ready(gd::Int cp);
    // FUN_800B680C: the button sounds of the guide's replay.
    void _button_sounds(gd::Int player, gd::Int word);
    // FUN_800B4818: the player is in an attack, or the dummy left its stance after a hit.
    bool _acting(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& o);
    // A player's combo counter, damage and labels (the hits the player takes).
    void _player_data(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& o);
    // Whether the opponent's move is a throw (move flag 13, or one of a few slots of banks 4, 7 and
    // 0xC), and whether its previous one was.
    void _throw_flag(gd::Int p, const gd::Ref<FighterState>& o);
    // The combo counter: kept while the opponent reacts, counting hits and throw steps, up to 99.
    void _count_combo(gd::Int p, const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& o);
    // A new best or an ended combo, and the marker's frames (90 on a new best or a multi-hit move).
    void _combo_markers(gd::Int p);
    // The hit's shown damage and share, the combo damage (capped at ±999) and the total.
    void _damage(gd::Int p, const gd::Ref<FighterState>& f);

    // ---- the replay settings (FUN_800B8D50)
    void _replay_control(const gd::Ref<FighterState>& dummy, const gd::Ref<FighterState>& me);

    // ---- the pause menu (practice_sim.menu_cursor, menu_input)

    // FUN_800B4BA8: up/down on the repeat pad, wrapping at 0 and `last`.
    gd::Int _cursor_step(gd::Int cur, gd::Int last);
    // FUN_800B49C4.
    void _menu_cursor();
    // FUN_800B4C44.
    void _menu_input();
    // FUN_800B4C8C.
    void _mode_select_confirm();
    void _changed();
    // Left or right flips a byte; true when a face button was pressed instead.
    bool _toggle(gd::Int at);
    // Right adds one, left takes one (both may apply), wrapping at 0 and `last`.
    bool _cycle(gd::Int at, gd::Int last);
    void _guide_changed();
    // The item under the cursor (FUN_800B4E4C, jump table 0x800B0A58).
    gd::Int item_under_cursor();
    void _item_input();
    // COMMAND LIST: a choice button opens or closes the move lists, which then take the input.
    void _command_list_item(gd::Int p);
    // The combo type: left / right cycle through the character's combos; true to go back.
    bool _combo_type_item(gd::Int p);
};

}  // namespace t3
