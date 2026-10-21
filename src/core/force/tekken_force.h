#pragma once
// remake/core/force/tekken_force.gd
// Tekken Force (force.ovl, mode 8; modes.md#tekken-force), written from the verified ports in
// tools/research/force_sim.py and the decompiled overlay: the level runner FUN_800B1778 (the level
// script, the spawn patterns, the two enemy slots, the items, the score, timer and countdown,
// the end of a level with its tally), who fights whom (FUN_800B5EC0, FUN_800B598C, the hidden
// manual switch), the side-scrolling camera (FUN_800B545C, FUN_800B5504) with the camera's
// view-space x the walls follow (FUN_800B4DF8, FUN_800B2F98, FUN_800B3020, FUN_800B309C), the
// boss of each level (FUN_800B2B3C) and the mode's hooks.
//
// State keeps the game's layout, as practice's does: force.ovl's data and bss from BASE (`v`),
// loaded from the overlay image when the mode's overlay is loaded. Fighter pointers are the
// records' fixed addresses; the level script pointer is kept as an offset into the level script.
// The screen points of the "+n SEC" and LIFE UP! texts are the presentation's: the state keeps
// the world point they are placed over.
#include <memory>

#include "gd/gd.h"
#include "core/fighter/fighter_input.h"   // FighterInput::PadWords

namespace t3 {

class FightState;
class FightTables;
class GameRam;
class ByteBlock;
class ViewMatrix;
class CameraView;
class FightSimulation;
class SimEvents;
class FighterState;
class MoveSystem;

class TekkenForce {
public:
    static constexpr gd::Int BASE = 0x800B69C0LL;              // force.ovl's data and bss from its HUD bars
    static constexpr gd::Int SIZE = 0x750;

