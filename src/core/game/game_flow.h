#pragma once
// remake/core/game/game_flow.gd
// The whole game as a frame-stepped state machine (modes.md#game-states): the main loop's frame
// work and the handler of the game state 0x800AE6CC, with the game's own states and sub-states,
// so that `tests/app/test_game_flow.gd` can compare every frame with the game running in the
// CPU harness (tools/research/flow_trace.py).
//
// The fight is the session's FightSimulation; its FightState holds the game's globals the fight
// code shares (the mode region, the progress block, the random generators). Screens with state of
// their own are separate objects (MainMenu, QuickSelect, CharacterSelect, VsScreen, the results,
// the ranking, the ending and Theater, the options). Loading takes no time: an overlay load costs
// the frames the loader state takes with an idle disc, as in the harness.
#include <functional>

#include "gd/gd.h"
#include "core/fight/round_hud.h"   // RoundHud::Message
#include "platform/pad_state.h"

namespace t3 {

class FightContent;
class FlowData;
class FightSimulation;
class FightState;
class GameProgress;
class ModeRegion;
class GameGlobals;
class ProgressRules;
class ModeRules;
class RuleSet;
class MainMenu;
class QuickSelect;
class CharacterSelect;
class RankingScreen;
class ResultScreens;
class EndingScreen;
class TheaterScreen;
class OptionsScreen;
class PracticeMode;
class VsScreen;
class MatchFlow;
class EnbuPerformance;

class GameFlow {
public:
    enum State : gd::Int { BOOT = 0, LOADER = 1, TRANSITION = 2, TITLE = 3, MENU = 4, OPTIONS = 5, ENBU = 6,
        PREPARE = 7, FIGHT = 8, SELECT = 9, QUICK_SELECT = 10, VS = 11, TEAM_RESULT = 12,
        TIME_RESULT = 13, SURVIVAL_RESULT = 14, FORCE_RESULT = 15, RANKING_LOAD = 16, RANKING = 17,
        SCREEN_LOADER = 18, ENDING = 19 };

    static constexpr gd::Int FACE_BUTTONS = PadState::FACE_BUTTONS;
    static constexpr gd::Int SOUND_START = 0x4CC0;
    static constexpr gd::Int PRESENTS_FADE_FRAMES = 0x20;
    static constexpr gd::Int PRESENTS_HOLD = 0x5A;
    static constexpr gd::Int WHITE_HOLD = 11;
    static constexpr gd::Int TITLE_FRAMES = 300;
    static constexpr gd::Int MUSIC_ENBU = 0;
    static constexpr gd::Int ENBU_STAGE = 4;
    // Title picture kinds (FUN_800DAAD8): every other kind is a black fade.
    static constexpr gd::Int PICTURE_BLACK = 0;
    static constexpr gd::Int PICTURE_MOVIE = 1;
    static constexpr gd::Int PICTURE_TITLE_PROMPT = 2;
    static constexpr gd::Int PICTURE_TITLE = 5;
    static constexpr gd::Int PICTURE_NONE = 7;
    static constexpr gd::Int PICTURE_FADE = 8;
    static inline const gd::Arr<gd::Int> PICTURES_DRAWN{0, 1, 2, 3, 4, 5, 6, 7};
    // The loader's overlays (the slot and the overlay kept in it).
    static constexpr gd::Int SLOT_MODE = 0;
    static constexpr gd::Int SLOT_SCREEN = 1;
    static constexpr gd::Int OVERLAY_TITLE = 5;
    static constexpr gd::Int OVERLAY_ENBU = 7;
    static constexpr gd::Int OVERLAY_SELECT = 4;
    static constexpr gd::Int OVERLAY_RESULT = 9;
    static constexpr gd::Int OVERLAY_ENDING = 8;
    static constexpr gd::Int OVERLAY_RANKING = 6;

    gd::Ref<FightContent> content;
    gd::Ref<FlowData> data;
    gd::Ref<FightSimulation> sim;
    gd::Ref<FightState> fight;
    gd::Ref<GameProgress> progress;
    gd::Ref<ModeRegion> region;
    gd::Ref<GameGlobals> globals;                  // GameGlobals.new(), made in the constructor
    gd::Ref<ProgressRules> rules;
    gd::Ref<ModeRules> modes;
    gd::Arr<gd::PI32> key_tables;                  // 0x80098290: per player the mapped word of each physical bit

    gd::Int state = State::BOOT;                   // 0x800AE6CC
    gd::Int sub = 0;                               // 0x800AE6EC
    bool movies_available = true;
    gd::Int movie = 0;                             // 0x800B09B8: the title's movie
    gd::Int movie_result = 0;                      // 1 finished, −1 skipped or failed, 0 playing
    // A movie the game plays inside one frame (the ending, Theater): the frames wait for it.
    bool movie_blocking = false;
    gd::Int music_countdown = 0;                   // 0x800A0986: the music stream's start timer (s16)

