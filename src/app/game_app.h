#pragma once
// remake/app/game.gd: the game: the whole frame loop of GameFlow from boot, with every mode, the
// screens, the fights, the movies and the music, and the progress kept in the save file.
//
// Each physics tick (60 a second) is one step of GameFlow with both pads' physical words; the views
// of the state the step left read what it did. Development options: a flow trace's pads
// (`replay`, `replay_frames`), the seed, the steps per tick.
//
// TODO (stage 2, slice by slice): the views not yet ported are listed in `_show` with their game.gd
// names (fight_view, hud, mode_hud, pause_view, practice_view, move_list_view, enbu_view,
// backdrop, bars), and so are texture packs (the settings, their screen and button and the save
// card are here: `Settings`, `settings_view`, `settings_button`, `card`).
#include <memory>
#include <string>

#include "app/progress_card.h"
#include "audio/mixer.h"
#include "dev/record_trace.h"
#include "gfx/canvas.h"
#include "gfx3d/renderer.h"
#include "platform/app.h"
#include "platform/connected_pads.h"
#include "platform/input_router.h"
#include "platform/pad_vibration.h"
#include "platform/rumble.h"
#include "platform/settings.h"
#include "presentation/audio/audio_director.h"
#include "presentation/camera/framing_bars.h"
#include "presentation/hud/fight_hud.h"
#include "presentation/hud/mode_hud_view.h"
#include "presentation/hud/pause_menu_view.h"
#include "presentation/hud/touch_controls.h"
#include "presentation/screens/attract_screen.h"
#include "presentation/screens/move_list_view.h"
#include "presentation/screens/practice_view.h"
#include "presentation/screens/ending_view.h"
#include "presentation/screens/fade_overlay.h"
#include "presentation/screens/main_menu_view.h"
#include "presentation/screens/movie_player.h"
#include "presentation/screens/options_view.h"
#include "presentation/screens/quick_select_view.h"
#include "presentation/screens/ranking_view.h"
#include "presentation/screens/result_view.h"
#include "presentation/screens/screen_mask.h"
#include "presentation/screens/select_view.h"
#include "presentation/screens/settings_button.h"
#include "app/perf_log.h"
#include "app/session_log.h"
#include "app/system_options.h"
#include "gfx/gpu_timer.h"
#include "presentation/screens/settings_view.h"
#include "presentation/screens/vs_view.h"

namespace t3 {

class GameFlow;
class FightContent;
class GameRam;
class VramImage;
class HudData;
class TextLocale;
class RuleSet;
class EffectData;
class EnbuAssets;
class EnbuPerformance;
class EnbuView;
class FightView;
class StageBackdrop;

struct GameOptions {
    std::string replay;          // --replay: a flow trace's pads
    gd::Int replay_frames = -1;  // its first n steps (-1: all)
    std::string pads;            // --pads: a pad script (DevOptions' _load_pad_script)
    bool skip_movies = false;    // --no-movies
    gd::Int seed = -1;
    gd::Int speed = 1;           // steps per physics tick
    // DisplayServer.is_touchscreen_available() for Settings.touch_enabled() ("auto"): on Android, on
    // the development host only with `--touch` (the mouse is pointer 0).
#ifdef __ANDROID__
    bool touch = true;
#else
    bool touch = false;
#endif
    // DevOptions.capturing (--screenshot): every frame shows the last step (weight(...) is 1).
    bool capturing = false;
    bool fresh = false;          // --fresh: a fresh card, the save file neither read nor written
    bool replay_writes = false;  // --replay-writes: a replay's card is read and written (testing)
    bool unlock_all = false;     // --unlock: everything open for this session, never saved
    std::string touch_edit;      // --touch-edit KEY: the layout edit open from the start, KEY selected (development)
};

class GameApp : public App {
public:
    static inline const gd::Dict<gd::Int, std::string> OVERLAY_NAMES = [] {
        gd::Dict<gd::Int, std::string> d;
        d.set(0, "arcade"); d.set(1, "practice"); d.set(4, "select"); d.set(5, "title");
        d.set(6, "ranking"); d.set(8, "ending"); d.set(9, "result");
        return d;
    }();