    // ---- force.ovl (addresses)
    static constexpr gd::Int PATTERNS = 0x800B0A10LL;          // 6-byte (variant, type, 0) entries; 99 ends a pattern
    static constexpr gd::Int KILL_SECONDS = 0x800B0B38LL;      // per enemy type: u16 ?, u16 seconds added for a kill
    static constexpr gd::Int ENEMY_HEALTH = 0x800B0B4CLL;      // per level 5 types (16.16)
    static constexpr gd::Int ENEMY_KEYS = 0x800B0BB0LL;        // per level: the costume keys of variants 0 and 1
    static constexpr gd::Int ALLOWANCE = 0x800B0BBCLL;         // per level: the bonus time allowance (frames)
    static constexpr gd::Int STAGE_BONUS = 0x800B0BD0LL;       // per level
    static constexpr gd::Int QUADRANT_ORDER = 0x800B0F04LL;    // s32[4]
    static constexpr gd::Int BOSS_TABLE = 0x800B685CLL;        // per costume key: pointer to 4 × (u16 character, s16 costume or −1)
    static constexpr gd::Int BARS = 0x800B69CCLL;              // 3 × 0x10: health seen, target, fill, trail, direction, x, y
    static constexpr gd::Int CLOSE_A = 0x800B6A0CLL;
    static constexpr gd::Int CLOSE_B = 0x800B6A10LL;
    static constexpr gd::Int MANUAL = 0x800B6A14LL;
    static constexpr gd::Int MANUAL_BUTTON = 0x800B6A18LL;
    static constexpr gd::Int MANUAL_SIDE = 0x800B6A1CLL;
    static constexpr gd::Int TARGET_WAIT = 0x800B6A20LL;
    static constexpr gd::Int TALLY = 0x800B6A6CLL;
    static constexpr gd::Int SCROLL_PARAM = 0x800B6A70LL;
    static constexpr gd::Int SLOTS = 0x800B6A7CLL;             // 2 × 0x18
    static constexpr gd::Int PLAYER = 0x800B6AACLL;
    static constexpr gd::Int ENEMIES = 0x800B6AB4LL;           // the two enemy records
    static constexpr gd::Int THIRD = 0x800B6AB8LL;
    static constexpr gd::Int SCRIPT_PTR = 0x800B6ABCLL;
    static constexpr gd::Int WAIT_DEFEATED = 0x800B6AC0LL;
    static constexpr gd::Int WAIT_COUNT = 0x800B6AC4LL;
    static constexpr gd::Int PATTERN_ON = 0x800B6AC8LL;
    static constexpr gd::Int PATTERN_STOP_X = 0x800B6ACCLL;
    static constexpr gd::Int PATTERN_PTR = 0x800B6AD0LL;
    static constexpr gd::Int FIGHT_START_TIMER = 0x800B6AD4LL;
    static constexpr gd::Int CLEAR_TIME = 0x800B6AD8LL;
    static constexpr gd::Int BONUS_TIME = 0x800B6ADCLL;
    static constexpr gd::Int CLEAR_BONUS = 0x800B6AE0LL;
    static constexpr gd::Int DRAWN = 0x800B6AE4LL;
    static constexpr gd::Int WALLS = 0x800B6AE8LL;             // 3 lines (k, c): left x, right x, stage 18's lower z
    static constexpr gd::Int ITEMS = 0x800B6E90LL;             // 2 × 0xBC
    static constexpr gd::Int BAR_PHASE = 0x800B6DE8LL;
    static constexpr gd::Int BAR_DONE = 0x800B6DECLL;
    static constexpr gd::Int CAM = 0x800B7098LL;               // the camera record: player, x, y, z, pitch, yaw?, h, mode, …
    static constexpr gd::Int CAM_X = 0x800B709CLL;
    static constexpr gd::Int CAM_Y = 0x800B70A0LL;
    static constexpr gd::Int CAM_Z = 0x800B70A4LL;
    static constexpr gd::Int CAM_PITCH = 0x800B70A8LL;
    static constexpr gd::Int CAM_YAW = 0x800B70ACLL;
    static constexpr gd::Int CAM_H = 0x800B70B0LL;
    static constexpr gd::Int SCROLL_MODE = 0x800B70B4LL;
    static constexpr gd::Int CAM_TARGET_X = 0x800B70B8LL;
    static constexpr gd::Int CAM_PITCH_TARGET = 0x800B70BCLL;
    static constexpr gd::Int SCROLL_LEFT = 0x800B70C0LL;
    static constexpr gd::Int SCROLL_RIGHT = 0x800B70C4LL;
    static constexpr gd::Int SCROLL_MIN = 0x800B70C8LL;
    static constexpr gd::Int SCROLL_MAX = 0x800B70CCLL;
    static constexpr gd::Int KEYS = 0x800B70D0LL;
    static constexpr gd::Int CLOCK = 0x800B70D4LL;
    static constexpr gd::Int SLOT_TIMER = 0xC;                 // a slot's +0xC: its state's timer (the level clock or the text-frame count)
    static constexpr gd::Int POPUP_TIMER = 0x14;               // a slot's +0x14: the end of its "+n SEC"
    static constexpr gd::Int HUMAN = 0x800B70D8LL;
    static constexpr gd::Int STATE = 0x800B70DCLL;
    static constexpr gd::Int TALLY_DONE = 0x800B70E0LL;
    static constexpr gd::Int START_X = 0x800B70E4LL;
    static constexpr gd::Int LEVEL_ENDED = 0x800B70E8LL;
    static constexpr gd::Int SCORE = 0x800B70ECLL;
    static constexpr gd::Int VIEW_X = 0x800B70F0LL;
    static constexpr gd::Int TILE_ONLY = 0x800B70F4LL;
    static constexpr gd::Int TILE_ONLY_LIGHT_STAGE = 12;
    static constexpr gd::Int FINAL_CHALLENGE = 0x800B70F8LL;
    static constexpr gd::Int KEY_EARNED = 0x800B70FCLL;
    static constexpr gd::Int HISCORE = 0x800B7100LL;
    static constexpr gd::Int LEVEL = 0x800B7104LL;

    // ---- the mode context (0x800AFF50 + x)
    static constexpr gd::Int RESULT_FLAGS = 0x40;              // 0x800AFF90: keys earned, Doctor B. saved, boss list
    static constexpr gd::Int BOSSES = 0x41;                    // 0x800AFF91 + level: the bosses met (character · 4 + costume)
    static constexpr gd::Int DEFEATED = 0x3C;                  // 0x800AFF8C: enemies defeated (u16)
    static constexpr gd::Int NEW_RECORD = 0x3F;                // 0x800AFF8F
    static constexpr gd::Int FINAL_SCORE = 0x38;               // 0x800AFF88: the score at the result
    static constexpr gd::Int FEATURE_PAUSE = 4;                // 0x800AFF54 (mode context): pausing allowed