    // The overlay loader (FUN_80052EC8, FUN_80052F18, FUN_80052FAC).
    gd::Arr<gd::PI32> loader_queue;                // (slot, overlay) per queued load
    gd::Vector2i loader_target{0, 0};              // 0x8009853E/F
    gd::PI32 slots{-1, -1};                        // the overlay in each slot

    // Screens with their own state (created in the constructor with `this`).
    gd::Ref<MainMenu> menu;
    gd::Ref<QuickSelect> quick;
    gd::Ref<CharacterSelect> select;
    gd::Ref<RankingScreen> ranking;
    gd::Ref<ResultScreens> results;
    gd::Ref<EndingScreen> ending;
    gd::Ref<TheaterScreen> theater;
    gd::Ref<OptionsScreen> options;
    gd::Ref<PracticeMode> practice;
    bool escape_pressed = false;                   // the remake's Escape on a pause menu, pending until the menu takes it (not a button of the game's pads)
    // Native: NETWORK MODE. The main menu lists it (below VS MODE) when `network_entry`, as
    // DISCONNECT in a session; choosing it parks the menu in its sub-state 4 (the app's network
    // screens) until `network_back`, an input like the pads, opens the list again. In a session
    // player 2's own characters (its save's unlocked and start costumes; −1: the progress's) widen
    // the selects' grids, and each player picks only their own.
    bool network_entry = false;
    bool network_session = false;
    bool network_back = false;
    gd::Int guest_unlocked = -1;
    gd::Int guest_costumes = -1;
    gd::Int unlocked_of(gd::Int pad) const;
    gd::Int costumes_of(gd::Int pad) const;
    gd::Ref<VsScreen> vs_screen;
    gd::Ref<MatchFlow> match_flow;

    // The demonstration's performance: a callable taking the demonstration number and returning an
    // EnbuPerformance (or null to count `performance_frames` frames instead, for tests).
    // Callable: empty means not valid.
    std::function<gd::Ref<EnbuPerformance>(gd::Int)> performance_factory;
    gd::Ref<EnbuPerformance> performance;
    gd::Int performance_frames = 3549;
    gd::Int _performance_count = 0;
    gd::Int title_kind = PICTURE_FADE;             // 0x800EB290
    gd::Int enbu_kind = PICTURE_FADE;              // 0x800DDE28

    // Per frame: what the screen shows (the sounds and music go out as the fight's SimEvents).
    gd::Int presents_mode = -1;                    // FUN_8004FA38: 0 text fading (level), 1 text, 2 black, 3 nothing
    gd::Int presents_level = 0;
    gd::Int picture = -1;                          // FUN_800DAAD8's picture kind this frame
    gd::PI32 fades;                                // FUN_8004E2E8 levels drawn this frame
    gd::Int movie_started = -1;
    bool performed = false;
    gd::Arr<gd::Ref<RoundHud::Message>> texts;     // FightMain's own texts: CONTINUE?, GAME OVER, A NEW CHALLENGER
    bool save_requested = false;                   // the transition screen saves (auto save)
    // Reads the save file at the first transition screen (FUN_8004C658): returns its data, or an
    // empty array when there is none (the defaults stay). Callable: empty means not valid.
    std::function<gd::PBytes()> card_loader;

    // Makes `globals`, then runs _init: the parts are created with `this`.
    GameFlow(const gd::Ref<FightContent>& fight_content, const gd::Ref<FlowData>& flow_data,
             const gd::Ref<RuleSet>& fight_rules, gd::Int seed);
    GameFlow(const GameFlow&) = delete;
    GameFlow& operator=(const GameFlow&) = delete;

    // One frame of the main loop with the pads' physical buttons.
    void step(const gd::PI32& buttons);

    // The pads as the screens read them.
    gd::Int pressed(gd::Int player);
    gd::Int held(gd::Int player);
    gd::Int repeat(gd::Int player);
    gd::Int pressed_any();