    explicit GameApp(GameOptions options) : options(std::move(options)) {}

    bool start(const std::string& data_root, const std::string& user_dir) override;
    void graphics_lost() override;
    void resize(int width, int height, const SafeArea& safe) override;
    void frame(double now) override;
    // Platform input (keys are Godot key codes, platform/keys.h).
    void pointer(PointerEvent kind, const Pointer& p) override;
    // The settings screen's _input sees the keys and pad buttons too (Godot's Input still has them).
    void key(int key_code, bool down) override {
        settings_view.key(key_code, down);
        Pads.key_event(key_code, down);
    }
    void pad(int device, PadButton button, bool down) override {
        settings_view.joy_button(down);
        Pads.joy_button(device, button, down);
    }
    void pad_axis(int device, int axis, float value) override { Pads.joy_axis(device, axis, value); }
    // Input.joy_connection_changed: InputRouter's handler (the autoload connects first), then
    // game.gd's `_joypads = Input.get_connected_joypads().size()`.
    void pad_connected(int device, bool connected) override {
        if (!_connected_pads.report(device, connected)) return;
        Pads.joy_connection(device, connected);
        _joypads = _connected_pads.size();
        if (flow) _show_touch();   // native: a pad gone with the touch controls off brings them back
    }
    // Native: something besides the touch screen to play with (a pad, or a keyboard used): without
    // one the touch controls stay, whatever the setting says, and SYSTEM OPTION does not offer OFF.
    bool _other_input() const {
#ifdef __ANDROID__
        return _joypads > 0 || Pads.has_keyboard();
#else
        return true;
#endif
    }
    // Native: something player 2 can play with: a device already theirs, the keyboard (its second
    // layout), a second pad, or a pad beside the touch controls. Without it the game's pad mask
    // (0x800A964C) says one controller and the main menu passes VS MODE over.
    bool _second_input() const {
        return Pads.devices[1] != InputRouter::NO_DEVICE || Pads.has_keyboard() || _joypads >= 2 ||
               (_joypads >= 1 && touch.visible);
    }
    // Back closes the touch layout's edit (as its Done) before it reaches the game.
    void back() override {
        // Native: Back closes the own button's wizard (CANCEL), else the layout edit (DONE).
        if (touch.wizard) touch.wizard.reset();
        else if (touch.editing) touch.finish_edit();
        else Pads.back();
    }
    void paused(bool is_paused) override;
    void audio(float* out, int frames, int rate) override;
    void stop() override;

    // The autoload `Settings` (`<user_dir>/settings.json`) and game.gd's `card` (the progress files).
    GameSettings Settings;
    gd::Ref<ProgressCard> card = gd::make<ProgressCard>();

    // The AudioServer and game.gd's `audio` (renamed: App::audio is the platform's callback). The
    // mixer is declared first so that it outlives the director.
    Mixer mixer;
    std::unique_ptr<AudioDirector> audio_director;
    std::unique_ptr<PadVibration> vibration;
    Rumble rumble;               // InputRouter.rumble (the router has no rumble yet)

