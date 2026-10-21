#include "app/game_app.h"

#include "app/skip_buttons.h"
#include "platform/pad_state.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <system_error>

#include "content/character_model.h"
#include "content/effect_data.h"
#include "content/enbu_assets.h"
#include "content/fight_content.h"
#include "content/flow_data.h"
#include "content/stage_data.h"
#include "core/camera/camera_director.h"
#include "core/camera/camera_view.h"
#include "core/force/tekken_force.h"
#include "core/game/game_mode.h"
#include "core/game/match_flow.h"
#include "core/scripted/enbu_performance.h"
#include "presentation/camera/camera_rig.h"
#include "presentation/fighter/arcade_attachments.h"
#include "presentation/fighter/fighter_view.h"
#include "presentation/render_quality.h"
#include "presentation/scene/enbu_view.h"
#include "presentation/scene/fight_view.h"
#include "presentation/scene/stage_backdrop.h"
#include "gfx/font.h"
#include "gfx3d/shader_program.h"
#include "presentation/stage/stage_lighting.h"
#include "presentation/stage/stage_view.h"
#include "content/game_ram.h"
#include "content/hud_data.h"
#include "content/json_file.h"
#include "content/movie_captions.h"
#include "content/text_locale.h"
#include "content/texture_packs.h"
#include "content/vram_image.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/rule_set.h"
#include "core/game/character_select.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/game_progress.h"
#include "core/game/main_menu.h"
#include "core/game/mode_region.h"
#include "core/game/options_screen.h"
#include "core/game/practice_mode.h"
#include "core/game/ranking_screen.h"
#include "core/game/result_screens.h"
#include "core/fighter/fighter_state.h"
#include "core/sim_events.h"
#include "gd/io.h"
#include "gfx/gl.h"
#include "platform/log.h"
#include "platform/save_store.h"

namespace t3 {

bool GameApp::start(const std::string& data_root, const std::string& user_dir) {
    root = data_root;
    user_dir_ = user_dir;
    // The views that read assets by path themselves (Assets.path in the remake): the arcade
    // attachments' tables (pose, fighter) and the pause menu's picture.
    ArcadeAttachments::assets_root = root;
    pause_view.assets_root = root;
    if (!gd::file_exists(gd::path_join(root, "tables/flow.json"))) {
        Log::error("game: no converted game data in " + root + " (run tools/remake_import/convert.py)");
        return false;
    }
    canvas = std::make_unique<gfx::Canvas>();
    // `user://`: the save files and the settings (made when missing).
    SaveStore::user_dir = user_dir_;
    if (!user_dir_.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(user_dir_), ec);
        // Native: the linked shaders' binaries, so a later run need not compile them again.
        std::string shaders = gd::path_join(user_dir_, "shaders");
        std::filesystem::create_directories(std::filesystem::path(shaders), ec);
        if (!ec) gfx3d::program_cache_dir() = shaders;
    }
    // The autoload Settings (its _ready, before the game's), with the engine's parts it reaches.
    gd::Json manifest = JsonFile::read(gd::path_join(root, "manifest.json"));
    Settings.Pads = &Pads;
    Settings.AudioServer_get_bus_index = [](const std::string& bus) { return gd::Int(Mixer::bus_index(bus)); };
    Settings.AudioServer_set_bus_mute = [this](gd::Int bus, bool mute) { mixer.set_bus_mute(static_cast<int>(bus), mute); };
    Settings.AudioServer_set_bus_volume_db = [this](gd::Int bus, float db) { mixer.set_bus_volume_db(static_cast<int>(bus), db); };
    // Assets.has_group / japanese_texts over the converter's manifest.
    Settings.Assets_has_group = [this, manifest](const std::string& group) {
        const gd::Json& groups = manifest.get("groups");
        if (!groups.is_object() || groups.get(group).to_int() == 0) return false;
        if (group == "music" || group == "movies" || group == "usa") {
            std::error_code ec;
            return std::filesystem::is_directory(std::filesystem::path(gd::path_join(root, group)), ec);
        }
        return true;
    };
    Settings.Assets_japanese_texts = [manifest] {
        const gd::Json& source = manifest.get("source");
        return !source.is_string() || source.str() != "usa";
    };
    Settings.touchscreen_available = options.touch;
    GameSettings::autoload = &Settings;
    Settings._ready();
    // game.gd's _ready: the settings screen and button over everything (`top`), their signals.
    settings_view.Settings = &Settings;
    settings_view.Pads = &Pads;
    settings_view.closed.push_back([this] { _on_settings_closed(); });
    // Native: the settings' "Edit layout" opens the touch controls' layout edit.
    settings_view.edit_layout.push_back([this] { touch.enter_edit(); });
    settings_button.pressed.push_back([this] { _open_settings(); });
    Settings.changed.push_back([this](const std::string& key) { _on_setting_changed(key); });
    _load();
    Pads.escape_rules = [this] { return _escape_rules(); };
    touch.Pads = &Pads;
    touch.ask_text = [](const std::string& title, const std::string& initial) { return Platform::ask_text(title, initial); };
    // Own buttons' forward and back: player 1's fighter on the right turns them, as FighterInput
    // turns the pad (its side flag).
    touch.facing_left = [this] {
        return flow != nullptr && flow->state == GameFlow::State::FIGHT && !flow->fight->fighters.is_empty() &&
               gd::Int(flow->fight->fighters[0]->side_flag) != 0;
    };
    touch.on_press = [this] {
        if (Settings.flag("touch_haptics") && !touch.editing) Platform::vibrate(TOUCH_BUZZ_MS, TOUCH_BUZZ_STRENGTH);
    };
    // Layout edit (native): the placements of an earlier edit.
    if (!user_dir_.empty()) touch.load_layout(gd::path_join(user_dir_, TouchControls::LAYOUT_FILE));
    _show_touch();
    if (!options.touch_edit.empty()) {
        touch.enter_edit();
        if (options.touch_edit == "ADD") touch.open_wizard(0);
        else touch.selected = options.touch_edit;
    }
    return true;
}

// The touch controls where the settings want them, with their macro buttons (hidden while the
// settings are open: the panel takes the taps).
void GameApp::_show_touch() {
    touch.set_scale_share(static_cast<float>(Settings.touch_scale()));
    touch.opacity = static_cast<float>(Settings.number("touch_opacity")) / 100.0f;
    touch.dpad = Settings.text("touch_directions") == "dpad";
    bool wanted = Settings.touch_enabled() || (Settings.touchscreen_available && !_other_input());
    touch.visible = wanted && !settings_view.visible;
    if (!touch.visible) touch.reset();
}

// Whether the settings may open: on the main menu's list (not while it opens or leaves).
bool GameApp::_settings_allowed() const {
    return flow != nullptr && flow->state == GameFlow::State::MENU && flow->sub == 1 && _replay == nullptr;
}

// A tap waiting for the next step is dropped as the settings open or close: the step does not run
// while they are open, and the tap was meant for them (a tap on the gear with the touch controls
// hidden arrives first as an emulated click, which the gear does not take).
void GameApp::_open_settings() {
    if (_settings_allowed() && !settings_view.visible) {
        _tap = false;
        settings_view.open();
        _show_touch();
    }
}

void GameApp::_on_settings_closed() {
    _tap = false;
    _release_pads = true;
    _show_touch();
    if (Settings.number("memory_card") != card->card) {
        _switch_card();
    }
}

// Another memory card: its progress replaces the one in memory (a card with no save starts fresh),
// and the main menu opens again through the transition screen, so that its entries and the key
// configuration follow the new progress.
void GameApp::_switch_card() {
    card->select(Settings.number("memory_card"));
    card->load_into(flow->progress);
    _record(SessionLog::SWITCH_CARD, 0, SessionLog::bytes_of(flow->progress->bytes));
    flow->goto_transition(GameFlow::State::MENU);
}

// The settings' Gameplay fixes on the rule set the game shares (RuleSet is read by the fights and
// the progress rules, so it is changed in place).
void GameApp::_apply_rules() {
    _fixes = Settings.flag("gameplay_fixes");
    rule_set->set_gameplay_fixes(_fixes);
    _record(SessionLog::RULES, _fixes ? 1 : 0);
}