    static constexpr gd::Int ENEMY = 0x15;                     // the enemies' character
    static constexpr gd::Int ENEMY_KEY_OFFSET = 0x54;          // costume key of an enemy: 0x54 + the level's key
    static inline const gd::Arr<gd::Int> FORCE_AI_LEVELS{0, 3, 4, 5, 5};
    static constexpr gd::Int SCORE_CAP = 99999990;
    static constexpr gd::Int TIMER_CAP = 0x1734;
    static constexpr gd::Int COUNTDOWN_SOUNDS = 0x8009747ELL;  // the announcer's 1..5
    static constexpr gd::Int SOUND_ITEM = 0x4E6A;
    static constexpr gd::Int SOUND_ITEM_END = 0x87C0;
    static constexpr gd::Int SOUND_TALLY = 0x4C6C;
    static constexpr gd::Int SOUND_TALLY_KEY = 0x4CEB;
    static constexpr gd::Int ITEM_REACH = 0x1F5;
    static constexpr gd::Int ITEM_HEALTH = 0x320000;
    static constexpr gd::Int ITEM_BEHIND = 5000;
    static constexpr gd::Int ITEM_Y = 0x420;
    static constexpr gd::Int SPAWN_EDGE = 5000;
    static constexpr gd::Int BOSS_AHEAD = 0x1000;
    static constexpr gd::Int SLOT_FREE = 1;
    static constexpr gd::Int TARGET_WAIT_FRAMES = 0x78;
    static constexpr gd::Int CLOSE_LIMIT = 399;
    static constexpr gd::Int NEAR = 0x9C4;
    static constexpr gd::Int NEAR_GAP = 0x190;
    static constexpr gd::Int LEVEL_4 = 3;
    static constexpr gd::Int DOCTOR_LEVEL = 4;
    static constexpr gd::Int CAMERA_ROLL_LEVEL_4 = 0x82;
    static constexpr gd::Int STAGE_18 = 0x12;

    // A "+n SEC" or LIFE UP! text over a world point (the presentation projects the point).
    class ForcePopup {
    public:
        gd::PI32 point{0, 0, 0};
        gd::Ref<ViewMatrix> view;
        gd::Int until = 0;                     // the level clock at which it ends
    };

    // `fight` is a GDScript property over the weak reference: held weakly, FightState owns this object.
    gd::Ref<FightState> fight() const { return _fight.lock(); }
    std::weak_ptr<FightState> _fight;
    gd::Ref<FightTables> t;
    gd::Ref<GameRam> ram;                      // force.ovl's tables
    gd::Ref<ByteBlock> v;                      // ByteBlock.new(SIZE), made in the constructor
    gd::PBytes script_bytes;                   // the level script (stage ARC member 3, 20-byte records)
    gd::Arr<gd::Ref<ForcePopup>> slot_popups;  // [ForcePopup.new(), ForcePopup.new()], made in the constructor
    // Bug #41 fixed (RuleSet.fix_force_slot_timers): per slot the +0xC and +0x14 timers as 32-bit
    // values; the slot record keeps the game's 16-bit copies.
    gd::PI32 _slot_timers{0, 0, 0, 0};
    gd::Arr<gd::Ref<ForcePopup>> item_popups;  // [ForcePopup.new(), ForcePopup.new()], made in the constructor

    TekkenForce(const gd::Ref<FightState>& fight_state, const gd::Ref<GameRam>& overlay);

    // ==== byte access by game address
    gd::Int g32(gd::Int a);
    gd::Int gu32(gd::Int a);
    gd::Int g16(gd::Int a);
    gd::Int gu16(gd::Int a);
    void put32(gd::Int a, gd::Int x);
    void put16(gd::Int a, gd::Int x);

    gd::Int level();
    gd::Int score();
    // 0x800B70D8: the pad the player uses (the human's side; the player's record is always the first).
    gd::Int human();
    // 0x800B70E8: the level's enemies are over (the boss fight, or the level won).
    bool level_ended();
    // HitApply's mode-8 scoring: ten points per damage the player (or a forced hit) did to an enemy.
    void add_score(gd::Int points);
    // FUN_800B2E60: the player's record.
    gd::Ref<FighterState> player();
    // FUN_800B2E90: enemy slot `i` (0x18 bytes at SLOTS).
    static gd::Int slot(gd::Int i);
    gd::Ref<FighterState> slot_fighter(gd::Int i);
    gd::Int slot_state(gd::Int i);
    // FUN_800B2EEC: enemy slot `i` is free while the level runs (PairwiseDistances then counts the
    // CPU record `i + 1` as off screen).
    bool slot_free(gd::Int i);

    // ==== the mode's start and each level's set-up