    gd::Int steps = 0;           // the steps run so far
    gd::Ref<GameFlow> flow;
    InputRouter Pads;            // the autoload `Pads`
    TouchControls touch;         // game.gd's `touch` (with the native layout edit)
    SettingsView settings_view;  // native: opened only for the keyboard's keys (SYSTEM OPTION's KEYBOARD KEYS)
    SystemOptions system_options;   // native: the settings as OPTION MODE's SYSTEM OPTION
    PerfLog perf_log;               // native: the performance log (setting perf_log)
    gfx::GpuTimer gpu_timer;
    // The 60 Hz steps due `elapsed` seconds after the first frame (that one runs step 1). The step
    // boundaries sit a quarter step after the display's refreshes: at 60 and 120 Hz the refreshes
    // fall exactly on them, and the clock's jitter (about a millisecond) gave a refresh no step and
    // the next two (a stutter); a quarter step keeps 4 ms clear at 60 Hz, 1.4 at 90 and 144.
    static constexpr double TICK_PHASE = 0.25;
    static gd::Int ticks_due(double elapsed) {
        return static_cast<gd::Int>(std::floor(elapsed * 60.0 + TICK_PHASE)) + 1;
    }
    static constexpr int TOUCH_BUZZ_MS = 12;          // native: Touch vibration's tap
    static constexpr int TOUCH_BUZZ_STRENGTH = 90;
    SettingsButton settings_button;

private:
    gd::Int _escape_buttons();
    gd::Vector2i _escape_rules();
    void _apply_focus_pause(gd::PI32& pads);
    bool _tap = false;
    gd::Int _logged_state = -1;   // the flow state last written to the log
    gd::Int _prev_pads = 0;       // both pads the step before (Select opens the settings on a press)
    gd::Arr<gd::Vector2i> _pad_script;   // `--pads`: the steps' pads after the replay
    // A development pad script: lines `<steps> <pad 1> [<pad 2>]` (hex words) played in turn once the
    // replay ends (or from boot); `#` starts a comment.
    void _load_pad_script(const std::string& path);
    bool _release_pads = false;   // after the settings: the game gets no buttons until all are released
    // Whether the settings may open: on the main menu's list (not while it opens or leaves).
    bool _settings_allowed() const;
    void _open_settings();
    void _on_settings_closed();
    void _switch_card();
    bool _pause_owed = false;     // the app went to the background in a fight: its pause opens as soon as one can
    gd::Int _fight_loads = -1;
    gd::PI32 _model_loads;        // FightSimulation.model_loads the fight view shows
    bool _fight_view_due = false;
    gd::Int _fight_view_mode = -1;   // native: the mode (ModeRegion::mode) the fight view was built in
    bool _sound_logged = false;   // native: the first key-on was written to the log
    void _set_audio_fighters();
    void _rumble();
    void _apply_rules();
    void _apply_texts();
    void _on_setting_changed(const std::string& key);
    void _forward_setting(const std::string& key);
    bool fresh = false;           // `--fresh` (GameOptions.fresh) or a replay: the save file is left as it is

    void _load();
    void _physics_process();
    gd::PI32 _pads();
    void _step(const gd::PI32& pads);
    void _overlays();
    void _show();
    void _render();
    void _show_touch();
    bool _playback();
    void _start_movie(gd::Int which, bool blocking);
    void _on_movie_finished();
    // Assets.has_group("movies"): the movies were converted and their MPEG-1 transcodes are present.
    bool _movies_present() const;
    void _load_captions();
    gd::Json movies;              // movies/movies.json (with movies)

    GameOptions options;
    std::string root;            // the converted assets (AssetCatalog.ROOT)
    std::string user_dir_;       // the platform's user directory (user://)
    gd::Ref<FightContent> content;
    gd::Ref<HudData> hud_data;
    gd::Ref<GameRam> ram;
    gd::Ref<VramImage> vram_system;
    gd::Ref<TextLocale> locale;
    gd::Ref<RuleSet> rule_set;
    bool _fixes = false;            // the rule set's gameplay fixes
    gd::Ref<FlowData> _flow_data;

