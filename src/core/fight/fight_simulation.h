#pragma once
// remake/core/fight/fight_simulation.gd
// One VS match as the game runs it (fight-frame.md): the match set-up (FUN_8002A660), FightMain's
// round transitions (states 5-7, the round start FUN_8002AB68) and FightFrame (0x8002B0AC)
// every frame, with the camera director, the round flow and the replay.
//
// `step` is one frame of the main loop with both pads' physical buttons. Everything that is
// heard or seen goes out as SimEvents; the presentation reads the state and never writes it.
//
// CPU fighters take their input from the AI (CpuOpponent, AiDecision). Tekken Ball (mode 7)
// adds the ball (TekkenBall) and its stand-in attacker in the third record; Tekken Force (mode 8)
// fights with all three records and runs its level (TekkenForce).
#include <memory>

#include "core/fighter/bank_type.h"
#include "core/fighter/fighter_input.h"
#include "gd/gd.h"

namespace t3 {

class AiDecision;
class Combat;
class CameraDirector;
class CpuOpponent;
class FightContent;
class FightSetup;
class FightState;
class FightTables;
class FighterAnimation;
class FighterPhysics;
class FighterState;
class MotionBank;
class MoveRow;
class MoveSystem;
class PracticeMode;
class RoundFlow;
class RuleSet;
class SimEvents;
class TekkenBall;
class ViewMatrix;

// enable_shared_from_this: the frame passes `self` (shared_from_this()) to RoundFlow, TekkenBall
// and TekkenForce as a gd::Ref<FightSimulation>. Not usable inside the constructor (start_vs
// does not pass `self`).
class FightSimulation : public std::enable_shared_from_this<FightSimulation> {
public:
    static constexpr gd::Int MAIN_NEXT_ROUND = 5;
    static constexpr gd::Int MAIN_ROUND_START = 7;
    static constexpr gd::Int MAIN_FIGHT = 8;
    static constexpr gd::Int LAST_BANK_TYPE = BankType::FORCE_ENEMY;
    static constexpr gd::Int SLOTS = 0xFB7;
    static constexpr gd::Int FALLBACK_SLOT = 0xDC5;
    static constexpr gd::Int PLACEMENT_X = 1000;
    static constexpr gd::Int PLACEMENT_FACING = 0x4000;
    static constexpr gd::Int START_HEAD_Y = -0x591;         // FUN_8002BFCC: joint 2's height before the first frame
    static constexpr gd::Int START_CHEST_Y = -0x41B;        // joint 1's
    static constexpr gd::Int START_SCREEN_X = 100;
    static constexpr gd::Int STANCE_HOLD = 0x90;            // physical Triangle or Square at round 1: stance slot 0
    static constexpr gd::Int STANCE_CHOICE = 0x44;          // Cross or L1: slot 2, else a coin flip between 0 and 2
    static constexpr gd::Int LONG_STANCE = 0x96;
    static constexpr gd::Int LOOK_START = 0x38E;
    static constexpr gd::Int HAND_SPEED = 10;
    static constexpr gd::Int SAVED_DISTANCE = 1000;
    static constexpr gd::Int VS_HANDICAPS = 0x3A;           // mode context: per player (4 bytes apart) the health index

    gd::Ref<FightState> fight;
    gd::Ref<FightContent> content;
    gd::Ref<FightSetup> setup;
    gd::Ref<FightTables> t;
    gd::Ref<MoveSystem> moves;
    gd::Ref<FighterPhysics> physics;
    gd::Ref<Combat> combat;
    gd::Ref<FighterAnimation> animation;
    gd::Ref<CameraDirector> camera;
    gd::Ref<RoundFlow> round_flow;
    gd::Ref<CpuOpponent> ai;
    gd::Ref<AiDecision> cpu;
    gd::Ref<PracticeMode> practice;             // practice.ovl's hooks in mode 5 (set by the game flow)
    // `var ball: TekkenBall` with only a getter (`return fight.ball`): the method ball().
    // volley.ovl's state and hooks in mode 7 (FightState.ball)
    gd::Ref<TekkenBall> ball() const;
    gd::Int loads = 0;                          // fights set up so far (the presentation builds the stage and models again)
    // Per record: models swapped in during the fight so far (Tekken Force's enemy variants and boss,
    // the Ogre scene's costume): the presentation replaces only that record's model.
    gd::PI32 model_loads;
    gd::Ref<SimEvents> events;
    gd::Ref<FighterInput::PadWords> pads;
    gd::Ref<ViewMatrix> view;                   // the frame's camera (after the shake)
    gd::Ref<ViewMatrix> fighter_view;           // what the fighters are drawn with (FUN_80036254)
    gd::Int main_state = MAIN_ROUND_START;      // FightMain's sub-state (0x800AE6EC)
    gd::Int state_timer = 0;                    // 0x800AE6DC
    bool finished = false;                      // the match is over (VS: FUN_800B15CC)
    bool exit_requested = false;                // MenuExitCheck: RESET or Start + Select leaves the fight
    gd::Arr<gd::PI32> key_tables;               // per player: the mapped word of each physical bit