    // FUN_800B1508 (the mode's start, FUN_800B60A4): the keys from the save, the score and the
    // saved hi-score.
    void mode_start();
    // FUN_800B2E10: the player (fighter 0, or 1 when the other side started) and the enemy records.
    void assign_records();
    // FUN_800B15EC (the level's set-up at each round start): the slots empty, the script from the
    // start, the camera and walls, the progress bar; the Doctor B. level has no script and starts
    // with its boss (state 8) and 60 seconds.
    void level_setup(const gd::PBytes& level_script);
    // Whether the level of `fight_index` is the Doctor B. level: no script, its boss from the start,
    // only the ground tile of the panorama (TILE_ONLY), and in the resident code (FUN_80039BB4,
    // FUN_8003A030, mode 8 with TILE_ONLY) stage TILE_ONLY_LIGHT_STAGE's light record for the stage's
    // lights and the fighters' base colour.
    static bool tile_only_level(gd::Int fight_index);
    // FUN_800B396C's state: the bar's phase and done flag.
    void _progress_setup();
    // FUN_800B4198's state: both items free, at y 0x420 (the point the picture is drawn above).
    void _items_setup();

    static inline const gd::Arr<gd::Int> WALL_LINES{-0x143, -0x9F9, 0x149, 0x7B3};
    static inline const gd::Arr<gd::Int> WALL_LINES_18{-0x177, -0x98D, 0x14D, 0x87F};

    // FUN_800B2F98: the wall lines by stage.
    void _walls_setup();
    // FUN_800B53E0: 0 free scrolling, 1 locked, 2 locked within 5,000 units either side.
    void scroll_mode(gd::Int mode);

    // ==== who fights whom (FUN_800B5EC0, FUN_800B598C)

    // FUN_800B5910 at the mode's fight hook: holding all four face buttons turns the hidden manual
    // target switch on (L1 if held, else L2, R1 or R2; L2 without a shoulder button).
    void manual_target_setup(gd::Int held);
    // FUN_800B58FC.
    void manual_target_off();
    // FUN_800B5EC0: the player, the enemy it faces and the other one (and every fighter's opponent).
    gd::Arr<gd::Ref<FighterState>> roles(const gd::Ref<MoveSystem>& moves, const gd::Ref<FighterInput::PadWords>& pads);
    // FUN_800B598C: which enemy the player faces. A hidden enemy (+0xC2) is skipped; while only
    // one is close it is taken; with both close the nearer one wins by 400 units, else the one more
    // in front of the player; a change waits 120 frames. The manual switch swaps the two. (Bug #47,
    // not reproduced: the player's angle fields are restored from a full copy.)
    gd::Ref<FighterState> _choose_target(const gd::Ref<MoveSystem>& moves, const gd::Ref<FighterInput::PadWords>& pads,
                                         const gd::Ref<FighterState>& p, const gd::Ref<FighterState>& a,
                                         const gd::Ref<FighterState>& b);
    // The player's twelve angle fields (+0x2A … +0x42) FUN_800B598C saves around its tests.
    static gd::PI32 _angles(const gd::Ref<FighterState>& f);
    static void _restore_angles(const gd::Ref<FighterState>& f, const gd::PI32& a);

    // ==== the camera (FUN_800B545C, FUN_800B5504) and the walls

    // FUN_800B545C: the side-scrolling camera starts on the player (level 4 rolls it slightly).
    void camera_setup(const gd::Ref<CameraView>& view);
    // FUN_800B5504: the camera scrolls with the player's projected screen x (in scroll mode 0 past
    // the centre, in mode 2 within its bounds), and pitches back when the player nears the bottom
    // of the screen. Returns the source record (x, y, z, pitch, yaw, h) for CameraUseSource(0).
    gd::PI32 camera_frame(const gd::Ref<CameraView>& view);
    // FUN_800B4DF8's first part: the camera's view-space x (the walls follow it).
    void background(const gd::Ref<ViewMatrix>& view, const gd::Ref<CameraView>& camera);
    // A wall line (FUN_800B3020 / FUN_800B309C): v · k / 1000 + c.
    gd::Int _line(gd::Int at, gd::Int x);
    // ArenaBounds in Tekken Force: z within [−0x898, 0x8FC] (on stage 18 the lower limit is a line
    // of x), the player's x between two lines of z relative to the camera's view x, an enemy within
    // 0x1068 of it. Returns Vector2i(dz, dx).
    gd::Vector2i walls(const gd::Ref<FighterState>& f);

    // ==== the level runner (FUN_800B1778)

