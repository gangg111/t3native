// Native: NETWORK MODE in the app (game_app.h): the network pages behind the main menu's entry,
// the session's start and end, and the steps of both sides.
#include <chrono>

#include "app/game_app.h"
#include "core/fight/fight_state.h"
#include "core/fight/rule_set.h"
#include "core/game/game_flow.h"
#include "core/game/game_progress.h"
#include "core/game/main_menu.h"
#include "core/game/options_screen.h"
#include "core/sim_events.h"
#include "content/flow_data.h"
#include "content/text_locale.h"
#include "core/fight/fight_simulation.h"
#include "gd/io.h"
#include "platform/log.h"
#include "platform/pad_state.h"
#include "presentation/scene/fight_view.h"

namespace t3 {

namespace {

constexpr gd::Int OK_BUTTONS = PadState::START | PadState::CROSS | PadState::CIRCLE | PadState::SQUARE;
constexpr gd::Int BACK_BUTTONS = PadState::SELECT | PadState::TRIANGLE;
constexpr gd::Int WHITE = 6, GREEN = 1;

gd::Int new_seed() { return std::chrono::steady_clock::now().time_since_epoch().count() & 0x7FFFFFFF; }

// FNV-1a of a file's bytes (0 when missing).
uint64_t file_print(const std::string& path) {
    if (!gd::file_exists(path)) return 0;
    gd::PBytes b = gd::read_file(path);
    uint64_t h = 0xCBF29CE484222325ULL;
    for (gd::Int i = 0; i < b.size(); ++i) {
        h ^= static_cast<uint64_t>(b.decode_u8(i));
        h *= 0x100000001B3ULL;
    }
    return h;
}

}  // namespace

// Both sides must run the same program on the same data: the build's time and the converter's
// manifest (its groups, sizes and sources).
net::NetSession::Identity GameApp::_net_identity() {
    net::NetSession::Identity id;
    id.build = std::string(__DATE__) + " " + __TIME__;
    id.data = file_print(gd::path_join(root, "manifest.json"));
    id.name = Platform::device_name();
    return id;
}

// Once a frame: the packets; the pages while the main menu waits in NETWORK MODE; a session's end.
void GameApp::_net_frame(double now) {
    if (!flow) return;
    _net.poll(now);
    if (_net_playing) {
        if (_net.phase() == net::NetSession::Phase::ENDED) {
            _net_end(_net.reason());
        } else if (flow->state == GameFlow::State::MENU && flow->sub == MainMenu::SUB_NETWORK) {
            // DISCONNECT chosen by player 1 (the guest's flow took it from the host's record).
            _net.close(net::NetSession::Reason::LEFT);
            _net_end(_net_host ? net::NetSession::Reason::NONE : net::NetSession::Reason::LEFT);
        } else if (!_net_host && flow->state == GameFlow::State::MENU && flow->sub == 1 && Pads.escape_pressed) {
            // Player 2 leaves with Back on the main menu (its menus are player 1's).
            _net.close(net::NetSession::Reason::LEFT);
            _net_end(net::NetSession::Reason::NONE);
        }
    } else {
        _net_pages(now);
    }
    // What the pages and the session show.
    main_menu_view.page.clear();
    main_menu_view.footer.clear();
    main_menu_view.footer_colour = WHITE;
    net_banner.text.clear();
    bool menu_list = flow->state == GameFlow::State::MENU && flow->sub == 1;
    bool notice = !_net_notice.empty() && now < _net_notice_until;
    if (!_net_playing && _net_page != NetPage::NONE) {
        _net_show_page();
    } else if (_net_playing && _net_waited >= 0 && now - _net_waited > NET_STALL) {
        net_banner.text = _net_host ? "WAITING FOR PLAYER 2" : "WAITING FOR PLAYER 1";
    } else if (_net_playing && menu_list) {
        main_menu_view.footer = _net_host ? "2P CONNECTED" : "PLAYER 1 IS CHOOSING";
    } else if (notice) {
        if (menu_list) main_menu_view.footer = _net_notice;
        else net_banner.text = _net_notice;
    }
    net_banner.visible = !net_banner.text.empty();
}

void GameApp::_net_show_page() {
    std::vector<std::string>& page = main_menu_view.page;
    main_menu_view.page_cursor = _net_cursor;
    switch (_net_page) {
        case NetPage::CHOICE:
            page = {"HOST GAME", "JOIN GAME"};
            main_menu_view.footer = net::lan_available() ? "NETWORK MODE" : "TURN ON WI-FI";
            break;
        case NetPage::HOSTING: {
            page = {"WAITING FOR PLAYER 2", Platform::device_name()};
            main_menu_view.page_cursor = 0;
            std::vector<net::Address> mine = net::local_addresses();
            main_menu_view.footer = mine.empty() ? "TURN ON WI-FI" : mine[0].text();
            main_menu_view.footer_colour = mine.empty() ? WHITE : GREEN;
            break;
        }
        case NetPage::SEARCHING:
            for (const net::NetSession::Offer& o : _net.offers()) page.push_back(o.name);
            if (page.empty()) page.push_back("SEARCHING");
            page.push_back("ENTER ADDRESS");
            main_menu_view.footer = "JOIN GAME";
            break;
        case NetPage::CONNECTING:
            page = {"CONNECTING", _net.peer_name()};
            main_menu_view.page_cursor = 0;
            main_menu_view.footer = "JOIN GAME";
            break;
        case NetPage::MESSAGE:
            page = {_net_message};
            main_menu_view.page_cursor = 0;
            main_menu_view.footer = "NETWORK MODE";
            break;
        default:
            break;
    }
    main_menu_view.page_colours.assign(page.size(), WHITE);
}

// The network pages: the main menu waits in its sub-state 4 while they are open.
void GameApp::_net_pages(double now) {
    bool open = flow->state == GameFlow::State::MENU && flow->sub == MainMenu::SUB_NETWORK;
    if (!open) {
        if (_net_page != NetPage::NONE) {
            _net.close(net::NetSession::Reason::NONE);
            Platform::network_lock(false);
            _net_page = NetPage::NONE;
        }
        return;
    }
    gd::Int held = _prev_pads;   // both pads of the last step (the players', a pad script's)
    if (_net_page == NetPage::NONE) {
        _net_page = NetPage::CHOICE;
        _net_cursor = 0;
        _net_prev = held;
        // No local network: the system's Wi-Fi panel over the game (or Wi-Fi turned on).
        if (!net::lan_available()) Platform::open_wifi_panel();
    }
    gd::Int pressed = held & ~_net_prev;
    _net_prev = held;
    bool up = (pressed & PadState::UP) != 0, down = (pressed & PadState::DOWN) != 0;
    bool ok = (pressed & OK_BUTTONS) != 0, back = (pressed & BACK_BUTTONS) != 0;
    if (Pads.escape_pressed) back = true;
    auto message = [&](const std::string& text, NetPage then) {
        _net_message = text;
        _net_back_page = then;
        _net_page = NetPage::MESSAGE;
        _net_cursor = 0;
    };
    auto search = [&] {
        _net.search(_net_identity(), now);
        _net_page = NetPage::SEARCHING;
        _net_cursor = 0;
    };
    auto mine = [&] {
        return net::NetSession::GuestProgress{static_cast<uint64_t>(gd::Int(flow->progress->unlocked)),
                                              static_cast<uint64_t>(gd::Int(flow->progress->start_costumes))};
    };
    if (_net_asking) {
        std::string text;
        switch (Platform::poll_text(text)) {
            case Platform::TEXT_ENTERED: {
                _net_asking = false;
                net::Address host;
                if (net::Address::parse(text, net::NetSession::PORT, host)) {
                    _net.join(host, mine(), now);
                    _net_page = NetPage::CONNECTING;
                } else {
                    message("WRONG ADDRESS", NetPage::SEARCHING);
                }
                break;
            }
            case Platform::TEXT_CANCELLED: _net_asking = false; break;
            default: break;
        }
        return;
    }
    // The menu's sounds (MainMenu's cursor and choice, as OPTION MODE's leave): after the page's
    // input, by what it changed.
    NetPage page_before = _net_page;
    gd::Int cursor_before = _net_cursor;
    bool acted = false;   // a press that changed no page but did something (the Wi-Fi panel, the address dialog)
    struct Sounds {
        GameApp& app;
        NetPage page;
        gd::Int cursor;
        bool& acted;
        bool pressed;
        ~Sounds() {
            gd::Int code = 0;
            if (pressed && (app._net_page != page || acted || app.flow->network_back)) code = MainMenu::SOUND_CHOOSE;
            else if (app._net_cursor != cursor) code = MainMenu::SOUND_CURSOR;
            if (code == 0) return;
            gd::Ref<SimEvents> events = gd::make<SimEvents>();
            events->add(SimEvents::SOUND, -1, code, 0);
            app.audio_director->handle(events);
        }
    } sounds{*this, page_before, cursor_before, acted, ok || back};
    switch (_net_page) {
        case NetPage::CHOICE:
            if (up || down) _net_cursor ^= 1;
            if (back) {
                flow->network_back = true;   // an input of the next step: the menu's list again
                _net_page = NetPage::NONE;
            } else if (ok && !net::lan_available()) {
                Platform::open_wifi_panel();
                acted = true;
            } else if (ok && _net_cursor == 0) {
                net::NetSession::Start s;
                s.seed = new_seed();
                s.movies = flow->movies_available;
                s.fixes = _fixes;
                s.english = locale->english;
                s.delay = NET_DELAY;
                s.progress = SessionLog::bytes_of(flow->progress->bytes);
                if (_net.host(_net_identity(), s, now)) {
                    Platform::network_lock(true);
                    _net_page = NetPage::HOSTING;
                } else {
                    message("CANNOT HOST GAME", NetPage::CHOICE);
                }
            } else if (ok) {
                search();
            }
            break;
        case NetPage::HOSTING:
            if (_net.phase() == net::NetSession::Phase::PLAYING) {
                _net_begin();
            } else if (back) {
                _net.close(net::NetSession::Reason::NONE);
                Platform::network_lock(false);
                _net_page = NetPage::CHOICE;
                _net_cursor = 0;
            }
            break;
        case NetPage::SEARCHING: {
            gd::Int offers = static_cast<gd::Int>(_net.offers().size());
            gd::Int items = std::max<gd::Int>(offers, 1) + 1;   // the games (or SEARCHING), ENTER ADDRESS
            if (up) _net_cursor = (_net_cursor + items - 1) % items;
            if (down) _net_cursor = (_net_cursor + 1) % items;
            _net_cursor = std::min(_net_cursor, items - 1);
            if (back) {
                _net.close(net::NetSession::Reason::NONE);
                _net_page = NetPage::CHOICE;
                _net_cursor = 1;
            } else if (ok && _net_cursor == items - 1) {
                if (Platform::ask_text("HOST ADDRESS", "")) _net_asking = acted = true;
            } else if (ok && _net_cursor < offers) {
                _net.join(_net.offers()[static_cast<size_t>(_net_cursor)].address, mine(), now);
                _net_page = NetPage::CONNECTING;
            }
            break;
        }
        case NetPage::CONNECTING:
            if (_net.phase() == net::NetSession::Phase::PLAYING) {
                _net_begin();
            } else if (_net.phase() == net::NetSession::Phase::ENDED) {
                using R = net::NetSession::Reason;
                R why = _net.reason();
                message(why == R::VERSION ? "DIFFERENT GAME VERSION"
                        : why == R::DATA  ? "DIFFERENT GAME DATA"
                        : why == R::BUSY  ? "GAME ALREADY FULL"
                                          : "NO ANSWER",
                        NetPage::SEARCHING);
            } else if (back) {
                search();
            }
            break;
        case NetPage::MESSAGE:
            if (ok || back) {
                if (_net_back_page == NetPage::SEARCHING) {
                    search();
                } else {
                    _net.close(net::NetSession::Reason::NONE);
                    _net_page = _net_back_page;
                    _net_cursor = 0;
                }
            }
            break;
        default:
            break;
    }
}

// The session's start on both sides: the same flow (the host's seed, rules, texts and progress)
// at the main menu, and the step counters at 0.
void GameApp::_net_begin() {
    const net::NetSession::Start& s = _net.start_info();
    _net_host = _net.is_host();
    _net_playing = true;
    _net_page = NetPage::NONE;
    _net_asking = false;
    Platform::network_lock(false);
    if (!_net_host) {
        rule_set = RuleSet::original();
        _fixes = s.fixes;
        rule_set->set_gameplay_fixes(_fixes);
    }
    flow = _make_flow(s.seed, rule_set, s.movies);
    flow->network_session = true;
    flow->guest_unlocked = static_cast<gd::Int>(s.guest.unlocked);
    flow->guest_costumes = static_cast<gd::Int>(s.guest.costumes);
    flow->options->system_option = false;   // the settings stay as they are for the session
    SessionLog::put_bytes(flow->progress->bytes, s.progress);
    flow->goto_transition(GameFlow::State::MENU);
    _map_blocks();
    _fight_loads = flow->sim->loads;
    _fight_view_due = false;
    fight_view = nullptr;
    _fight_shown = false;
    _slots = gd::PI32{-1, -1};
    _logged_state = -1;
    vibration->clear();
    movie.stop();
    audio_director->stop_all();
    if (!_net_host) {
        // The host's texts (the flow's tables follow them: the staff roll's rows differ).
        locale->english = s.english;
        flow->data->localize(locale);
        attract_screen.set_locale(locale);
        pause_view.locale = locale;
        select_view.vram = _select_vram();
        _load_captions();
        _reload_pictures();
    }
    _net_step = 0;
    _net_due = 0;
    _net_started = Platform::now();
    _net_waited = -1;
    _net_notice.clear();
    if (_net_host) {
        _logging = true;
        _log = SessionLog{};
        _log.seed = s.seed;
        _log.movies = s.movies;
        _log.fixes = _fixes;
        _log.progress = s.progress;
        _log_net = true;
        _net_events_sent = 0;
        _net_local.assign(s.delay, 0);
        _release_pads = true;
    } else {
        _logging = false;
        _net_next_pad = s.delay;
        _net_records.clear();
    }
    Log::info(std::string("net: session as ") + (_net_host ? "host" : "guest") + " with " + _net.peer_name() + ", seed " +
              std::to_string(s.seed));
    // The views show the new flow before its first step (the guest may wait for it a while).
    _show();
}

// The session's end (`why` NONE: this side chose DISCONNECT). The host plays on with its flow; the
// guest's game is its own again.
void GameApp::_net_end(net::NetSession::Reason why) {
    using R = net::NetSession::Reason;
    double took = Platform::now() - _net_started;
    Log::info("net: session ended (" + std::to_string(static_cast<int>(why)) + ") at step " + std::to_string(_net_step) + " after " +
              std::to_string(took) + " s (" + std::to_string(took > 0 ? _net_step / took : 0.0) + " steps a second)");
    _net.close(R::NONE);
    _net_playing = false;
    _net_waited = -1;
    _net_records.clear();
    _net_inside.clear();
    if (why == R::NONE) {
        _net_notice.clear();
    } else {
        _net_notice = why == R::LEFT      ? (_net_host ? "PLAYER 2 LEFT" : "PLAYER 1 LEFT")
                      : why == R::DESYNC  ? "GAMES WENT APART"
                                          : "CONNECTION LOST";
        _net_notice_until = Platform::now() + NET_NOTICE;
    }
    if (_net_host) {
        flow->network_session = false;
        flow->guest_unlocked = -1;
        flow->guest_costumes = -1;
        flow->options->system_option = true;
        if (flow->state == GameFlow::State::MENU && flow->sub == MainMenu::SUB_NETWORK) flow->network_back = true;
    } else {
        _net_local_game();
    }
}

// The guest's own game again: a new flow with its card's progress at the main menu.
void GameApp::_net_local_game() {
    seed = new_seed();
    rule_set = RuleSet::original();
    flow = _make_flow(seed, rule_set, _net_movies);
    card->load_into(flow->progress);
    _apply_rules();
    flow->goto_transition(GameFlow::State::MENU);
    _map_blocks();
    _fight_loads = flow->sim->loads;
    _fight_view_due = false;
    fight_view = nullptr;
    _fight_shown = false;
    _slots = gd::PI32{-1, -1};
    _logged_state = -1;
    vibration->clear();
    movie.stop();
    audio_director->stop_all();
    _logging = true;
    _log_net = false;
    _log = SessionLog{};
    _log.seed = seed;
    _log.movies = flow->movies_available;
    _log.fixes = _fixes;
    _log.progress = SessionLog::bytes_of(flow->progress->bytes);
    _apply_texts();
    _show();
}

// Player 2's pad plays where the game expects them (the selects on their turn, the VS screen, the
// fights and their pauses, a challenger's Start, the results); the menus are player 1's.
bool GameApp::_net_guest_live() const {
    switch (flow->state) {
        case GameFlow::State::SELECT:
        case GameFlow::State::QUICK_SELECT:
        case GameFlow::State::VS:
        case GameFlow::State::PREPARE:
        case GameFlow::State::FIGHT:
        case GameFlow::State::TEAM_RESULT:
        case GameFlow::State::TIME_RESULT:
        case GameFlow::State::SURVIVAL_RESULT:
        case GameFlow::State::FORCE_RESULT:
            return true;
        default:
            return false;
    }
}

// The host's tick: the steps due since the session began (a stalled tick is made up, up to
// NET_CATCH_UP a tick), each once the guest's pad for it came (the first `delay` steps have none),
// with this side's pad of `delay` steps before; each step's record goes to the guest at once.
void GameApp::_net_host_tick() {
    _net.poll(Platform::now());   // the guest's latest pads (packets are read each tick, not each frame)
    if (!_net_playing || _net.phase() != net::NetSession::Phase::PLAYING) return;
    uint32_t delay = _net.start_info().delay;
    _net_due = std::min(_net_due + 1, _net_step + NET_CATCH_UP);
    while (_net_step < _net_due) {
        uint32_t n = _net_step;
        uint32_t guest = 0;
        if (n >= delay && !_net.guest_pad(n, guest)) {
            if (_net_waited < 0) _net_waited = Platform::now();
            break;
        }
        _net_waited = -1;
        gd::PI32 local = _pads();
        _net_local.push_back(static_cast<uint32_t>(gd::Int(local[0])));
        uint32_t mine = _net_local.front();
        _net_local.pop_front();
        if (!_net_guest_live()) guest = 0;
        steps += 1;
        _step(gd::PI32{gd::Int(mine), gd::Int(guest)});
        _prev_pads = gd::Int(mine) | gd::Int(guest);
        bool check = (n + 1) % NET_CHECK_EVERY == 0;
        _net.put_record(_log.record_bytes(n, _net_events_sent, check, check ? SessionLog::checksum(*flow) : 0));
        _net_step += 1;
        if (!_net_playing) return;
    }
    _net.poll(Platform::now());   // the records out at once
}

// The guest's tick: the host's records (more than one while they pile up), each checked against the
// host's checksum when it has one, then its pad for a step `delay` ahead of what it has run.
void GameApp::_net_guest_tick() {
    _net.poll(Platform::now());   // the host's latest records
    if (!_net_playing || _net.phase() != net::NetSession::Phase::PLAYING) return;
    uint32_t delay = _net.start_info().delay;
    std::vector<uint8_t> bytes;
    while (_net.take_record(bytes)) _net_records.push_back(std::move(bytes));
    if (_net_records.empty()) {
        if (_net_waited < 0) _net_waited = Platform::now();
    } else {
        _net_waited = -1;
        size_t backlog = _net_records.size();
        size_t run = backlog >= 3 ? std::min<size_t>(backlog - 1, NET_CATCH_UP) : 1;
        for (size_t k = 0; k < run && !_net_records.empty(); ++k) {
            SessionLog::Record rec;
            bool parsed = SessionLog::parse_record(_net_records.front(), rec);
            _net_records.pop_front();
            if (!parsed) {
                _net.close(net::NetSession::Reason::DESYNC);
                _net_end(net::NetSession::Reason::DESYNC);
                return;
            }
            for (const SessionLog::Event& e : rec.before) {
                if (e.kind == SessionLog::RULES) {
                    _fixes = e.value != 0;
                    rule_set->set_gameplay_fixes(_fixes);
                }
            }
            _net_inside = rec.inside;
            _net_inside_at = 0;
            steps += 1;
            _step(SessionLog::pads(rec.step), &rec.step);
            _net_step += 1;
            if (rec.has_check && rec.check != SessionLog::checksum(*flow)) {
                Log::error("net: the games went apart at step " + std::to_string(_net_step - 1));
                _net.close(net::NetSession::Reason::DESYNC);
                _net_end(net::NetSession::Reason::DESYNC);
                return;
            }
            if (rec.has_check && _net_step % (10 * NET_CHECK_EVERY) == 0) {
                Log::info("net: the host's checksum agrees at step " + std::to_string(_net_step) + " (state " + gd::str(flow->state) + ")");
            }
            if (!_net_playing) return;
        }
    }
    if (_net_next_pad < _net_step + delay + 2) {
        gd::PI32 local = _pads();
        uint32_t pad = static_cast<uint32_t>(gd::Int(local[0]) | gd::Int(local[1]));
        if (_tap) {
            pad |= PadState::START;
            _tap = false;
        }
        _net.send_pad(_net_next_pad++, pad);
        _net.poll(Platform::now());   // the pad out at once
    }
}

// The guest's callbacks: the result the host's callback gave in this step.
const SessionLog::Event* GameApp::_net_event(uint8_t kind) {
    while (_net_inside_at < _net_inside.size()) {
        const SessionLog::Event& e = _net_inside[_net_inside_at++];
        if (e.kind == kind) return &e;
    }
    return nullptr;
}

}  // namespace t3