    // A fight simulation; with a set-up it starts that VS match at once (the M2 app and the fight
    // trace tests), without one the game flow drives it (GameFlow: load_fighters, round_start,
    // fight_frame).
    // Creates events, pads, view and fighter_view (declaration order), then runs _init.
    FightSimulation(const gd::Ref<FightContent>& fight_content, const gd::Ref<FightSetup>& fight_setup,
                    const gd::Ref<RuleSet>& rules);

    // ==== match and rounds

    // A VS match as the fight harness starts one: the VS mode start (FUN_800DAF2C) with the set-up's
    // options, FightMain's match counters, the stage, the seeds, then the fighters (FUN_8002A660).
    void start_vs(const gd::Ref<FightSetup>& fight_setup);

    // The mode overlay was (re)loaded (FightPrepare): its data starts from the overlay's image.
    void mode_overlay_loaded();

    // FightPrepare's FightAllocBuffers: Tekken Ball's ball object, Tekken Force's state (with the
    // overlay's data when it was loaded); the fighting records.
    void prepare_mode();

    // FUN_8002A660: the match's fighters (identity, CPU flag, model, health) and round counters.
    void load_fighters(const gd::PI32& chars, const gd::PI32& costumes, const gd::PI32& cpu_flags);

    // FUN_8002A660 in Tekken Force: the player (side 0's pick, or side 1's when side 0 is the CPU)
    // is record 0; records 1 and 2 are the level's two enemy variants, or Doctor B. and an enemy on
    // the Doctor B. level.
    gd::Arr<gd::PI32> _force_fighters(const gd::PI32& chars, const gd::PI32& costumes, const gd::PI32& cpu_flags);

    // FUN_8004F55C: a record's identity from the character record of its costume key.
    void _identity(const gd::Ref<FighterState>& f, gd::Int character, gd::Int costume);

    // FighterLoadCharacter and FighterSetupParts: the model of the record's costume, its hands and
    // face at rest (FUN_80034010, FUN_80034A08).
    void _load_model(const gd::Ref<FighterState>& f);

    // A new model in a record that already holds one: the record's matrices stay.
    void _reload_model(const gd::Ref<FighterState>& f);

    // FighterSetupParts' resets (FUN_80034010, FUN_80034A08): the face and the hands at rest.
    void setup_parts(const gd::Ref<FighterState>& f);

    // A Tekken Force enemy slot's new variant (force.ovl FUN_800B1778 state 3: FUN_800363FC,
    // FUN_800360B0, FUN_80035F3C): the model of its costume key.
    void reload_enemy(const gd::Ref<FighterState>& f);

    // A record's model was swapped in during the fight.
    void _model_loaded(gd::Int i);

    // FUN_8002D93C: the fighter starts move slot `slot` from its first frame.
    void start_move(const gd::Ref<FighterState>& f, gd::Int slot);

    // The Ogre scene's reload of a fighter in a new costume (FighterLoadCharacter,
    // FighterSetupParts): its model, hands and the players' merged move index.
    void reload_fighter(gd::Int i);

    // FUN_800519B4: a fighter's full health by mode (16.16): arcade by the human count, VS by the
    // handicap chosen on the select screen, team 170, survival 140, practice 200, the demonstration
    // 125, other modes 130 (Tekken Force's enemies take theirs from the level runner).
    gd::Int _health_max(const gd::Ref<FighterState>& f);

    // FUN_80034A08: both hands to the costume's own shape, the face and True Ogre's channels at rest.
    void hands_rest(const gd::Ref<FighterState>& f);

    // FUN_8002AB68: the start of a round.
    void round_start();

    // FUN_8002AB68 in Tekken Force: the player and both enemy records at the origin facing +x (the
    // Doctor B. level: the player at −1000, the others at +1000), placed where they stand; the
    // enemies hidden until their slots bring them in.
    void _force_placement();

    // FUN_8004F5C4: Mokujin takes the moves of a random unlocked character each round (frame
    // generator 0x8009F650 against the unlock mask 0x800982D0).
    void _mokujin_bank(const gd::Ref<FighterState>& f);