// The game texts' language on everything that shows them (a trace replay keeps the Japanese
// release's, as the traces were recorded).
void GameApp::_apply_texts() {
    locale->english = Settings.text("game_texts") == "english" && !_replay;
    flow->data->localize(locale);
    attract_screen.set_locale(locale);
    pause_view.locale = locale;
    select_view.vram = _select_vram();
    _load_captions();
    _reload_pictures();
}

// The 3D views' own Settings.changed handlers (StageView, StageLighting, FighterView connect to the
// signal in the remake): the app forwards each change to the views it holds.
void GameApp::_forward_setting(const std::string& key) {
    auto stage = [&](const gd::Ref<StageView>& view, const gd::Ref<StageLighting>& lighting) {
        if (view) view->_on_setting_changed(key);
        if (lighting) lighting->_on_setting_changed(key);
    };
    if (fight_view) {
        stage(fight_view->stage_view, fight_view->lighting);
        for (const gd::Ref<FighterView>& f : fight_view->fighters)
            if (f) f->_on_setting_changed(key);
    }
    if (enbu_view) {
        stage(enbu_view->stage_view, enbu_view->lighting);
        for (const auto& slots : enbu_view->views)
            for (const auto& kv : slots)
                if (kv.second) kv.second->_on_setting_changed(key);
    }
    if (backdrop) stage(backdrop->stage_view, backdrop->lighting);
}

void GameApp::_on_setting_changed(const std::string& key) {
    _forward_setting(key);
    if (key == "gameplay_fixes") {
        if (!_replay) {
            _apply_rules();
        }
    } else if (key == "language") {
        // TODO: TranslationServer.set_locale(Settings.locale()) (no translations natively yet).
    } else if (key == "game_texts") {
        _apply_texts();
    } else if (key == "graphics") {
        // TODO: RenderQuality.apply_viewport(get_viewport()).
    } else if (key == "texture_pack" || key == "texture_filter") {
        TexturePacks::clear();
        _reload_pictures();
    } else if (key == "stage_backdrops") {
        // _rebuild_stages: the attract demonstration's stage is built again, the ranking's backdrop
        // when next shown (a fight's view is built for each fight).
        enbu_view = gd::make<EnbuView>();
        enbu_view->set_as_tree_root(true);
        enbu_view->setup(content->stage(ENBU_STAGE), effect_data);
        enbu_view->stage_view->_notification(StageView::NOTIFICATION_ENTER_TREE);
        _enbu_shown = false;
        backdrop = nullptr;
        _backdrop_shown = false;
    } else if (key == "touch_controls" || key == "touch_size" || key == "touch_opacity" || key == "touch_directions") {
        _show_touch();
    }
}

// Every GL object went with the context. The context generation moves on first (gl_generation:
// the old objects are forgotten, never deleted), then the canvas is made again. The textures read
// from files (the effect objects' page and palettes, the flipbooks, the tile-map, item, caption
// and screen pictures) and the kept ones (the floors' fade table) make themselves again when next
// drawn; the VRAM images drop their sprites with TexturePacks' generation, the font, the gaze atlas
// and the movie their pictures when they find them stale (gfx::Texture::stale).
void GameApp::graphics_lost() {
    _perf("graphics lost");
    // The 3D renderer forgets its GL objects (programs, buffers, the meshes' and multimeshes' uploads
    // by gl_generation) and is made again; the tile-map backdrop's render target too. The stages and
    // fighters take their pictures again as after a texture pack change (_on_setting_changed).
    gfx3d::Renderer3D::graphics_lost();
    renderer3d = nullptr;
    canvas = std::make_unique<gfx::Canvas>();
    TexturePacks::clear();
    if (fight_view != nullptr) {
        fight_view->graphics_lost();
        fight_view->stage_view->_on_setting_changed("texture_pack");
        for (const gd::Ref<FighterView>& f : fight_view->fighters) f->_on_setting_changed("texture_pack");
    }
    if (enbu_view != nullptr) {
        enbu_view->stage_view->_on_setting_changed("texture_pack");
        for (const auto& per_fighter : enbu_view->views) {
            for (const auto& kv : per_fighter) kv.second->_on_setting_changed("texture_pack");
        }
    }
    backdrop = nullptr;
    _backdrop_shown = false;
    movie.graphics_lost();
    _load_captions();
    _reload_pictures();
}

void GameApp::_load() {
    content = FightContent::load_from(root);
    locale = TextLocale::load_from(root);
    content->locale = locale;
    effect_data = EffectData::load_from(gd::path_join(root, "effects"));
    hud_data = HudData::load_from(gd::path_join(root, "hud"));
    std::string screens_dir = gd::path_join(root, "screens");
    screens = ScreenData::load_from(screens_dir);
    ram = GameRam::load_from(screens_dir);
    ram->locale = locale;
    vram_system = VramImage::load_system(gd::path_join(screens_dir, "vram_system.bin"));
    vibration = std::make_unique<PadVibration>(content->tables->vibration);
    // `var audio := AudioDirector.new()` entering the tree (its _ready), then Settings.apply_audio.
    audio_director = std::make_unique<AudioDirector>(&mixer, root);
    audio_director->_ready();
    Settings.apply_audio();
    audio_director->use_tables(content->tables);
    // InputRouter.set_motors(player, ...): the player's device's motors.
    PadVibration::Pads_set_motors = [this](gd::Int player, bool small_on, gd::Int large) {
        rumble.set_motors(Pads.devices[player], small_on, large);
    };
    auto data = FlowData::load_from(gd::path_join(root, "tables/flow.json"));
    seed = options.seed;
    fresh = options.fresh;
    if (!options.replay.empty()) {
        _replay = FlowTrace::load_scenario(options.replay);
        if (!_replay) {
            Log::warning("game: no flow trace " + options.replay);
        } else {
            // A replay starts from the trace's progress, never the card's (the host's --replay-writes
            // keeps the card writing, to test the memory card with a trace's pads).
            fresh = !options.replay_writes;
            seed = _replay->header["scenario"]["seed"].to_int();
        }
    }
    // game.gd's _parse_arguments: `--pads=`.
    if (!options.pads.empty()) _load_pad_script(options.pads);
    if (seed < 0) seed = std::chrono::steady_clock::now().time_since_epoch().count() & 0x7FFFFFFF;
    Log::debug("game: seed " + gd::str(seed));
    rule_set = RuleSet::original();
    // A trace replay runs the original rules, as the game does.
    if (!_replay) {
        _apply_rules();
    }
    // _ready: `movie.visible = false`, `movie.finished.connect(_on_movie_finished)`.
    movie._ready(&mixer);
    movie.finished.push_back([this] { _on_movie_finished(); });
    _flow_data = data;
    flow = _make_flow(seed, rule_set, _movies_present() && !options.skip_movies);
    if (flow->movies_available) {
        movies = JsonFile::read(gd::path_join(root, "movies/movies.json"));
    }
    Log::info(std::string("game: ") + (flow->movies_available ? "with movies" : "without movies"));
    card->reads = !fresh;
    // A fresh session (`--fresh`, a replay) leaves the save file as it is.
    card->writes = !fresh;
    card->auto_save_default = !fresh;
    card->unlock_all = options.unlock_all;
    card->defaults = flow->progress->save_data();
    card->select(Settings.number("memory_card"));
    card->fresh_progress(flow->progress);
    // Native: the session's log from here (save states; never in a trace replay or a pad script's
    // development run, whose inputs come from elsewhere).
    _logging = !_replay;
    if (_logging) {
        _log = SessionLog{};
        _log.seed = seed;
        _log.movies = flow->movies_available;
        _log.fixes = _fixes;
        _log.progress = SessionLog::bytes_of(flow->progress->bytes);
        _read_saved_state();
    }
    // Native: the settings as OPTION MODE's SYSTEM OPTION (not in a trace replay: the game's flow
    // as recorded).
    if (!_replay) {
        system_options.Settings = &Settings;
        system_options.view = &settings_view;
        system_options.edit_layout = [this] { touch.enter_edit(); };
        system_options.keyboard_keys = [this] {
            _tap = false;
            settings_view.open();
            if (settings_view.tab != SettingsView::CONTROLS) settings_view.switch_tab(1);
            _show_touch();
        };
        system_options.closed = [this] {
            _tap = false;
            if (Settings.number("memory_card") != card->card) _switch_card();
        };
    }
    if (_replay) {
        // The trace's pokes (unlocks the scenario starts with).
        for (const gd::PI64& poke : _replay->pokes()) flow->progress->put8(poke[0] - GameProgress::BASE, poke[1]);
    }
    _map_blocks();
    // `var enbu_view := EnbuView.new()` added to the game node (the root of its 3D tree here).
    enbu_view = gd::make<EnbuView>();
    enbu_view->set_as_tree_root(true);
    enbu_view->setup(content->stage(ENBU_STAGE), effect_data);
    enbu_view->stage_view->_notification(StageView::NOTIFICATION_ENTER_TREE);
    attract_screen.setup(screens, false);
    main_menu_view.setup(hud_data, TexturePacks::texture(gd::path_join(screens_dir, "title.png")));
    options_view.setup(hud_data, ram, vram_system, screens_dir);
    settings_view.pad_texture = options_view.pads[0];
    select_view.setup(hud_data, ram, vram_system, screens_dir);
    quick_view.setup(hud_data, ram, vram_system);
    vs_view.setup(hud_data, ram, vram_system, screens_dir);
    result_view.setup(hud_data, ram, vram_system, screens_dir);
    ranking_view.setup(hud_data, ram, vram_system, screens_dir);
    ending_view.setup(hud_data, ram, vram_system, screens_dir);
    practice_view.setup(hud_data, ram, vram_system);
    move_list_view.setup(hud_data, ram, vram_system, root);
    mode_hud.setup_modes(hud_data, ram, vram_system, content->mode_ram("force"), screens_dir);
    pause_view.setup(&hud);
    _apply_texts();
}