    // SoundPlayFighter(0, code, 0): the menus' sounds.
    void sound(gd::Int code);
    void system_sound(gd::Int code);
    // MusicPlay (the track, −1 stops).
    void music(gd::Int track);
    // FUN_8006C870: the music stops.
    void music_stop();
    // A disc read (BnsStartQueuedLoads: the overlay loader, the menu's overlay, the VS screen's
    // pictures, Tekken Ball's and Force's stages; a movie, as the movie players start):
    // FUN_8006B834(0) pauses the drive, and with it the XA music, which does not resume.
    void disc_read();
    void music_volume(gd::Int volume, gd::Int frames);
    // A movie played within the frame (FUN_8010D474): the frame finishes when it ends.
    void start_blocking_movie(gd::Int id);
    // FUN_8006BCC8: whether the music has started. The remake's music starts at once, as the game's
    // does with no stream playing: the first call of the session only counts the stream's timer
    // down, later calls run FUN_8006AE4C (the volume up).
    bool music_ready();
    // FUN_80029860: the display switch (0: on).
    void display(gd::Int off);
    // FUN_8004FBE0: go to a state through the transition screen. Going to the title plays the next
    // attract demonstration at attract step 1 (or the one R1 / L1 / L1 + R1 choose).
    void goto_transition(gd::Int target);
    static gd::Int target_state(gd::Int s);
    // FUN_80050208: go to a state through the screen-overlay loader.
    void goto_screen_loader(gd::Int target);
    // FUN_80050478.
    void goto_ranking();
    // FUN_80052EC8: a load for the overlay loader.
    void queue_overlay(gd::Int slot, gd::Int overlay);
    // FUN_80052F18: to `to` (and `to_sub`), through the loader when loads are queued.
    void goto_loader(gd::Int to, gd::Int to_sub);
    // FUN_80052FAC (game state 1): one load per frame with an idle disc.
    void _loader();
    // ScreenOverlayLoad (0x8005025C, game state 18).
    void _screen_loader();
    // FUN_800291B0: the vibration off and the controller settings cleared.
    void controllers_reset();
    // The remake's Escape on an open pause menu: leaves it as CANCEL (practice: OK) does; false
    // while the menu is not taking input yet.
    bool _escape();
    // The remake's Escape on a pause menu, once per frame while it is pending: the menu takes it
    // (`_escape`) as soon as it listens. It stays pending through the menu's inert frames and is
    // dropped once taken, when no menu is open or when the menu has left its list (Tekken Ball's HOW TO
    // page, a confirmed item): Escape does not wait for the page to close.
    void _update_escape();
    // A pause menu is open and takes the remake's Escape (the move lists take it as Start).
    bool pause_menu_open();
    // The pad the fight's human plays on (practice: its player's): the one whose Start pauses, or −1
    // with no human (the demonstration).
    gd::Int fight_pad();
    // The pads humans play the fight on, a bit per pad: practice its player's only, else the human
    // mask's.
    gd::Int human_pads();
    // Whether a pad is played by a human in the fight.
    bool plays_fight(gd::Int pad);
    // The pad the remake's Escape presses for when the keyboard is `keyboard_pad`'s, or −1 for that
    // pad itself: in a fight (and its preparation) the human's when the keyboard's player does not
    // play there (Start of a pad nobody plays would join as a challenger).
    gd::Int escape_pad(gd::Int keyboard_pad);
    // The pad whose Start opens the pause now (the press FightSimulation._pause_check reads), or −1
    // when none can: outside a fight or its round state FIGHT, in the demonstration and a practice
    // replay (no human), or with a pause already open.
    gd::Int pause_pad();
    // The pad a tap or click presses Start on, or −1 for none: player 1's, but in a fight the human's
    // (Start of a pad nobody plays there joins as a challenger), and not while a pause is open (the
    // click that brings the window back would resume the fight).
    gd::Int tap_pad();
    // MenuExitCheck (FUN_80051304): Select + Start (Start alone in the demonstration fight) or the
    // pause menu's RESET returns to the main menu.
    bool menu_exit();
    void next_attract_step();
    // FUN_8004FB9C: the next attract step, skipping the title at step 3.
    void attract_advance();
    void _to_menu();
    void _boot();
    void _transition();
    void _queue_menu_overlay();
    void _overlay_loaded();
    void _overlay_ready();
    // AutoSave (FUN_8004C6A0): with AUTO SAVE on and a save pending, the owner writes the save file.
    void auto_save();
    // AutoSaveErrorShow (FUN_8004C758): 120 frames of AUTO SAVE ERROR! after a failed save.
    bool auto_save_error();
    // The owner reports a failed save: AUTO SAVE ERROR! shows for 120 frames.
    void save_failed();
    void _title();
    void _enbu();
    // The engine calls of the performance's move events (PadVibrate and CameraShakeStart run as in
    // the fight; the rest is the demonstration's own presentation).
    void _performance_calls();
    // enbu.ovl FUN_800D3CCC: the performance's globals: a scripted scene (0x800AFF69), the attract
    // flag, no freeze or pause, and fighters without a motion bank until the script sets them.
    void _enbu_setup();
    // FUN_800DAAD8 / FUN_800D2C30: kind 0 black, 1 and 7 nothing, 2–4 the title with the start
    // prompt, the logo or the copyright, 5 the title, 6 the options backdrop, other kinds a black
    // fade (level 0).
    void _draw_picture(gd::Int kind);
    void _fight_prepare();
};

}  // namespace t3