    // FUN_8004D13C: one step of the frame generator; its value.
    gd::Int _frame_random();

    // FUN_8006A440: the players' banks (DivmotLinkBank) and DivmotMergeSharedSlots: a slot empty in
    // one bank takes the other bank's move, or slot 0xDC5 when both lack it.
    void _link_banks();

    // An untyped Array of SLOTS MoveRows (null where the bank's slot is empty).
    gd::Arr<gd::Ref<MoveRow>> _slot_rows(gd::Int bank_type);

    // FUN_8002BFCC: the fighter placed facing `opp`, everything the round resets, its stance. A
    // fresh placement puts it at ±1000 facing the other side; otherwise it stays where its anchor
    // and facing are (Tekken Ball and Tekken Force set them first).
    void place(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp, bool fresh = true);

    // The fighter stands at (x, z) facing `facing`, the head and chest joints above it.
    void _set_position(const gd::Ref<FighterState>& f, gd::Int x, gd::Int z, gd::Int facing);

    // The per-round state cleared: movement, reactions, hits, timers and blending.
    void _clear_round_state(const gd::Ref<FighterState>& f);

    // HurtRadiusSetup (0x8003F2DC): the hurt cylinders' radii of the character.
    void _hurt_radii(const gd::Ref<FighterState>& f);

    // FUN_800B2B3C (FightMain sub-state 23, Tekken Force's area change): enemy slot 0 becomes the
    // level's boss (by the player's costume, force.ovl's table), loaded with its model and music,
    // 4,096 units ahead of the camera with the boss health, and starts its entrance (move slot 3).
    void force_boss();

    // FighterStartStance (0x8002D570): the stance move of the round start. In the first round the
    // physical buttons held choose it (or a coin flip).
    void _start_stance(const gd::Ref<FighterState>& f, const gd::Ref<FighterState>& opp);

    // FUN_8002BE54: every fighter's input cleared and its input mode set.
    void set_input_mode(gd::Int mode);

    // The main loop's work before the game state runs (main 0x80028DF4): the frame counters, the
    // random generators its vertical-blank wait steps (once per frame at 60 fps), the display
    // buffer and the pads (FUN_8002A014 with the key configuration `tables`).
    void begin_frame(const gd::PI32& buttons, const gd::Arr<gd::PI32>& tables);

    // One frame of the main loop in the fight: `buttons` are the pads' physical words.
    void step(const gd::PI32& buttons);

    // FightFrame (0x8002B0AC); returns the round state.
    gd::Int fight_frame();

    // The fight's own frame (FightFrame without pause, replay or freeze). Tekken Force runs every
    // per-fighter step for the third record too, tests hits between all three, and ends with its
    // level runner.
    void _frame(const gd::Ref<FighterState>& a, const gd::Ref<FighterState>& b, const gd::Ref<FighterState>& c);

    // FUN_8006DAB4: the background. Tekken Force's panorama keeps the camera's view-space x, which
    // its walls read; a panorama turns with the camera (StageBackgroundDraw; not drawn in Tekken Ball
    // and True Ogre fights). The drawing is the presentation's.
    void background();

    // The effect log (FUN_8004A840 / FUN_8004A860) of the flipbooks and dust rings the events from
    // `from` on start: move events 6-10 as flipbooks, ground impacts and landings as dust rings.
    void _log_effects(gd::Int from);

    // MoveEvents' FighterVibrate and CameraShakeStart calls from `from` on, run in place: the
    // requests are replaced by the events the calls produce.
    void move_event_calls(gd::Int from);

    // FUN_80045260: the previous frame's hit outcome flags.
    static void _clear_hit_flags(const gd::Ref<FighterState>& f);

    // CameraShakeStep, CameraFrame (the view) and FUN_80036254 (the fighters' view).
    void _camera_frame();

    // CameraFrame (the view) and FUN_80036254 (the fighters' view), without the shake: the Ogre
    // scene's camera frame.
    void view_frame();

    // A frame that only draws (pause or freeze): the HUD, the view and the skeletons.
    void _draw_frame();

    // The replay's frame (FightFrame with a playing replay): the blend and the effects run on the
    // even half frames only; Start ends the replay.
    void _replay_frame();

    // FUN_800B0E14 with the frame's camera (the popups keep it).
    void _ball_frame();

    // FUN_8002B9EC: the pause request (Start of a human in round state 1) and its sound. In
    // practice the player's Start pauses (the first pause comes by itself while practice waits).
    void _pause_check();
};

}  // namespace t3