// A GameFlow with the app's callbacks. Native: each callback's result goes into the session's
// log, or, while a save state is loaded (_resim), comes from its log instead.
gd::Ref<GameFlow> GameApp::_make_flow(gd::Int flow_seed, const gd::Ref<RuleSet>& rules, bool movies_on) {
    gd::Ref<GameFlow> f = gd::make<GameFlow>(content, _flow_data, rules, flow_seed);
    f->performance_factory = [this](gd::Int number) { return _new_performance(number); };
    f->movies_available = movies_on;
    f->card_loader = [this] {
        if (_resim) {
            const SessionLog::Event* e = _replayed(SessionLog::CARD_READ);
            gd::PBytes b;
            if (e != nullptr) {
                b.resize(static_cast<gd::Int>(e->bytes.size()));
                SessionLog::put_bytes(b, e->bytes);
            }
            return b;
        }
        gd::PBytes b = card->read();
        _record(SessionLog::CARD_READ, 0, SessionLog::bytes_of(b));
        return b;
    };
    f->options->save_writer = [this](const gd::PBytes& bytes) {
        if (_resim) {
            const SessionLog::Event* e = _replayed(SessionLog::SAVE_WRITE);
            return e != nullptr ? gd::Int(e->value) : gd::Int(SaveStore::CARD_DONE);
        }
        gd::Int result = card->write(bytes);
        _record(SessionLog::SAVE_WRITE, static_cast<int32_t>(result));
        return result;
    };
    // Native: the settings as OPTION MODE's SYSTEM OPTION (not in a trace replay: the game's flow
    // as recorded).
    if (!_replay) {
        GameFlow* raw = f.get();
        f->options->system_option = true;
        f->options->system_step = [this, raw] {
            if (_resim) {
                const SessionLog::Event* e = _replayed(SessionLog::SYSTEM_STEP);
                return e != nullptr && e->value != 0;
            }
            bool left = system_options.step(*raw, *raw->options);
            _record(SessionLog::SYSTEM_STEP, left ? 1 : 0);
            return left;
        };
    }
    return f;
}

// _map_blocks: the remake's byte blocks at their game addresses, for the screens' drawing.
void GameApp::_map_blocks() {
    ram->map(GameProgress::BASE, flow->progress);
    ram->map(ModeRegion::BASE, flow->region);
    ram->map(CharacterSelect::BASE, flow->select->ctx);
    ram->map(CharacterSelect::CELLS_BASE, flow->select->cells);
    ram->map(RankingScreen::TABLE_BASE, flow->ranking->table);
    ram->map(RankingScreen::NAME_BASE, flow->ranking->name);
    ram->map(RankingScreen::BACKDROP_BASE, flow->ranking->backdrop);
    ram->map(ResultScreens::BASE, flow->results->state);
    ram->map(ResultScreens::BANNER_BASE, flow->results->banner);
    ram->map(PracticeMode::BASE, flow->practice->s);
}

// ---- save states (SessionLog)

// An event of the live session: inside the step running now, else before the next one.
void GameApp::_record(uint8_t kind, int32_t value, std::vector<uint8_t> bytes) {
    if (!_logging || _resim) return;
    SessionLog::Event e;
    e.inside = _stepping;
    e.step = static_cast<uint32_t>(_stepping ? _log.steps.size() - 1 : _log.steps.size());
    e.kind = kind;
    e.value = value;
    e.bytes = std::move(bytes);
    _log.events.push_back(std::move(e));
}

// The next logged event of the replayed step, of this kind; the app's own changes logged before
// it (the rules, another card) are made again on the way. Null (and the load failed) when the log
// and the replay part ways.
const SessionLog::Event* GameApp::_replayed(uint8_t kind) {
    Resim& r = *_resim;
    while (r.event < r.log.events.size()) {
        const SessionLog::Event& e = r.log.events[r.event];
        if (!e.inside || e.step != r.step) break;
        r.event += 1;
        if (e.kind == kind) return &e;
        if (!_apply_change(e)) break;
    }
    r.failed = true;
    return nullptr;
}

// The rules or another card's progress, as the session changed them.
bool GameApp::_apply_change(const SessionLog::Event& e) {
    Resim& r = *_resim;
    if (e.kind == SessionLog::RULES) {
        r.rules->set_gameplay_fixes(e.value != 0);
        r.fixes = e.value != 0;
        return true;
    }
    if (e.kind == SessionLog::SWITCH_CARD) {
        SessionLog::put_bytes(r.flow->progress->bytes, e.bytes);
        r.flow->goto_transition(GameFlow::State::MENU);
        return true;
    }
    return false;
}

std::string GameApp::_state_path() const { return user_dir_.empty() ? "" : gd::path_join(user_dir_, STATE_FILE); }

void GameApp::_read_saved_state() {
    std::string path = _state_path();
    if (path.empty() || !gd::file_exists(path)) return;
    gd::PBytes b = gd::read_file(path);
    SessionLog saved;
    if (saved.parse(SessionLog::bytes_of(b))) {
        _saved = std::move(saved);
        _has_saved = true;
        Log::info("game: a save state of " + std::to_string(_saved.steps.size()) + " steps");
    } else {
        Log::warning("game: " + path + " is not a save state of this version");
    }
}

// SAVE STATE: the log so far and the state's checksum, kept and written to the user folder.
void GameApp::_save_state() {
    _saved = _log;
    _saved.check = SessionLog::checksum(*flow);
    _has_saved = true;
    std::string path = _state_path();
    bool written = false;
    if (!path.empty()) {
        std::vector<uint8_t> data = _saved.serialize();
        std::string temporary = path + ".tmp";
        if (FILE* f = std::fopen(temporary.c_str(), "wb")) {
            written = std::fwrite(data.data(), 1, data.size(), f) == data.size();
            written = std::fclose(f) == 0 && written;
        }
        std::error_code ec;
        if (written) std::filesystem::rename(temporary, path, ec);
        written = written && !ec;
    }
    Log::info("game: state saved at step " + std::to_string(_saved.steps.size()) + (written ? "" : " (not written to a file)"));
    _notice("STATE SAVED");
}

// LOAD STATE: a new flow from the save state's start, stepped through its log over the next frames
// (_resimulate) while this one waits paused.
void GameApp::_start_load() {
    if (!_has_saved) {
        _notice("NO SAVE STATE");
        return;
    }
    _resim = std::make_unique<Resim>();
    Resim& r = *_resim;
    r.log = _saved;
    r.fixes = r.log.fixes;
    r.rules = RuleSet::original();
    r.rules->set_gameplay_fixes(r.fixes);
    r.flow = _make_flow(r.log.seed, r.rules, r.log.movies);
    SessionLog::put_bytes(r.flow->progress->bytes, r.log.progress);
    r.started = Platform::now();
    _notice("LOADING STATE", 1 << 20);
    Log::info("game: loading a state of " + std::to_string(r.log.steps.size()) + " steps");
}