    // One frame of the level: the script, the enemy slots, the progress bar's phase, the items, the
    // score and timer caps and the announcer's countdown. Returns true on the frame NOW LOADING ends
    // (FightFrame then changes the area: round state 7).
    bool level_frame(const gd::Ref<FightSimulation>& sim, const gd::Ref<SimEvents>& events);
    void _sound(gd::Int code, const gd::Ref<SimEvents>& events);
    // The level script's part (FUN_800B1778 up to its enemy slots).
    bool _script_frame(const gd::Ref<FightSimulation>& sim, const gd::Ref<SimEvents>& events);
    // A script record (20 bytes): s32 trigger, s16 command, dx, _, z, _, param, u16 arg.
    gd::Int _rec32(gd::Int at);
    gd::Int _rec16(gd::Int at);
    gd::Int _recu16(gd::Int at);
    // State 0: every record whose trigger the camera has passed runs.
    void _commands(gd::Int progress, const gd::Ref<SimEvents>& events);
    // FUN_800B2EB0: the first free enemy slot, −1 for none.
    gd::Int free_slot();
    // FUN_800B2F30.
    void _release(gd::Int i);
    // A new enemy in slot `i`: state 4 (enter) when its model is loaded, else 2 (load it first).
    void _spawn(gd::Int i, gd::Int variant, gd::Int kind, gd::Int x, gd::Int z);
    // FUN_800B2F50: anchor and root at (x, z).
    static void place(const gd::Ref<FighterState>& f, gd::Int x, gd::Int z);
    // FUN_800B4328: the first free item slot gets the item.
    void _place_item(gd::Int x, gd::Int z);
    // The spawn pattern (command 2): a new enemy enters from the right whenever a slot is free, until
    // the pattern's 99 or the camera passing its stop x.
    void _patterns(gd::Int progress);
    // State 6: the clear and bonus time, and on the stage clear (round state 8) the clear bonus and
    // the key rule.
    void _bonus_frame();
    // State 7: STAGE BONUS, CLEAR BONUS, CLEAR TIME, then BONUS TIME (or the key or final stage
    // message) 30 frames apart, each with its sound; the bonuses go to the score as they appear.
    void _tally(const gd::Ref<SimEvents>& events);
    // The tally's lines for the presentation: (frames shown, stage bonus, clear bonus, clear time,
    // bonus time) and which last line shows (0 bonus time, 1 the key, 2 the final stage, −1 none).
    gd::PI32 tally_lines();
    // State 8 (the Doctor B. level's fight): slot 0 becomes a type-4 enemy with the level's boss
    // health, its entrance starts, the camera locks (scroll mode 2) and state 9 times the fight.
    void _boss(const gd::Ref<FightSimulation>& sim, const gd::Ref<SimEvents>& events);
    // One enemy slot: 0 release, 1 free, 2–3 load the costume's model, 4 enter with full health,
    // 5 fight, 6 defeated (+n SEC), 7 blink out.
    void _enemy_slot(const gd::Ref<FightSimulation>& sim, gd::Int i, const gd::Ref<FighterState>& p,
                     const gd::Ref<SimEvents>& events);
    // The boss of the level for the player's costume key (FUN_800B2B3C's table): (character,
    // costume); a costume of −1 is the player's other costume when the boss is the player's own
    // character, else costume 0.
    gd::Vector2i boss_of(gd::Int key);
    // FUN_800B2F64: the costume key of an enemy variant on a level.
    gd::Int enemy_key(gd::Int lv, gd::Int variant);
    // Whether a slot's "+n SEC" shows (the presentation draws it rising over its point).
    bool slot_popup_shown(gd::Int i);
    // A slot's timer (SLOT_TIMER, POPUP_TIMER): the game's 16-bit copy, sign-extended and compared with
    // the 32-bit clocks (bug #41), or the whole value with the fix.
    gd::Int _timer(gd::Int i, gd::Int field);
    void _set_timer(gd::Int i, gd::Int field, gd::Int value);
    // FUN_800B4374: the two items. A placed item is removed 5,000 units behind the camera; the player
    // picks it up within 500 units in x and z (health +50, sound 0x4E6A), which starts LIFE UP! for
    // 60 frames (sound 0x87C0 15 frames before its end).
    void _items(const gd::Ref<FightSimulation>& sim, const gd::Ref<FighterState>& p, const gd::Ref<SimEvents>& events);
    // An item's state (0 none, 1 placed, 2 picked up: LIFE UP! shows) and world point.
    gd::PI32 item(gd::Int i);

    // ==== the health bars (FUN_800B30DC, FUN_800B3118)

    // FUN_800B30DC: the three bars empty (they fill up at the round start).
    void bars_reset();
    // FUN_800B3118's state: the player's bar (0x9C pixels) and each shown enemy's (as wide as its
    // type's entry): a drop shows at once, a refill grows 3 pixels per frame, the recent-damage part
    // closes a sixteenth per frame.
    void bars_step();
    // A bar for the presentation: target, fill, recent damage, grows rightwards, x, y, width.
    gd::PI32 bar(gd::Int k);
};

}  // namespace t3