    // ---- native: save states (SessionLog: the session's inputs, a load steps a new flow through them)
    static constexpr const char* STATE_FILE = "savestate.bin";
    static constexpr double RESIM_BUDGET = 0.010;   // seconds of a frame a load steps for
    static constexpr int RESIM_BATCH = 64;          // steps between looks at the clock
    static constexpr gd::Int NOTICE_FRAMES = 120;
    struct Resim {
        SessionLog log;
        gd::Ref<GameFlow> flow;
        gd::Ref<RuleSet> rules;
        bool fixes = false;
        size_t step = 0, event = 0;
        bool failed = false;
        double started = 0.0;
        std::vector<gd::PI32> music;   // (kind, a, b) since the last track began
    };
    SessionLog _log;                // this session's, from its start (not in a trace replay)
    bool _logging = false;
    bool _stepping = false;         // inside flow->step (events then belong to the step)
    SessionLog _saved;              // the save state (user://savestate.bin)
    bool _has_saved = false;
    std::unique_ptr<Resim> _resim;  // a load in progress
    gd::Int _notice_frames = 0;
    std::string _perf_note;         // what this frame did that may take long (PerfLog::spike)
    // Native: VERSION on ARCADE (stage_backdrops): the views draw the arcade's models and stages.
    bool _arcade_version() { return Settings.text("stage_backdrops") == "arcade"; }
    void _perf(const std::string& what);
    gd::Ref<GameFlow> _make_flow(gd::Int flow_seed, const gd::Ref<RuleSet>& rules, bool movies_on);
    void _map_blocks();
    void _record(uint8_t kind, int32_t value, std::vector<uint8_t> bytes = {});
    const SessionLog::Event* _replayed(uint8_t kind);
    bool _apply_change(const SessionLog::Event& e);
    std::string _state_path() const;
    void _read_saved_state();
    void _save_state();
    void _start_load();
    void _resimulate(double budget);
    void _note_music(const SimEvents& events);
    void _take_loaded();
    void _notice(const std::string& text, gd::Int frames = NOTICE_FRAMES);
    gd::Ref<FlowTrace> _replay;   // the flow trace a replay reads its pads from
    gd::Int seed = -1;
    gd::PI32 _slots{-1, -1};
    double _start_time = -1;

    std::string _screen_file(const std::string& name) const;
    gd::Ref<VramImage> _select_vram() const;
    void _reload_pictures();

    gd::Ref<ScreenData> screens;
    // Gamepads connected (the title's start prompt): Input.get_connected_joypads().size(), kept by
    // pad_connected from the platforms' reports.
    gd::Int _joypads = 0;
    ConnectedPads _connected_pads;   // Input.get_connected_joypads()
    AttractScreen attract_screen;
    MainMenuView main_menu_view;
    OptionsView options_view;
    SelectView select_view;
    QuickSelectView quick_view;
    VsView vs_view;
    ResultView result_view;
    RankingView ranking_view;
    EndingView ending_view;
    ScreenMask mask;
    MoviePlayer movie;
    FadeOverlay fade;
    bool blackout = false;

    // The fight renderer slice: game.gd's 3D views and the HUD over them.
    static inline const std::string ENBU_STAGE = "e";
    gd::Ref<EffectData> effect_data;
    gd::Ref<EnbuView> enbu_view;
    gd::Ref<EnbuAssets> enbu_assets;
    gd::Ref<FightView> fight_view;
    gd::Ref<StageBackdrop> backdrop;          // the ranking's turning stage
    FightHud hud;
    PauseMenuView pause_view;
    FramingBars bars;
    PracticeView practice_view;
    MoveListView move_list_view;
    ModeHudView mode_hud;
    gfx::Color _clear_colour = gfx::Color::black();   // RenderingServer.set_default_clear_color
    // The 3D renderer with the graphics preset's MSAA and shadow map (made again when they change).
    std::unique_ptr<gfx3d::Renderer3D> renderer3d;
    gfx3d::RenderSettings _render_settings{0, 0};
    double _fraction = 1.0;                   // Engine.get_physics_interpolation_fraction() of this frame
    // The 3D views' visibility the stage views were last told (their NOTIFICATION_VISIBILITY_CHANGED).
    bool _fight_shown = false, _enbu_shown = false, _backdrop_shown = false;
    void _ranking_backdrop(bool shown);
    void _build_fight_view();
    void _replace_fighters();
    gd::Ref<EnbuPerformance> _new_performance(gd::Int number);
    // The attract demonstrations' assets, loaded when first needed.
    gd::Ref<EnbuAssets> _enbu_assets();
    // game.gd's _process: the shown 3D view between the last two steps, its frame for the bars and HUD.
    void _process();
    // The stage views' visibility hooks, where Godot would notify them.
    void _notify_visibility();
    // The renderer for the graphics preset in effect (RenderQuality: MSAA and shadow size).
    void _ensure_renderer();

    std::unique_ptr<gfx::Canvas> canvas;
    int width_ = 0, height_ = 0;
};

}  // namespace t3