// Steps the loading flow for up to `budget` seconds of this frame; when done, takes it.
void GameApp::_resimulate(double budget) {
    _perf("state load");
    Resim& r = *_resim;
    double until = Platform::now() + budget;
    while (r.step < r.log.steps.size() && !r.failed && Platform::now() < until) {
        for (int k = 0; k < RESIM_BATCH && r.step < r.log.steps.size() && !r.failed; ++k) {
            // The app's changes between steps, then the step with its logged inputs.
            while (r.event < r.log.events.size() && !r.log.events[r.event].inside && r.log.events[r.event].step == r.step) {
                if (!_apply_change(r.log.events[r.event])) r.failed = true;
                r.event += 1;
            }
            const SessionLog::Step& s = r.log.steps[r.step];
            SessionLog::prepare(*r.flow, s);
            r.flow->step(SessionLog::pads(s));
            // Changes logged inside the step after its last callback.
            while (r.event < r.log.events.size() && r.log.events[r.event].inside && r.log.events[r.event].step == r.step) {
                if (!_apply_change(r.log.events[r.event])) r.failed = true;
                r.event += 1;
            }
            if ((s.flags & SessionLog::SAVE_FAILED) != 0) r.flow->save_failed();
            r.flow->fight->state_request = 0;
            _note_music(*r.flow->sim->events);
            r.step += 1;
        }
    }
    if (!r.failed && r.step < r.log.steps.size()) return;
    bool same = !r.failed && SessionLog::checksum(*r.flow) == r.log.check;
    Log::info("game: state " + std::string(same ? "loaded" : "NOT loaded (the replay parted from the save)") + " after " +
              std::to_string(r.step) + " steps in " + std::to_string(Platform::now() - r.started) + " s");
    if (!same) {
        _resim = nullptr;
        _notice("LOAD FAILED");
        return;
    }
    _take_loaded();
}

// The music events since the last track started (MUSIC, MUSIC_TRACK, MUSIC_STOP begin again).
void GameApp::_note_music(const SimEvents& events) {
    for (const gd::Ref<SimEvents::Event>& e : events.items) {
        gd::Int k = e->kind;
        if (k == SimEvents::Kind::MUSIC || k == SimEvents::Kind::MUSIC_TRACK || k == SimEvents::Kind::MUSIC_STOP) {
            _resim->music.clear();
        }
        if (k == SimEvents::Kind::MUSIC || k == SimEvents::Kind::MUSIC_TRACK || k == SimEvents::Kind::MUSIC_STOP ||
            k == SimEvents::Kind::MUSIC_PREPARE || k == SimEvents::Kind::MUSIC_VOLUME) {
            _resim->music.push_back(gd::PI32{k, gd::Int(e->a), gd::Int(e->b)});
        }
    }
}

// The loaded flow in place of the running one: its log goes on from the save state's, the views
// are made again for it, its music plays, and its pause menu closes (Escape, on the next step).
void GameApp::_take_loaded() {
    std::unique_ptr<Resim> r = std::move(_resim);
    flow = r->flow;
    rule_set = r->rules;
    _fixes = r->fixes;
    _log = r->log;
    _log.check = 0;
    _map_blocks();
    _fight_loads = flow->sim->loads;
    _fight_view_due = true;
    _slots = gd::PI32{-1, -1};
    _logged_state = -1;
    vibration->clear();
    movie.stop();
    audio_director->stop_all();
    gd::Ref<SimEvents> music = gd::make<SimEvents>();
    for (const gd::PI32& m : r->music) music->add(m[0], -1, m[1], m[2]);
    audio_director->handle(music);
    flow->escape_pressed = true;
    // The settings' rules now (the save state's were the ones it was made with).
    if (Settings.flag("gameplay_fixes") != _fixes) _apply_rules();
    _notice("STATE LOADED");
}

void GameApp::_notice(const std::string& text, gd::Int frames) {
    pause_view.notice = text;
    _notice_frames = frames;
}

// _apply_texts: the ending movies' captions in the texts' language.
void GameApp::_load_captions() {
    movie.captions = MovieCaptions::load_from(
        locale->file_or("captions.json", gd::path_join(gd::path_join(root, "movies"), "captions.json")));
}

// Assets.has_group("movies"): the manifest's group and the folder; native: every movies.json entry
// has the MPEG-1 transcode of one of its variants (tools/movies_from_disc.py).
bool GameApp::_movies_present() const {
    gd::Json manifest = JsonFile::read(gd::path_join(root, "manifest.json"));
    const gd::Json& groups = manifest.get("groups");
    if (!groups.is_object() || groups.get("movies").to_int() == 0) return false;
    std::string folder = gd::path_join(root, "movies");
    std::string list_path = gd::path_join(folder, "movies.json");
    if (!gd::file_exists(list_path)) return false;
    gd::Json list = JsonFile::read(list_path);
    gd::Int found = 0;
    for (const char* key : {"title", "ending"}) {
        const gd::Json& entries = list.get(key);
        gd::Int n = entries.is_array() ? entries.size() : 0;
        for (gd::Int i = 0; i < n; ++i) {
            if (!entries[i].has("video")) continue;
            if (MoviePlayer::_first_video(folder, entries[i]["video"]).empty()) {
                Log::warning("game: no MPEG-1 movie for " + entries[i].get("name").str() +
                             " (run tools/movies_from_disc.py): playing without movies");
                return false;
            }
            found += 1;
        }
    }
    return found > 0;
}

// A movie of the title (`title`) or of the ending and the Theater (`ending`, played inside a
// frame: the flow waits for it).
void GameApp::_start_movie(gd::Int which, bool blocking) {
    _perf("movie");
    const gd::Json& list = movies.get(blocking ? "ending" : "title");
    gd::Int n = list.is_array() ? list.size() : 0;
    if (which < 0 || which >= n) {
        flow->movie_result = 1;
        return;
    }
    const gd::Json& entry = list[which];
    float level = entry.has("volume") ? static_cast<float>(entry["volume"].num()) : 127.0f;
    if (blocking) {
        audio_director->stop_all();
    }
    if (!movie.play(gd::path_join(root, "movies"), entry, level / 127.0f, blocking ? which : -1)) {
        flow->movie_result = 1;
    }
    Log::info("game: movie " + entry.get("name").str() + (movie.playing ? " playing" : " not played") + " at step " +
              gd::str(steps));
}

void GameApp::_on_movie_finished() {
    Log::info("game: movie ended at step " + gd::str(steps) + " (" + gd::str(movie.video.stream_position) + " s)");
    if (flow->movie_blocking || (flow->state == GameFlow::State::TITLE && flow->sub == 2)) {
        flow->movie_result = 1;
    }
}

std::string GameApp::_screen_file(const std::string& name) const {
    return locale->file_or(name, gd::path_join(gd::path_join(root, "screens"), name));
}

// The select screen's VRAM: the system textures and select.ovl's pictures (the USA names in English).
gd::Ref<VramImage> GameApp::_select_vram() const {
    gd::Ref<VramImage> image = vram_system->duplicate_image();
    image->upload_file(_screen_file("select.tims"));
    return image;
}

// The pictures the 2D views took from TexturePacks when they were set up, again: for the texts'
// language (the USA title) and after the GL context was lost.
void GameApp::_reload_pictures() {
    main_menu_view.title = TexturePacks::texture(_screen_file("title.png"));
    attract_screen.reload_pictures();
    pause_view.reset_pictures();
    for (PsxCanvas* view : {static_cast<PsxCanvas*>(&options_view), static_cast<PsxCanvas*>(&select_view),
                            static_cast<PsxCanvas*>(&vs_view), static_cast<PsxCanvas*>(&result_view)}) {
        view->reload_pictures();
    }
    settings_view.pad_texture = options_view.pads[0];
}

void GameApp::resize(int width, int height, const SafeArea& safe) {
    width_ = width;
    height_ = height;
    touch.set_window(gfx::Vec2(static_cast<float>(width), static_cast<float>(height)), safe);
    // The full-window Controls' size before their next draw (pointer events lay them out).
    settings_view.size = gfx::Vec2(static_cast<float>(width), static_cast<float>(height));
    settings_button.size = settings_view.size;
}

void GameApp::frame(double now) {
    if (!flow) return;
    double cpu_start = Platform::now();   // the performance log's CPU time (`now` may be a fixed step)
    unsigned uploads = gfx::gl_uploads(), programs = gfx::gl_programs();
    // Native: the name of a touch button being typed (Platform::ask_text's dialog).
    if (touch.wizard && touch.wizard->asking) {
        std::string text;
        switch (Platform::poll_text(text)) {
            case Platform::TEXT_ENTERED: touch.text_entered(text); break;
            case Platform::TEXT_CANCELLED: touch.wizard->asking = false; break;
            default: break;
        }
    }
    // Physics ticks at 60 a second (Godot's physics_ticks_per_second); at most 4 to catch up.
    if (_start_time < 0) _start_time = now;
    gd::Int due = ticks_due(now - _start_time);
    gd::Int ticks = 0;
    static gd::Int done = 0;
    // Native: a save state loading: the paused game waits while the new flow steps (RESIM_BUDGET of
    // each frame, so the frames keep coming); its notice stays for NOTICE_FRAMES after.
    if (_resim) {
        _resimulate(RESIM_BUDGET);
        done = due;
    }
    if (_notice_frames > 0 && --_notice_frames == 0) pause_view.notice.clear();
    while (done < due && ticks < 4) {
        _physics_process();
        done += 1;
        ticks += 1;
    }
    if (done < due) done = due;   // too far behind: drop the backlog instead of spiralling
    // Engine.get_physics_interpolation_fraction(): how far the display time is past the last step
    // (step done - 1 ran at _start_time + (done - 1) / 60), 0 to 1.
    _fraction = std::clamp((now - _start_time) * 60.0 + TICK_PHASE - static_cast<double>(done - 1), 0.0, 1.0);
    bool measured = Settings.flag("perf_log") && !_replay;
    _process();
    double render_start = Platform::now();
    // The GPU timer's query is the frame's first GL call, which waits for a free buffer of the
    // window: it belongs to the render time.
    if (measured) gpu_timer.begin();
    _render();
    if (!measured) {
        perf_log.reset();
        _perf_note.clear();
        return;
    }
    gpu_timer.end();
    // Native: the performance log: this frame's update (steps, views) and render times, the GPU
    // times back so far; a long frame's line at once, and a line every PerfLog::WINDOW seconds.
    double elapsed = now - _start_time;
    double end = Platform::now();
    double update_ms = (render_start - cpu_start) * 1000.0, render_ms = (end - render_start) * 1000.0;
    perf_log.frame(elapsed, update_ms, render_ms);
    std::string parts = perf_log.frame_parts();
    if (update_ms + render_ms > PerfLog::SPIKE_MS) {
        if (gfx::gl_programs() != programs) _perf(std::to_string(gfx::gl_programs() - programs) + " shaders linked");
        if (gfx::gl_uploads() != uploads) _perf(std::to_string(gfx::gl_uploads() - uploads) + " textures uploaded");
        Platform::perf_line(PerfLog::spike(elapsed, update_ms, render_ms, static_cast<int>(flow->state), _perf_note) + parts);
    }
    _perf_note.clear();
    gpu_timer.collect(perf_log.gpu_ms);
    std::string line = perf_log.take(elapsed, static_cast<int>(flow->state));
    if (!line.empty()) Platform::perf_line(line);
}

// Native: what this frame did that may take long (the performance log's spike lines).
void GameApp::_perf(const std::string& what) {
    if (_perf_note.find(what) != std::string::npos) return;
    _perf_note += _perf_note.empty() ? what : ", " + what;
}

void GameApp::_physics_process() {
    // Native: an own touch button's combo moves on before the router reads the touch word.
    touch.tick();
    // InputRouter runs first (process_physics_priority -100).
    Pads._physics_process();
    rumble.update();   // the end of InputRouter._physics_process
    // Android's Back backs out of the settings but does not open them from the main menu.
    Pads.back_allowed = flow->state != GameFlow::State::MENU || settings_view.visible;
    // Native: not over the touch controls' layout edit (its Done and Reset are in the top row).
    // Native: the settings are OPTION MODE's SYSTEM OPTION; the gear and Select no longer open the
    // remake's settings screen (it opens only for the keyboard's keys).
    settings_button.visible = false;
    system_options.touch = touch.visible;
    system_options.other_input = _other_input();
#ifdef __ANDROID__
    system_options.keyboard = Pads.has_keyboard();
#else
    system_options.keyboard = true;
#endif
    touch.set_shoulders(TouchControls::shoulders_shown(*flow));
    touch.set_playback(_playback());
    if (Pads.escape_pressed && flow->pause_menu_open()) flow->escape_pressed = true;
    double t = Platform::now();
    for (gd::Int i = 0; i < options.speed; ++i) {
        gd::PI32 pads = _pads();
        steps += 1;
        gd::Int both = gd::Int(pads[0]) | gd::Int(pads[1]);
        if (settings_view.visible) {
            settings_view.handle(both);
        } else {
            _step(pads);
        }
        _prev_pads = both;
    }
    // Native: the performance log's parts of the update.
    auto part = [&](const char* name) {
        double now = Platform::now();
        perf_log.part(name, (now - t) * 1000.0);
        t = now;
    };
    part("steps");
    _rumble();
    // The movie player's _process (and its VideoStreamPlayer's): native, once a tick with the
    // tick's time, so the picture follows the app's 60 Hz steps.
    movie._process(1.0 / 60.0);
    part("movie");
    // Native: the director's Tween (the music fade) as the scene tree would process it, and the
    // music decoded ahead of the audio thread.
    audio_director->_process(1.0 / 60.0);
    part("audio");
    mixer.update();
    part("mixer");
}

// The two fighters whose voices the audio plays.
void GameApp::_set_audio_fighters() {
    const gd::Arr<gd::Ref<FighterState>>& fighters = flow->fight->fighters;
    audio_director->set_fighters(gd::PI32{fighters[0]->costume_slot, fighters[1]->costume_slot},
                                 gd::PI32{fighters[0]->char_id, fighters[1]->char_id});
}

void GameApp::_rumble() {
    vibration->drive(Settings.flag("vibration") && !settings_view.visible);
}

// TouchControls._input takes the touches first, then the GUI: the settings screen (over the whole
// window while open) or the settings button; game.gd's _unhandled_input: with the touch controls
// shown, Start is their button: other taps do nothing. Otherwise a tap or click is the first pad's Start.
void GameApp::pointer(PointerEvent kind, const Pointer& p) {
    if (touch.pointer(kind, p)) return;
    if (settings_view.visible) {
        settings_view.pointer(kind, p);
        return;
    }
    if (settings_button.visible && settings_button.pointer(kind, p)) return;
    if (touch.visible) return;
    if (kind == PointerEvent::DOWN) _tap = true;
}

// Whether the touch controls show only Select and Start: over what Start skips (movies, the
// title and demonstration, replays) and the records pages without a name to enter, which Start
// leaves.
bool GameApp::_playback() {
    if (SkipButtons::skippable(*flow, movie.playing)) return true;
    return (flow->state == GameFlow::State::RANKING_LOAD || flow->state == GameFlow::State::RANKING) &&
           gd::Int(flow->progress->name_entry) == 0;
}

// A mobile app does not step in the background: its pause comes after the return. A fight that was
// not yet open to a pause (its intro) does not pause a player who is back.
void GameApp::paused(bool is_paused) {
    if (is_paused) touch._notification(TouchControls::NOTIFICATION_APPLICATION_PAUSED);
    if (is_paused) Settings._notification(GameSettings::NOTIFICATION_APPLICATION_PAUSED);
    if (!flow) return;
    if (is_paused) _pause_owed = flow->state == GameFlow::State::FIGHT && !flow->pause_menu_open();
    else _pause_owed = _pause_owed && flow->pause_pad() >= 0;
}

// What the keyboard's Escape does on this screen: the pause in a fight, a move list's exit and a
// movie skip (Start), back out of the options and Theater (Select), and back to the main menu from
// the character selects (Start + Select, the game's own way out); elsewhere Select. On an open pause
// menu it presses nothing: GameFlow leaves the menu.
gd::Int GameApp::_escape_buttons() {
    if (movie.playing) return PadState::START;
    if (flow->pause_menu_open()) return 0;
    switch (flow->state) {
        case GameFlow::State::FIGHT:
        case GameFlow::State::PREPARE:
        case GameFlow::State::VS: return PadState::START;
        case GameFlow::State::SELECT:
        case GameFlow::State::QUICK_SELECT: return PadState::START | PadState::SELECT;
        default: return PadState::SELECT;
    }
}

gd::Vector2i GameApp::_escape_rules() {
    if (!flow) return gd::Vector2i(PadState::START, -1);
    return gd::Vector2i(_escape_buttons(), flow->escape_pad(Pads.escape_owner()));
}

// A fight whose app went to the background opens its pause as the human's Start would, as soon as
// the round state takes one; a trace replay keeps its pads.
void GameApp::_apply_focus_pause(gd::PI32& pads) {
    if (!_pause_owed) return;
    if (_replay || !_pad_script.is_empty() || flow->state != GameFlow::State::FIGHT) {
        _pause_owed = false;
        return;
    }
    gd::Int pad = flow->pause_pad();
    if (pad >= 0) {
        pads[pad] |= PadState::START;
        _pause_owed = false;
    }
}

// This step's pads: the players', a trace replay's.
gd::PI32 GameApp::_pads() {
    gd::PI32 pads{Pads.held[0], Pads.held[1]};
    if (_replay) {
        gd::Int frames = _replay->frames;
        if (steps >= (options.replay_frames < 0 ? frames : gd::mini(frames, options.replay_frames))) {
            _replay = nullptr;
        } else {
            pads = gd::PI32{_replay->pad(steps, 0), _replay->pad(steps, 1)};
        }
    }
    if (!_replay && !_pad_script.is_empty()) {
        gd::Vector2i scripted = _pad_script[0];
        _pad_script.remove_at(0);
        pads = gd::PI32{scripted.x, scripted.y};
    }
    return pads;
}

void GameApp::_load_pad_script(const std::string& path) {
    if (!gd::file_exists(path)) {
        Log::warning("game: no pad script " + path);
        return;
    }
    gd::PBytes bytes = gd::read_file(path);
    std::string text;
    gd::Int n = bytes.size();
    for (gd::Int i = 0; i < n; ++i) text.push_back(static_cast<char>(gd::Int(bytes[i])));
    size_t start = 0;
    while (start <= text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(start, end - start);
        start = end + 1;
        line = line.substr(0, line.find('#'));
        std::vector<std::string> words;
        size_t at = 0;
        while (at < line.size()) {
            size_t space = line.find(' ', at);
            if (space == std::string::npos) space = line.size();
            std::string word = line.substr(at, space - at);
            while (!word.empty() && (word.back() == '\r' || word.back() == '\t')) word.pop_back();
            if (!word.empty()) words.push_back(word);
            at = space + 1;
        }
        if (words.size() < 2) {
            continue;
        }
        gd::Int p1 = words.size() > 2 ? std::strtoll(words[2].c_str(), nullptr, 16) : 0;
        gd::Int count = std::strtoll(words[0].c_str(), nullptr, 10);
        gd::Int p0 = std::strtoll(words[1].c_str(), nullptr, 16);
        for (gd::Int i = 0; i < count; ++i) {
            _pad_script.append(gd::Vector2i(p0, p1));
        }
    }
}

// One step of the game and everything that shows it.
void GameApp::_step(const gd::PI32& step_pads) {
    gd::PI32 pads = step_pads;
    // A trace replay keeps its pads.
    if (!_replay) pads = SkipButtons::apply(pads, *flow, movie.playing);
    if (_release_pads) {
        _release_pads = (gd::Int(pads[0]) | gd::Int(pads[1])) != 0;
        pads = gd::PI32{0, 0};
    }
    if (_tap) {
        gd::Int tap_pad = flow->tap_pad();
        if (tap_pad >= 0) pads[tap_pad] |= PadState::START;
        _tap = false;
    }
    _apply_focus_pause(pads);
    bool waiting = flow->movie_blocking;
    gd::Int handled = waiting ? flow->sim->events->items.size() : 0;
    bool in_options = flow->state == GameFlow::State::OPTIONS;
    // A trace replay keeps the recorded two controllers.
    if (!_replay) flow->sim->pads->connected = _second_input() ? 3 : 1;
    // Native: the step's inputs into the session's log (save states), the pause menu's state items.
    if (_logging) {
        flow->fight->state_items = true;
        _log.steps.push_back(SessionLog::capture(*flow, pads));
    }
    _stepping = true;
    flow->step(pads);
    _stepping = false;
    gd::Int state_request = flow->fight->state_request;
    flow->fight->state_request = 0;
    // Leaving OPTIONS saves the options (ProgressCard): the game would keep them until its next
    // auto save.
    bool save = flow->save_requested || (in_options && flow->state != GameFlow::State::OPTIONS &&
                                         card->saves_options(flow->progress));
    if (flow->state != _logged_state) {
        _logged_state = flow->state;
        Log::info("game: state " + gd::str(_logged_state) + " at step " + gd::str(steps));
    }
    // A new fight: its view is built by _show, and the last fight's rumble stops before this step's
    // own starts.
    if (flow->sim->loads != _fight_loads) {
        _fight_loads = flow->sim->loads;
        _fight_view_due = true;
        vibration->clear();
    }
    if (waiting && flow->movie_blocking) {
        if (movie.playing && SkipButtons::skips_blocking_movie(*flow)) {
            // The movie ends; the next step finishes the frame that started it.
            movie.stop();
        }
        return;
    }
    audio_director->soundtrack = flow->progress->bgm;
    audio_director->set_mono(flow->progress->speaker == GameProgress::SPEAKER_MONO);
    audio_director->handle(flow->sim->events, handled);
    gd::Int n_events = flow->sim->events->items.size();
    for (gd::Int i = handled; i < n_events; ++i) {
        const gd::Ref<SimEvents::Event>& e = flow->sim->events->items[i];
        if (e->kind == SimEvents::Kind::VIBRATE) {
            vibration->vibrate(e->a, e->b);
        }
    }
    if (!_sound_logged && audio_director->key_ons > 0) {
        _sound_logged = true;
        Log::info("audio: first sound keyed on at step " + gd::str(steps));
    }
    if (save && card->write(flow->progress->save_data()) != SaveStore::CARD_DONE) {
        flow->save_failed();
        if (_logging && !_log.steps.empty()) _log.steps.back().flags |= SessionLog::SAVE_FAILED;
    }
    if (_logging && state_request == PauseMenu::PAGE_SAVE_STATE) _save_state();
    if (_logging && state_request == PauseMenu::PAGE_LOAD_STATE) _start_load();
    if (flow->movie_started >= 0 && _replay) {
        // The harness's movies end on their first frame.
        while (flow->movie_blocking) {
            flow->movie_result = 1;
            flow->step(pads);
        }
        flow->movie_result = 1;
    } else if (flow->movie_started >= 0) {
        _start_movie(flow->movie_started, flow->movie_blocking);
    }
    if (movie.playing && !flow->movie_blocking && !(flow->state == GameFlow::State::TITLE && flow->sub == 2)) {
        movie.stop();
    }
    _overlays();
    _show();
}

// The overlays the loader put in its slots: their tables join the game memory the views read.
void GameApp::_overlays() {
    for (gd::Int slot = 0; slot < 2; ++slot) {
        if (flow->slots[slot] != _slots[slot]) {
            _slots[slot] = flow->slots[slot];
            if (OVERLAY_NAMES.has(_slots[slot])) {
                _perf("overlay");
                ram->load_overlay(OVERLAY_NAMES.at(_slots[slot]));
            }
        }
    }
}

// The views of the state the step left.
void GameApp::_show() {
    gd::Int state = flow->state;
    gd::Int frames = flow->fight->vblank;
    gd::Int sub = flow->sub;
    bool fight_state = state == GameFlow::State::FIGHT || state == GameFlow::State::PREPARE;
    // The fighters of a new fight: the stage and models are built again; a model swapped in during
    // the fight replaces only its own.
    if (_fight_view_due) {
        _fight_view_due = false;
        _model_loads = flow->sim->model_loads.duplicate();
        _build_fight_view();
        _fight_view_mode = flow->region->mode;
    } else if (flow->sim->model_loads != _model_loads) {
        _replace_fighters();
    }
    // Native: not a fight of another mode. A mode chosen on the main menu passes PREPARE on its way
    // to the character select, and the remake showed the last fight there (the demonstration's
    // after the attract loop: its fighters flashed for a few frames).
    bool fighting = fight_state && fight_view != nullptr && (state == GameFlow::State::PREPARE || sub >= 7) &&
                    gd::Int(flow->region->mode) == _fight_view_mode;
    if (fight_view != nullptr) {
        fight_view->visible = fighting;
        if (fighting) {
            fight_view->apply(flow->sim);
        }
    }
    // The Ogre scene draws no HUD (its engine calls are the stage, the two fighters and the fade).
    bool hud_shown = fighting && !(sub >= MatchFlow::Sub::OGRE_DISPLAY_ON && sub <= MatchFlow::Sub::OGRE_DISPLAY_OFF);
    hud.visible = hud_shown;
    pause_view.visible = hud_shown;
    if (hud_shown) {
        hud.flow_texts = flow->texts.duplicate();
        hud.show_state(flow->sim);
        pause_view.show_state(flow->sim);
    }
    mode_hud.show_state(flow->sim, hud_shown);
    practice_view.visible = fighting && gd::Int(flow->region->mode) == GameMode::PRACTICE;
    if (practice_view.visible) {
        practice_view.show_state(flow.get());
    }
    if (fighting) {
        move_list_view.show_state(flow.get());
    } else {
        move_list_view.visible = false;
    }
    bool enbu = state == GameFlow::State::ENBU;
    if (flow->performed && flow->performance != nullptr && flow->performance->drawn) {
        enbu_view->apply(flow->performance);
    }
    enbu_view->visible = enbu && flow->performed && flow->performance != nullptr && flow->performance->drawn;
    attract_screen.setup_variant(enbu);
    attract_screen.prompt = _joypads >= 2 ? 3 : 1;
    attract_screen.show_frame(flow->presents_mode, flow->presents_level, flow->picture, frames);
    main_menu_view.visible = state == GameFlow::State::MENU;
    if (main_menu_view.visible) main_menu_view.show_state(flow->menu, frames);
    options_view.visible = state == GameFlow::State::OPTIONS;
    if (options_view.visible) options_view.show_state(flow);
    select_view.visible = state == GameFlow::State::SELECT;
    if (select_view.visible) select_view.show_state(flow);
    // The quick select's context lies in the screen overlays' slot: it is read only in its state. Its
    // first sub-states draw nothing (the last visit's frame must not show), but start its counters.
    bool quick = state == GameFlow::State::QUICK_SELECT;
    quick_view.visible = quick && sub > 1;
    if (quick) {
        ram->map(QuickSelect::BASE, flow->quick->ctx);
        quick_view.show_state(flow);
    } else {
        ram->unmap(flow->quick->ctx);
    }
    // The VS screen stays while the fighters load (FightMain's sub-states 3 and 4 draw nothing).
    vs_view.visible = state == GameFlow::State::VS || (state == GameFlow::State::FIGHT && (sub == 3 || sub == 4));
    if (vs_view.visible) vs_view.show_state(flow);
    result_view.visible = state >= GameFlow::State::TEAM_RESULT && state <= GameFlow::State::FORCE_RESULT;
    if (result_view.visible) result_view.show_state(flow);
    ranking_view.visible = state == GameFlow::State::RANKING;
    if (ranking_view.visible) ranking_view.show_state(flow);
    _ranking_backdrop(state == GameFlow::State::RANKING &&
                      flow->ranking->table->u16(RankingScreen::T_MODE) != RankingScreen::TableMode::HIDDEN);
    ending_view.visible = state == GameFlow::State::ENDING && !movie.playing;
    if (ending_view.visible) ending_view.show_state(flow.get());
    mask.visible = !fighting && !enbu_view->visible && !(backdrop != nullptr && backdrop->visible);
    _notify_visibility();
    blackout = flow->globals->display_on == 0 && !movie.playing;
    gd::PI32 levels = flow->fades.duplicate();
    for (ScreenCanvas* view : {static_cast<ScreenCanvas*>(&quick_view), static_cast<ScreenCanvas*>(&result_view),
                               static_cast<ScreenCanvas*>(&ranking_view)}) {
        if (view->visible && view->fade_level != ScreenCanvas::NO_FADE) levels.append(view->fade_level);
    }
    fade.show_levels(levels);
}

// The ranking's stage (ranking.ovl FUN_800C390C picks it, FUN_800C3AAC turns the camera).
void GameApp::_ranking_backdrop(bool shown) {
    if (shown && !_backdrop_shown) _perf("ranking backdrop");
    if (!shown) {
        if (backdrop != nullptr) {
            backdrop->visible = false;
        }
        return;
    }
    gd::Int stage = flow->fight->stage;
    if (backdrop == nullptr || backdrop->number != stage) {
        // backdrop.queue_free(); StageBackdrop.new() added to the game node (its own 3D tree's root).
        backdrop = gd::make<StageBackdrop>();
        backdrop->set_as_tree_root(true);
        backdrop->setup(content->stage_number(stage));
        backdrop->stage_view->_notification(StageView::NOTIFICATION_ENTER_TREE);
        _backdrop_shown = true;
    }
    backdrop->visible = true;
    // RankingScreen makes the turning backdrop camera the game's camera (FUN_80046458).
    backdrop->show_view(flow->sim->camera->view->copy(), flow->sub <= 2);
}

void GameApp::_build_fight_view() {
    _perf("fight view");
    if (fight_view != nullptr) {
        // fight_view.queue_free()
        fight_view = nullptr;
        _fight_shown = false;
    }
    const gd::Ref<FightState>& fight = flow->fight;
    if (fight->fighters[0]->body == nullptr) {
        return;
    }
    gd::Arr<gd::Ref<CharacterModel>> models;
    gd::PI32 slots;
    gd::PI32 chars;
    // The records that fight: Tekken Force's third too (Tekken Ball's third is the ball's
    // stand-in attacker and is not drawn).
    for (gd::Int p = 0; p < fight->count; ++p) {
        const gd::Ref<FighterState>& f = fight->fighters[p];
        models.append(content->model(f->costume_slot));
        slots.append(static_cast<int32_t>(f->costume_slot));
        chars.append(static_cast<int32_t>(f->char_id));
    }
    _set_audio_fighters();
    fight_view = gd::make<FightView>();
    // add_child(fight_view): the game node is the root of its 3D tree here.
    fight_view->set_as_tree_root(true);
    _fight_shown = true;
    gd::Ref<StageData> stage = content->stage_number(fight->stage);
    gd::Ref<StageData> light_stage = nullptr;
    if (gd::Int(fight->mode) == GameMode::FORCE && TekkenForce::tile_only_level(flow->region->fight_index)) {
        light_stage = content->stage_number(TekkenForce::TILE_ONLY_LIGHT_STAGE);
    }
    fight_view->setup(stage, effect_data, models, slots, chars, light_stage);
    fight_view->setup_modes(stage, fight->tables, gd::Int(fight->mode) == GameMode::BALL ? content->mode_ram("volley") : nullptr);
    fight_view->stage_view->_notification(StageView::NOTIFICATION_ENTER_TREE);
    hud.setup(hud_data, slots);
    practice_view.camera = fight_view->rig->camera;
}

// The records whose model changed since the fight view showed them.
void GameApp::_replace_fighters() {
    _perf("fighter swap");
    const gd::Ref<FightState>& fight = flow->fight;
    gd::Int n = _model_loads.size();
    for (gd::Int i = 0; i < n; ++i) {
        if (flow->sim->model_loads[i] == _model_loads[i]) {
            continue;
        }
        _model_loads[i] = flow->sim->model_loads[i];
        const gd::Ref<FighterState>& f = fight->fighters[i];
        if (fight_view != nullptr && i < fight_view->fighters.size()) {
            fight_view->replace_fighter(i, content->model(f->costume_slot), f->costume_slot);
        }
        hud.set_slot(i, f->costume_slot);
    }
    _set_audio_fighters();
}

gd::Ref<EnbuPerformance> GameApp::_new_performance(gd::Int number) {
    _perf("demo load");
    gd::Ref<EnbuAssets> assets = _enbu_assets();
    enbu_view->load_demo(assets->demo(number), assets->models(number));
    return assets->performance(number);
}

// The attract demonstrations' assets, loaded when first needed.
gd::Ref<EnbuAssets> GameApp::_enbu_assets() {
    if (enbu_assets == nullptr) {
        enbu_assets = EnbuAssets::load_from(root);
    }
    return enbu_assets;
}

// The stage views' NOTIFICATION_VISIBILITY_CHANGED: Godot sends it as a 3D view is shown or hidden.
void GameApp::_notify_visibility() {
    bool fight_shown = fight_view != nullptr && fight_view->visible;
    if (fight_view != nullptr && fight_shown != _fight_shown) {
        fight_view->stage_view->_notification(StageView::NOTIFICATION_VISIBILITY_CHANGED);
    }
    _fight_shown = fight_shown;
    bool enbu_shown = enbu_view->visible;
    if (enbu_shown != _enbu_shown) {
        enbu_view->stage_view->_notification(StageView::NOTIFICATION_VISIBILITY_CHANGED);
    }
    _enbu_shown = enbu_shown;
    bool backdrop_shown = backdrop != nullptr && backdrop->visible;
    if (backdrop != nullptr && backdrop_shown != _backdrop_shown) {
        backdrop->stage_view->_notification(StageView::NOTIFICATION_VISIBILITY_CHANGED);
    }
    _backdrop_shown = backdrop_shown;
}

// game.gd's _process: every rendered frame, the 3D view shown between the last two steps.
void GameApp::_process() {
    gfx::Vec2 window(static_cast<float>(width_), static_cast<float>(height_));
    // options.weight(fraction): 1 while capturing; the Interpolation setting off shows the last step.
    double fraction = Settings.flag("interpolation") ? (options.capturing ? 1.0 : _fraction) : 1.0;
    bool surround = Settings.text("framing") == "stage";
    // Each 3D view has its own camera: _render draws the shown one through it (make_current).
    if (fight_view != nullptr && fight_view->visible) {
        fight_view->interpolate(fraction, window);
        bars.frame(fight_view->rig->visible_rect, surround ? fight_view->clear_colour : gfx::Color::black());
        hud.place(fight_view->rig->frame_rect);
        mode_hud.place(fight_view->rig->frame_rect);
        practice_view.place(fight_view->rig->frame_rect);
        move_list_view.place(fight_view->rig->frame_rect);
        _clear_colour = fight_view->clear_colour;
        bars.visible = true;
    } else if (backdrop != nullptr && backdrop->visible) {
        backdrop->interpolate(static_cast<float>(fraction), window);
        _clear_colour = gfx::Color::black();
        bars.visible = false;
    } else if (enbu_view->visible) {
        enbu_view->interpolate(fraction, window);
        bars.frame(enbu_view->rig->visible_rect, gfx::Color::black());
        _clear_colour = gfx::Color::black();
        bars.visible = true;
    } else {
        _clear_colour = gfx::Color::black();
        bars.visible = false;
    }
}

// The renderer for the graphics preset in effect: RenderQuality's MSAA (Viewport.MSAA: 2^n samples)
// and shadow map size.
void GameApp::_ensure_renderer() {
    RenderQuality::Preset p = RenderQuality::current();
    gfx3d::RenderSettings wanted{1 << static_cast<int>(p.msaa), static_cast<int>(p.shadow_size), static_cast<float>(p.scale)};
    if (renderer3d == nullptr || wanted.msaa != _render_settings.msaa || wanted.shadow_size != _render_settings.shadow_size ||
        wanted.scale != _render_settings.scale) {
        renderer3d = std::make_unique<gfx3d::Renderer3D>(wanted);
        _render_settings = wanted;
    }
}

void GameApp::_render() {
    // The 3D view shown and its camera (game.gd's make_current), in _process's order.
    Node3D* scene = nullptr;
    Camera3D* camera = nullptr;
    if (fight_view != nullptr && fight_view->visible) {
        scene = fight_view.get();
        camera = fight_view->rig->camera.get();
        // The tile-map backdrop's SubViewport draws before the scene that shows it.
        fight_view->draw_backdrop(*canvas);
    } else if (backdrop != nullptr && backdrop->visible) {
        scene = backdrop.get();
        camera = backdrop->rig->camera.get();
    } else if (enbu_view->visible) {
        scene = enbu_view.get();
        camera = enbu_view->rig->camera.get();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, width_, height_);
    // RenderingServer's default clear colour, then the 3D world, then the canvas layers over it
    // (gfx::Canvas::begin does not clear; flush draws without depth test into the bound framebuffer).
    glClearColor(_clear_colour.r, _clear_colour.g, _clear_colour.b, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (scene != nullptr && width_ > 0 && height_ > 0) {
        _ensure_renderer();
        renderer3d->render(*scene, *camera, width_, height_, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, width_, height_);
    }
    gfx::Vec2 window(static_cast<float>(width_), static_cast<float>(height_));
    canvas->begin(width_, height_);
    // CanvasLayer 1, in game.gd's order: bars, hud, mode_hud, pause_view, practice_view,
    // move_list_view, attract_screen, then the screens.
    if (bars.visible) bars.draw(*canvas, window);
    if (hud.visible) hud.draw(*canvas, window);
    mode_hud.draw(*canvas, window);
    if (pause_view.visible) pause_view.draw(*canvas, window);
    practice_view.draw(*canvas, window);
    move_list_view.draw(*canvas, window);
    if (attract_screen.visible) attract_screen.draw(*canvas, window);
    for (ScreenCanvas* view : {static_cast<ScreenCanvas*>(&main_menu_view), static_cast<ScreenCanvas*>(&options_view),
                               static_cast<ScreenCanvas*>(&select_view), static_cast<ScreenCanvas*>(&quick_view),
                               static_cast<ScreenCanvas*>(&vs_view), static_cast<ScreenCanvas*>(&result_view),
                               static_cast<ScreenCanvas*>(&ranking_view), static_cast<ScreenCanvas*>(&ending_view)}) {
        view->draw(*canvas, window);
    }
    mask.draw(*canvas, window);
    movie.draw(*canvas, window);
    // CanvasLayer 10.
    if (blackout) canvas->draw_rect(gfx::Rect2(gfx::Vec2(0, 0), window), gfx::Color::black());
    fade.draw(*canvas, window);
    // TODO: message, stats (game.gd's layer 10 order: blackout, fade, message, stats, touch, settings_button, settings_view).
    if (touch.visible) touch.draw(*canvas, window);
    if (settings_button.visible) settings_button.draw(*canvas, window);
    if (settings_view.visible) settings_view.draw(*canvas, window);
    // Native: a shader the device's driver rejected, written over the game (a screenshot shows it).
    if (!gfx3d::shader_errors().empty()) {
        gfx::FontRef font = gfx::Font::fallback();
        float y = 40;
        for (const std::string& e : gfx3d::shader_errors()) {
            canvas->draw_rect(gfx::Rect2(8, y - 26, window.x - 16, 34), gfx::Color(0, 0, 0, 0.75f));
            font->draw_string(*canvas, gfx::Vec2(16, y), e, gfx::HorizontalAlignment::LEFT, window.x - 32, 22,
                              gfx::Color(1.0f, 0.35f, 0.3f));
            y += 38;
        }
    }
    canvas->flush();
}

// The audio thread: the mixer drains the director's commands and mixes (audio/mixer.h).
void GameApp::audio(float* out, int frames, int rate) {
    mixer.mix(out, frames, rate);
}

// game.gd's _quit: every sound stopped and both players' motors still.
void GameApp::stop() {
    Settings.save();
    movie.stop();
    if (audio_director) audio_director->stop_all();
    if (vibration) PadVibration::stop_motors();
    rumble.update();
}

}  // namespace t3
