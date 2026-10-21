// remake/tests/app/test_game_flow.gd: the whole game against the game's own code
// (`work/traces/flow_<scenario>.bin`, tools/research/flow_trace.py): from boot with the same pads
// frame by frame, the game state and sub-state, the engine calls, the progress block, the mode
// region, the players' globals, the screens' state and, in fights, the fighters are compared with
// the game's.
// Not ported: test_force_pickup_flash (it checks StageLighting, a presentation class).
#include <algorithm>
#include <cstdio>

#include "content/enbu_assets.h"
#include "content/fight_content.h"
#include "content/flow_data.h"
#include "core/ball/tekken_ball.h"
#include "core/camera/backdrop_turn.h"
#include "core/camera/camera_director.h"
#include "core/camera/camera_view.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/rule_set.h"
#include "core/fighter/fighter_input.h"
#include "core/fighter/fighter_state.h"
#include "core/game/byte_block.h"
#include "core/game/character_select.h"
#include "core/game/game_flow.h"
#include "core/game/game_globals.h"
#include "core/game/game_progress.h"
#include "core/game/mode_region.h"
#include "core/game/mode_start.h"
#include "core/game/options_screen.h"
#include "core/game/practice_mode.h"
#include "core/game/quick_select.h"
#include "core/game/ranking_screen.h"
#include "core/game/result_screens.h"
#include "core/math/c_rand.h"
#include "core/sim_events.h"
#include "platform/pad_state.h"
#include "support/fight_compare.h"
#include "support/test_suite.h"

using namespace t3;
using t3test::FlowTrace;
using t3test::RecordTrace;
using Lines = std::vector<std::string>;

namespace {

constexpr size_t MAX_REPORTED = 30;
constexpr gd::Int REFILL_RULE = 4;
constexpr gd::Int DRAIN_RULE = 3;
constexpr gd::Int PRACTICE_FREE_PAGE = 476;

struct Global {
    const char* name;
    gd::Int address, size;
};
const Global DIRECTOR_GLOBALS[] = {{"intro counter", 0x8009892CLL, -4}, {"winner style", 0x800A0844LL, -4}};
const Global BACKDROP_GLOBALS[] = {{"backdrop angle", 0x800A9650LL, 4}, {"backdrop prev yaw", 0x800A9658LL, 4},
                                   {"backdrop yaw", 0x800A9664LL, 4}, {"look point x", 0x800AE0D8LL, -4},
                                   {"look point z", 0x800AE0E0LL, -4}};
const gd::Int VIEW_SUBS[] = {8, 17};
const Global VIEW_FIELDS[] = {{"pitch", 0x800A8A88LL, -4}, {"yaw", 0x800A8A8CLL, -4}, {"x", 0x800A8A9CLL, -4},
                              {"y", 0x800A8AA0LL, -4}, {"z", 0x800A8AA4LL, -4}};
const Global GLOBALS[] = {
    {"char 0", 0x800AE224LL, 2}, {"char 1", 0x800AE226LL, 2}, {"costume 0", 0x800AE260LL, 2},
    {"costume 1", 0x800AE262LL, 2}, {"cpu 0", 0x800A95F0LL, 2}, {"cpu 1", 0x800A95F2LL, 2},
    {"active 0", 0x800AE6C0LL, 2}, {"active 1", 0x800AE6C2LL, 2}, {"keep 0", 0x800AE484LL, 2},
    {"keep 1", 0x800AE486LL, 2}, {"other player", 0x800B0A06LL, 1}, {"win streak", 0x800AE218LL, 2},
    {"tie choice", 0x800AE406LL, 1}, {"save pending", 0x800AE428LL, 1}, {"challenger lock", 0x800AE6DALL, 2},
    {"state timer", 0x800AE6DCLL, -4}, {"stage", 0x800AE14CLL, 2}, {"music", 0x800AE1D0LL, 2},
    {"round time", 0x800AE3C0LL, 2}, {"human mask", 0x800AE3D8LL, 2}, {"difficulty", 0x800AE6D0LL, 2},
    {"level", 0x800AE6D9LL, 1}, {"attract", 0x800AE39CLL, 2}, {"hud shown", 0x800AE6C8LL, 2},
    {"rand", 0x800A3E80LL, 4}, {"frame rng", 0x8009F650LL, 4}, {"camera rng", 0x800A95F4LL, 4},
    {"pad repeat 0", 0x800AE3C8LL, 2}, {"pad repeat 1", 0x800AE3CALL, 2},
};
const gd::Int RESULT_STATES[] = {GameFlow::State::TEAM_RESULT, GameFlow::State::TIME_RESULT, GameFlow::State::SURVIVAL_RESULT};
std::vector<gd::Int> result_fields(gd::Int state) {
    if (state == GameFlow::State::TEAM_RESULT) return {0x00, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x20, 0x230};
    if (state == GameFlow::State::TIME_RESULT) return {0x190, 0x194, 0x198, 0x19C, 0x1A0, 0x1A4, 0x1A8, 0x1AC, 0x1B0, 0x230};
    return {0x1B8, 0x1E0, 0x1E4, 0x1E8, 0x1EC, 0x1F0, 0x1F4, 0x1F8, 0x1FC, 0x200, 0x230};
}
struct RankingField {
    const char* name;
    gd::Int at, size, after;
};
const RankingField RANKING_FIELDS[] = {{"mode", 0, 2, 2}, {"y", 2, 2, 5}, {"target", 4, 2, 6}, {"hold", 6, 2, 2},
                                       {"frames", 8, 2, 2}, {"alpha", 0xA, 2, 6}, {"rows", 0xC, 2, 2}, {"group", 0x50, 4, 5},
                                       {"shown", 0x54, 4, 5}, {"name row", 0x86, 2, 0xB}, {"entry", 0x88, 2, 2}};
const gd::Int QUICK_FIELDS[] = {0x00, 0x08, 0x0C, 0x18, 0x20, 0x24, 0x3C, 0x40, 0x44, 0x50, 0x70,
                                0x7C, 0xC4, 0xCC, 0xD0, 0xE8, 0xEC, 0xF0, 0xFC, 0x11C, 0x128};
const Global FIGHTER_FIELDS[] = {{"charId", 0x18, -2}, {"costume key", 0x14, -2}, {"isCpu", 0xC5, 1}, {"health", 0x3F4, -4},
                                 {"roundWins", 0x44, -2}, {"posX", 0x00, -4}, {"posZ", 0x08, -4}, {"poseFrame", 0x58, -2}};
const gd::Int BALL_SPANS[] = {0x00, 0x04, 0x68, 0x74, 0x78, 0x84, 0x88, 0x94, 0x98, 0xA6, 0xA8, 0xB4, 0xB8, 0x152};
const gd::Int VOLLEY_SPANS[] = {0x645C, 0x645D, 0x68E8, 0x6908, 0x6948, 0x6A68, 0x6AD8, 0x6AEC, 0x6B40, 0x6B58};
const gd::Int PRACTICE_SPANS[] = {0x00, 0x54, 0x74, 0x8C, 0x9D, 0xAD, 0xB0, 0x1F8};

std::string hex(gd::Int v, int digits) {
    char b[24];
    std::snprintf(b, sizeof b, "%0*llX", digits, static_cast<unsigned long long>(v & 0xFFFFFFFFLL));
    return b;
}
std::string join(const Lines& v) {
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ", \"" : "\"") + v[i] + "\"";
    return s + "]";
}

struct Shared {
    gd::Ref<FightContent> content;
    gd::Ref<FlowData> data;
    gd::Ref<EnbuAssets> enbu;
};
Shared& shared() {
    static Shared s;
    return s;
}

// TestSuite.imported_flow: a GameFlow on the converted assets.
gd::Ref<GameFlow> imported_flow(t3test::Context& t, gd::Int seed = 1) {
    if (!t.require(t3test::imported("tables/flow.json"))) return nullptr;
    Shared& s = shared();
    if (!s.content) {
        s.content = FightContent::load_from(t3test::paths().imported);
        s.data = FlowData::load_from(t3test::imported("tables/flow.json"));
    }
    return gd::make<GameFlow>(s.content, s.data, RuleSet::original(), seed);
}

void use_enbu(GameFlow& flow) {
    Shared& s = shared();
    if (!s.enbu) s.enbu = EnbuAssets::load_from(t3test::paths().imported);
    gd::Ref<EnbuAssets> enbu = s.enbu;
    flow.performance_factory = [enbu](gd::Int number) { return enbu->performance(number); };
}

void apply_pokes(GameFlow& flow, const FlowTrace& trace) {
    for (const gd::PI64& poke : trace.pokes()) flow.progress->put8(poke[0] - GameProgress::BASE, poke[1]);
}

// One frame as the harness plays it: its movies end on their first frame.
void step_flow(GameFlow& flow, const gd::PI32& pads) {
    flow.step(pads);
    while (flow.movie_blocking) {
        flow.movie_result = 1;
        flow.step(pads);
    }
    if (flow.movie_started >= 0) flow.movie_result = 1;
}

gd::PI32 pads_of(const FlowTrace& trace, gd::Int frame) { return gd::PI32{trace.pad(frame, 0), trace.pad(frame, 1)}; }

// flow_trace.refill / flow_trace.drain, after the frame (as the harness does).
void refill(GameFlow& flow, const gd::Json& rule) {
    if (flow.state != GameFlow::State::FIGHT || gd::Int(flow.region->mode) != 8) return;
    gd::Int below = rule[1].to_int();
    auto f = flow.fight->fighters[0];
    if (f->health > 0 && f->health < below) f->health = f->health_max;
    if (flow.fight->round_state != 1) return;
    gd::Int timer_below = rule[2].to_int();
    gd::Int timer_refill = rule[3].to_int();
    if (flow.fight->timer > 0 && flow.fight->timer < timer_below) flow.fight->timer = timer_refill;
}

void drain(GameFlow& flow, const gd::Json& rule) {
    if (flow.state != GameFlow::State::FIGHT || flow.fight->round_state != 1) return;
    gd::Int limit = rule[2].to_int();
    for (gd::Int i = 0; i < 2; ++i) {
        auto f = flow.fight->fighters[i];
        if (f->is_cpu != 0 && f->health > limit) f->health = limit;
    }
}

gd::Int global_of(GameFlow& flow, const std::string& name) {
    const GameGlobals& gl = *flow.globals;
    const FightState& fight = *flow.fight;
    if (name == "char 0") return gl.player_char[0];
    if (name == "char 1") return gl.player_char[1];
    if (name == "costume 0") return gl.player_costume[0];
    if (name == "costume 1") return gl.player_costume[1];
    if (name == "cpu 0") return gl.player_cpu[0];
    if (name == "cpu 1") return gl.player_cpu[1];
    if (name == "active 0") return gl.player_active[0];
    if (name == "active 1") return gl.player_active[1];
    if (name == "keep 0") return gl.player_keep[0];
    if (name == "keep 1") return gl.player_keep[1];
    if (name == "other player") return gl.other_player;
    if (name == "win streak") return gl.win_streak;
    if (name == "tie choice") return fight.tie_choice;
    if (name == "save pending") return gl.save_pending;
    if (name == "challenger lock") return fight.challenger_lock;
    if (name == "state timer") return flow.sim->state_timer;
    if (name == "stage") return fight.stage;
    if (name == "music") return fight.music;
    if (name == "round time") return fight.round_time_option;
    if (name == "human mask") return fight.human_mask;
    if (name == "difficulty") return fight.ai_difficulty;
    if (name == "level") return fight.ai_level;
    if (name == "attract") return fight.attract;
    if (name == "hud shown") return fight.hud_shown;
    if (name == "rand") return fight.rng->state;
    if (name == "frame rng") return fight.frame_rng;
    if (name == "camera rng") return fight.camera_rng;
    if (name == "pad repeat 0") return flow.sim->pads->repeat[0];
    if (name == "pad repeat 1") return flow.sim->pads->repeat[1];
    return 0;
}

gd::Int fighter_of(const FighterState& f, const std::string& name) {
    if (name == "charId") return f.char_id;
    if (name == "costume key") return f.costume_key;
    if (name == "isCpu") return f.is_cpu;
    if (name == "health") return f.health;
    if (name == "roundWins") return f.round_wins;
    if (name == "posX") return f.pos_x;
    if (name == "posZ") return f.pos_z;
    if (name == "poseFrame") return f.pose_frame;
    return 0;
}

void blocks(Lines& out, const std::string& name, const ByteBlock& ours, gd::Int base, const FlowTrace& trace, gd::Int frame) {
    gd::PBytes theirs = trace.block(frame, base, ours.bytes.size());
    if (theirs == ours.bytes) return;
    gd::Int shown = 0;
    for (gd::Int i = 0; i < ours.bytes.size(); ++i) {
        if (ours.bytes[i] != theirs[i]) {
            out.push_back(name + " 0x" + hex(base + i, 8) + ": " + hex(ours.bytes[i], 2) + " != " + hex(theirs[i], 2));
            if (++shown >= 8) return;
        }
    }
}

void camera(Lines& out, GameFlow& flow, const FlowTrace& trace, gd::Int frame) {
    const CameraDirector& cam = *flow.sim->camera;
    gd::Int ours[] = {cam.intro_counter, cam.winner_style};
    for (size_t k = 0; k < 2; ++k) {
        const Global& g = DIRECTOR_GLOBALS[k];
        gd::Int expected = trace.ram(frame, g.address, g.size);
        gd::Int actual = RecordTrace::width(ours[k], g.size);
        if (actual != expected) out.push_back(std::string(g.name) + ": " + gd::str(actual) + " != " + gd::str(expected));
    }
    const BackdropTurn& b = *flow.fight->backdrop;
    gd::Int turn[] = {b.angle, b.prev_yaw, b.yaw, b.point_x, b.point_z};
    if (trace.has_range(0x800A9650LL)) {
        for (size_t k = 0; k < 5; ++k) {
            const Global& g = BACKDROP_GLOBALS[k];
            gd::Int expected = trace.ram(frame, g.address, g.size);
            gd::Int actual = RecordTrace::width(turn[k], g.size);
            if (actual != expected) out.push_back(std::string(g.name) + ": " + gd::str(actual) + " != " + gd::str(expected));
        }
    }
    if (std::find(std::begin(VIEW_SUBS), std::end(VIEW_SUBS), flow.sub) == std::end(VIEW_SUBS)) return;
    const CameraView& view = *cam.view;
    gd::Int values[] = {view.pitch, view.yaw, view.x, view.y, view.z};
    for (size_t k = 0; k < 5; ++k) {
        gd::Int expected = trace.ram(frame, VIEW_FIELDS[k].address, -4);
        gd::Int actual = RecordTrace::width(values[k], -4);
        if (actual != expected) out.push_back(std::string("camera ") + VIEW_FIELDS[k].name + ": " + gd::str(actual) + " != " + gd::str(expected));
    }
}

struct Run {
    gd::Int options_page = -1;
    gd::Ref<TekkenBall> seeded_ball;
};

void options(Lines& out, Run& run, GameFlow& flow, const FlowTrace& trace, gd::Int frame) {
    const OptionsScreen& o = *flow.options;
    // A page's own words are compared once the page has run a frame.
    bool ran = run.options_page == o.page;
    run.options_page = o.page;
    gd::Int page = trace.ram(frame, 0x800EC5E8LL, 1);
    if (o.page != page) out.push_back("options page: " + gd::str(o.page) + " != " + gd::str(page));
    for (gd::Int p = 0; p < 6; ++p) {
        gd::Int cursor = trace.ram(frame, 0x800EB2F0LL + 0x34 * p, 4);
        gd::Int sub = trace.ram(frame, 0x800EB2F4LL + 0x34 * p, 4);
        if (o.cursors[p] != cursor || o.subs[p] != sub)
            out.push_back("options page " + gd::str(p) + ": cursor " + gd::str(o.cursors[p]) + " sub " + gd::str(o.subs[p]) +
                          " != " + gd::str(cursor) + ", " + gd::str(sub));
    }
    gd::Int card = trace.ram(frame, 0x800EC63CLL, 1);
    if (o.page == OptionsScreen::PAGE_CARD && ran && o.card_state != card)
        out.push_back("card state: " + gd::str(o.card_state) + " != " + gd::str(card));
    if (o.page == OptionsScreen::PAGE_KEYS && ran) {
        for (gd::Int p = 0; p < 2; ++p) {
            gd::Int at = 0x800EC640LL + 0x1E8 * p;
            const auto& rec = *o.keys[p];
            const char* names[] = {"selected", "armed", "busy", "used"};
            gd::Int values[] = {rec.selected, rec.armed, rec.busy, rec.used};
            gd::Int offsets[] = {4, 8, 0xC, 0x24};
            for (size_t k = 0; k < 4; ++k) {
                gd::Int expected = trace.ram(frame, at + offsets[k], 4);
                if (values[k] != expected)
                    out.push_back("key config " + gd::str(p) + " " + names[k] + ": " + gd::str(values[k]) + " != " + gd::str(expected));
            }
        }
    }
    if (o.page == OptionsScreen::PAGE_RECORDS && o.subs[OptionsScreen::PAGE_RECORDS] != 0) {
        gd::Int sub = trace.ram(frame, 0x800ECA10LL, 4);
        if (o.record_page != sub) out.push_back("records page: " + gd::str(o.record_page) + " != " + gd::str(sub));
        for (gd::Int i = 0; i < 4; ++i) {
            gd::Int top = trace.ram(frame, 0x800ECA20LL + 0x20 * i, 4);
            gd::Int count = trace.ram(frame, 0x800ECA24LL + 0x20 * i, 4);
            if (o.record_tops[i] != top || o.record_lists[i].size() != count)
                out.push_back("records list " + gd::str(i) + ": top " + gd::str(o.record_tops[i]) + " count " +
                              gd::str(o.record_lists[i].size()) + " != " + gd::str(top) + ", " + gd::str(count));
            for (gd::Int k = 0; k < gd::mini(count, 22); ++k) {
                gd::Int id = trace.ram(frame, 0x800ECA28LL + 0x20 * i + k, 1);
                if (k < o.record_lists[i].size() && o.record_lists[i][k] != id) {
                    out.push_back("records list " + gd::str(i) + " row " + gd::str(k) + ": " + gd::str(o.record_lists[i][k]) +
                                  " != " + gd::str(id));
                    break;
                }
            }
        }
    }
}

void practice(Lines& out, GameFlow& flow, const FlowTrace& trace, gd::Int frame) {
    const ByteBlock& s = *flow.practice->s;
    gd::PBytes theirs = trace.block(frame, PracticeMode::BASE, PracticeMode::SIZE);
    gd::Int shown = 0;
    for (size_t k2 = 0; k2 < std::size(PRACTICE_SPANS); k2 += 2)
        for (gd::Int i = PRACTICE_SPANS[k2]; i < PRACTICE_SPANS[k2 + 1]; ++i)
            if (s.bytes[i] != theirs[i] && shown < 8) {
                out.push_back("practice S+0x" + hex(i, 1) + ": " + hex(s.bytes[i], 2) + " != " + hex(theirs[i], 2));
                shown += 1;
            }
    for (gd::Int i = 0; i < 4; ++i) {
        gd::Int at = PracticeMode::MARKERS + 8 * i;
        for (gd::Int k = 0; k < 4; ++k)
            if (s.bytes[at + k] != theirs[at + k] && shown < 8) {
                out.push_back("practice marker " + gd::str(i) + " +" + gd::str(k) + ": " + hex(s.bytes[at + k], 2) + " != " + hex(theirs[at + k], 2));
                shown += 1;
            }
    }
    gd::Int paused = trace.ram(frame, 0x800958DCLL, 4);
    if (flow.fight->practice_paused != paused) out.push_back("practice paused: " + gd::str(flow.fight->practice_paused) + " != " + gd::str(paused));
    gd::Int intro = trace.ram(frame, 0x800958E0LL, 4);
    if (flow.fight->practice_intro != intro) out.push_back("practice intro: " + gd::str(flow.fight->practice_intro) + " != " + gd::str(intro));
}

// result.ovl's state and banner are never cleared by the game: on the frame a result state is
// entered the remake's blocks take the trace's bytes.
void uninitialised_results(GameFlow& flow, const FlowTrace& trace, gd::Int frame) {
    if (flow.sub != 0 || flow.state < GameFlow::State::TEAM_RESULT || flow.state > GameFlow::State::SURVIVAL_RESULT) return;
    for (gd::Int at = 0; at < ResultScreens::SIZE; at += 4) flow.results->state->put32(at, trace.ram(frame, ResultScreens::BASE + at, 4));
    for (gd::Int at = 0; at < 8; at += 4) flow.results->banner->put32(at, trace.ram(frame, ResultScreens::BANNER_BASE + at, 4));
}

// Tekken Ball's ball object lives in the fight heap, which holds older data: the remake's object
// takes the trace's bytes on the frame it is made.
void uninitialised_ball(Run& run, GameFlow& flow, const FlowTrace& trace, gd::Int frame) {
    gd::Ref<TekkenBall> ball = flow.fight->ball;
    if (!ball || ball == run.seeded_ball || !trace.has_pointed("ball")) return;
    run.seeded_ball = ball;
    for (gd::Int at = 0; at < TekkenBall::BALL_SIZE; ++at) ball->b->put8(at, trace.pointed_value(frame, "ball", at, 1));
}

void result(Lines& out, GameFlow& flow, const FlowTrace& trace, gd::Int frame) {
    // On the frame the state is entered the handler has not run.
    if (flow.sub == 0) return;
    const ResultScreens& r = *flow.results;
    for (gd::Int at : result_fields(flow.state)) {
        gd::Int expected = trace.ram(frame, ResultScreens::BASE + at, 4);
        if (r.state->u32(at) != expected) out.push_back("result +0x" + hex(at, 1) + ": " + gd::str(r.state->u32(at)) + " != " + gd::str(expected));
    }
    gd::Int seq = trace.ram(frame, 0x800A37A8LL, 1);
    if (r.seq_state != seq) out.push_back("voice sequence: " + gd::str(r.seq_state) + " != " + gd::str(seq));
    if (flow.state != GameFlow::State::TIME_RESULT) blocks(out, "banner", *r.banner, ResultScreens::BANNER_BASE, trace, frame);
}

void ranking(Lines& out, GameFlow& flow, const FlowTrace& trace, gd::Int frame) {
    const ByteBlock& table = *flow.ranking->table;
    for (const RankingField& f : RANKING_FIELDS) {
        if (flow.sub < f.after || (f.at == 0x86 && flow.sub > 0xD)) continue;
        gd::Int expected = trace.ram(frame, RankingScreen::TABLE_BASE + f.at, f.size);
        gd::Int actual = f.size == 2 ? table.u16(f.at) : table.u32(f.at);
        if (actual != expected) out.push_back(std::string("ranking ") + f.name + ": " + gd::str(actual) + " != " + gd::str(expected));
    }
    for (gd::Int i = 0; i < gd::mini(table.u16(0xC), 22); ++i) {
        gd::Int expected = trace.ram(frame, RankingScreen::TABLE_BASE + 0xE + 2 * i, 2);
        if (table.u16(0xE + 2 * i) != expected) out.push_back("ranking row " + gd::str(i) + ": " + gd::str(table.u16(0xE + 2 * i)) + " != " + gd::str(expected));
    }
    if (flow.sub == 0xC || flow.sub == 0xD) blocks(out, "name", *flow.ranking->name, RankingScreen::NAME_BASE, trace, frame);
    const ByteBlock& cam = *flow.ranking->backdrop;
    for (gd::Int at : {4, 0x14, 0x18, 0x1C}) {
        gd::Int expected = trace.ram(frame, RankingScreen::BACKDROP_BASE + at, 4);
        if (cam.u32(at) != expected) out.push_back("ranking camera +0x" + hex(at, 1) + ": " + gd::str(cam.u32(at)) + " != " + gd::str(expected));
    }
}

Lines unique(const Lines& calls) {
    Lines out;
    for (const std::string& c : calls) if (std::find(out.begin(), out.end(), c) == out.end()) out.push_back(c);
    return out;
}

Lines differences(Run& run, GameFlow& flow, const FlowTrace& trace, gd::Int frame) {
    Lines out;
    if (flow.state != trace.state(frame) || flow.sub != trace.sub_state(frame))
        out.push_back("state " + gd::str(flow.state) + " sub " + gd::str(flow.sub) + " != " + gd::str(trace.state(frame)) + " sub " +
                      gd::str(trace.sub_state(frame)));
    Lines ours = t3test::FightCalls::of_events(*flow.sim->events);
    Lines theirs = t3test::FightCalls::of_list(trace.calls(frame));
    if (flow.state == GameFlow::State::ENBU) {
        // Bug #49 (fixed): the game repeats fighter 0's frame events while both fighters move.
        ours = unique(ours);
        theirs = unique(theirs);
    }
    if (ours != theirs) out.push_back("calls: " + join(ours) + " != " + join(theirs));
    blocks(out, "progress", *flow.progress, GameProgress::BASE, trace, frame);
    blocks(out, "mode", *flow.region, ModeRegion::BASE, trace, frame);
    for (const Global& g : GLOBALS) {
        gd::Int expected = trace.ram(frame, g.address, g.size);
        gd::Int actual = RecordTrace::width(global_of(flow, g.name), g.size);
        if (actual != expected) out.push_back(std::string(g.name) + ": " + gd::str(actual) + " != " + gd::str(expected));
    }
    if (flow.state == GameFlow::State::QUICK_SELECT && flow.sub >= 2) {
        for (gd::Int at : QUICK_FIELDS) {
            if ((at == 0x70 || at == 0x11C) && flow.quick->mode() != 1) continue;   // the handicap is VS's only
            gd::Int expected = trace.ram(frame, QuickSelect::BASE + at, 4);
            if (flow.quick->ctx->u32(at) != expected)
                out.push_back("quick select +0x" + hex(at, 1) + ": " + gd::str(flow.quick->ctx->u32(at)) + " != " + gd::str(expected));
        }
    }
    if (flow.state == GameFlow::State::SELECT && trace.has_range(CharacterSelect::BASE)) {
        blocks(out, "select", *flow.select->ctx, CharacterSelect::BASE, trace, frame);
        blocks(out, "cells", *flow.select->cells, CharacterSelect::CELLS_BASE, trace, frame);
    }
    if (flow.state == GameFlow::State::OPTIONS && flow.sub >= 1 && trace.has_range(0x800EB2F0LL)) options(out, run, flow, trace, frame);
    if (flow.state == GameFlow::State::FIGHT && gd::Int(flow.region->mode) == 5 && flow.sub >= 8 && trace.has_range(PracticeMode::BASE))
        practice(out, flow, trace, frame);
    if (std::find(std::begin(RESULT_STATES), std::end(RESULT_STATES), flow.state) != std::end(RESULT_STATES) &&
        trace.has_range(ResultScreens::BASE))
        result(out, flow, trace, frame);
    if (flow.state == GameFlow::State::RANKING && flow.sub >= 1 && trace.has_range(RankingScreen::TABLE_BASE)) ranking(out, flow, trace, frame);
    if (flow.state == GameFlow::State::FIGHT && trace.has_range(0x8009892CLL)) camera(out, flow, trace, frame);
    if (trace.has_fighters(frame) && flow.sub >= 8) {
        for (gd::Int i = 0; i < 2; ++i) {
            const FighterState& f = *flow.fight->fighters[i];
            for (const Global& field : FIGHTER_FIELDS) {
                gd::Int expected = trace.fighter(frame, i, field.address, field.size);
                gd::Int actual = RecordTrace::width(fighter_of(f, field.name), field.size);
                if (actual != expected) out.push_back("fighter " + gd::str(i) + " " + field.name + ": " + gd::str(actual) + " != " + gd::str(expected));
            }
        }
    }
    return out;
}

void ball(Lines& out, const TekkenBall& b, const FlowTrace& trace, gd::Int frame) {
    gd::Int shown = 0;
    for (size_t k = 0; k < std::size(BALL_SPANS); k += 2)
        for (gd::Int at = BALL_SPANS[k]; at < BALL_SPANS[k + 1]; ++at) {
            gd::Int expected = trace.pointed_value(frame, "ball", at, 1);
            if (b.b->u8(at) != expected && shown < 8) {
                out.push_back("ball +0x" + hex(at, 1) + ": " + hex(b.b->u8(at), 2) + " != " + hex(expected, 2));
                shown += 1;
            }
        }
    for (size_t k = 0; k < std::size(VOLLEY_SPANS); k += 2)
        for (gd::Int a = VOLLEY_SPANS[k]; a < VOLLEY_SPANS[k + 1]; ++a) {
            gd::Int address = 0x800B0000LL + a;
            gd::Int expected = trace.ram(frame, address, 1);
            if (b.v->u8(address - TekkenBall::BASE) != expected && shown < 12) {
                out.push_back("volley 0x" + hex(address, 8) + ": " + hex(b.v->u8(address - TekkenBall::BASE), 2) + " != " + hex(expected, 2));
                shown += 1;
            }
        }
    for (gd::Int i = 0; i < 8; ++i)
        for (gd::Int at = 0x1C; at < 0x20; ++at) {
            gd::Int address = TekkenBall::POPUP_SLOTS + 0x20 * i + at;
            gd::Int expected = trace.ram(frame, address, 1);
            if (b.v->u8(address - TekkenBall::BASE) != expected && shown < 12) {
                out.push_back("popup " + gd::str(i) + " +0x" + hex(at, 1) + ": " + hex(b.v->u8(address - TekkenBall::BASE), 2) + " != " + hex(expected, 2));
                shown += 1;
            }
        }
}

// Tekken Ball and Tekken Force: all three records field by field (FighterCompare), the ball.
void all_fighters(Lines& out, GameFlow& flow, const t3test::FighterCompare& compare, const FlowTrace& trace, gd::Int frame) {
    const FightState& fight = *flow.fight;
    if (gd::Int(fight.mode) == 7 && flow.sim->ball()) ball(out, *flow.sim->ball(), trace, frame);
    gd::Arr<gd::Ref<MotionBank>> banks{fight.fighters[0]->bank, fight.fighters[1]->bank, flow.content->common};
    for (gd::Int i = 0; i < FightState::RECORDS; ++i)
        for (const std::string& line : compare.differences(*fight.fighters[i], frame, i, banks))
            if (out.size() < MAX_REPORTED) out.push_back("fighter " + gd::str(i) + " " + line);
}

// `watch(flow, trace, frame)` runs after each frame that matched the trace; the run ends when it returns true.
using Watch = std::function<bool(GameFlow&, const FlowTrace&, gd::Int)>;

void run_scenario(t3test::Context& t, const std::string& scenario, const Watch& watch = nullptr) {
    if (!t.require(FlowTrace::path(scenario), "run tools/research/flow_trace.py first")) return;
    auto trace = FlowTrace::load_scenario(scenario);
    if (!t.expect(trace != nullptr, scenario + " does not load")) return;
    auto flow = imported_flow(t, trace->header["scenario"]["seed"].to_int());
    if (!flow) return;
    use_enbu(*flow);
    apply_pokes(*flow, *trace);
    const gd::Json& refill_rule = trace->header.get("refill");
    if (!refill_rule.is_null() && refill_rule.size() != REFILL_RULE) {
        t.expect(false, scenario + ": an older refill rule; re-record it with tools/research/flow_trace.py");
        return;
    }
    const gd::Json& drain_rule = trace->header.get("drain");
    if (!drain_rule.is_null() && drain_rule.size() != DRAIN_RULE) {
        t.expect(false, scenario + ": an older drain rule; re-record it with tools/research/flow_trace.py");
        return;
    }
    t3test::FighterCompare compare(*trace);
    Run run;
    gd::Int fight_frames = 0;
    for (gd::Int frame = 0; frame < trace->frames; ++frame) {
        gd::PI32 pads = pads_of(*trace, frame);
        flow->step(pads);
        // The harness's movies end on their first frame.
        while (flow->movie_blocking) {
            flow->movie_result = 1;
            flow->step(pads);
        }
        if (flow->movie_started >= 0) flow->movie_result = 1;
        if (!refill_rule.is_null()) refill(*flow, refill_rule);
        if (!drain_rule.is_null()) drain(*flow, drain_rule);
        uninitialised_results(*flow, *trace, frame);
        uninitialised_ball(run, *flow, *trace, frame);
        Lines lines = differences(run, *flow, *trace, frame);
        if (trace->fighter_count(frame) == FightState::RECORDS && flow->sub >= 8) all_fighters(lines, *flow, compare, *trace, frame);
        if (trace->has_fighters(frame)) ++fight_frames;
        if (!lines.empty()) {
            std::string shown;
            for (size_t k = 0; k < lines.size() && k < MAX_REPORTED; ++k) shown += "\n    " + lines[k];
            t.expect(false, scenario + " frame " + gd::str(frame) + " (state " + gd::str(flow->state) + " sub " + gd::str(flow->sub) +
                                ", game " + gd::str(trace->state(frame)) + " sub " + gd::str(trace->sub_state(frame)) + "):" + shown);
            return;
        }
        if (watch && watch(*flow, *trace, frame)) return;
    }
    t.messages.push_back(gd::str(trace->frames) + " frames matched (" + gd::str(fight_frames) + " in fights)");
}

// The track playing after a step's music requests (-1: none).
gd::Int music_after(const SimEvents& events, gd::Int music) {
    for (const auto& e : events.items) {
        switch (e->kind) {
            case SimEvents::MUSIC:
            case SimEvents::MUSIC_PREPARE:
            case SimEvents::MUSIC_TRACK: music = e->a; break;
            case SimEvents::MUSIC_STOP: music = -1; break;
            default: break;
        }
    }
    return music;
}

}  // namespace

#define FLOW_TRACE(name) \
    T3_TEST(game_flow, name) { run_scenario(t, #name); }

FLOW_TRACE(vs_mode)
FLOW_TRACE(arcade)
FLOW_TRACE(time_attack)
FLOW_TRACE(survival)
FLOW_TRACE(team_battle)
FLOW_TRACE(practice)
FLOW_TRACE(options)
FLOW_TRACE(attract)
FLOW_TRACE(ball)
FLOW_TRACE(ball_versus)
FLOW_TRACE(force)
FLOW_TRACE(force_run)
FLOW_TRACE(force_final)
FLOW_TRACE(force_bosses)
FLOW_TRACE(ogre)

// The Ogre scene ends in a fade to white (FUN_8004E2E8 at level + 256 from frame 192, growing by 4
// a frame) held at full for the last three frames, before True Ogre's fight loads.
T3_TEST(game_flow, ogre_scene_fades_to_white) {
    if (!t.require(FlowTrace::path("ogre"), "run tools/research/flow_trace.py first")) return;
    auto trace = FlowTrace::load_scenario("ogre");
    auto flow = imported_flow(t, trace->header["scenario"]["seed"].to_int());
    if (!flow) return;
    use_enbu(*flow);
    apply_pokes(*flow, *trace);
    const gd::Json& refill_rule = trace->header.get("refill");
    const gd::Json& drain_rule = trace->header.get("drain");
    gd::PI32 levels;
    gd::Int frames_in_scene = 0;
    for (gd::Int frame = 0; frame < trace->frames; ++frame) {
        bool in_scene = flow->state == GameFlow::State::FIGHT && flow->sub == 17;
        step_flow(*flow, pads_of(*trace, frame));
        if (!refill_rule.is_null()) refill(*flow, refill_rule);
        if (!drain_rule.is_null()) drain(*flow, drain_rule);
        if (in_scene) {
            frames_in_scene += 1;
            levels.append(!flow->fades.is_empty() ? gd::Int(flow->fades[0]) : -1);
        } else if (frames_in_scene > 0) {
            break;
        }
    }
    if (!t.expect(frames_in_scene > 0, "the trace reaches the Ogre scene")) return;
    gd::Int first_fade = levels.find(0x100);
    t.expect(first_fade >= 0, "the fade starts at the neutral level 0x100");
    gd::PI32 white = levels.slice(levels.size() - 3);
    t.expect(white == gd::PI32{0x200, 0x200, 0x200}, "the scene ends in three frames of full white: " + t3test::show(white));
    bool rising = true;
    for (gd::Int i = first_fade + 1; i < levels.size(); ++i) rising = rising && levels[i] >= levels[i - 1];
    t.expect(rising, "the fade never goes back down");
    t.expect(first_fade > 0 && levels[first_fade - 1] == -1, "no fade is drawn before the script's fade event");
}

// The Ogre scene draws only the fighters it stepped: none before its moves start at script frame
// 10, then both, and both again for the fight that follows.
T3_TEST(game_flow, ogre_scene_draws_only_stepped_fighters) {
    if (!t.require(FlowTrace::path("ogre"), "run tools/research/flow_trace.py first")) return;
    auto trace = FlowTrace::load_scenario("ogre");
    auto flow = imported_flow(t, trace->header["scenario"]["seed"].to_int());
    if (!flow) return;
    use_enbu(*flow);
    apply_pokes(*flow, *trace);
    const gd::Json& drain_rule = trace->header.get("drain");
    gd::PI32 masks;
    gd::Int after = -1;
    for (gd::Int frame = 0; frame < trace->frames; ++frame) {
        bool in_scene = flow->state == GameFlow::State::FIGHT && flow->sub == 17;
        step_flow(*flow, pads_of(*trace, frame));
        if (!drain_rule.is_null()) drain(*flow, drain_rule);
        if (in_scene || flow->sub == 17) {
            masks.append(flow->fight->undrawn_mask);
        } else if (!masks.is_empty()) {
            after = flow->fight->undrawn_mask;
            break;
        }
    }
    if (!t.expect(!masks.is_empty(), "the trace reaches the Ogre scene")) return;
    t.expect_equal(gd::Int(masks[0]), gd::Int(3), "nothing is drawn when the scene starts");
    t.expect_equal(gd::Int(masks[masks.size() / 2]), gd::Int(0), "both fighters are drawn once their moves run");
    t.expect_equal(after, gd::Int(0), "the fight after the scene draws both fighters");
}

// Practice's COMMAND LIST from the FREE page of the practice scenario, then the remake's Escape:
// kept through the menu's inert frames, then as OK.
T3_TEST(game_flow, practice_command_list_and_escape) {
    if (!t.require(FlowTrace::path("practice"), "run tools/research/flow_trace.py first")) return;
    auto trace = FlowTrace::load_scenario("practice");
    auto flow = imported_flow(t, trace->header["scenario"]["seed"].to_int());
    if (!flow) return;
    for (gd::Int frame = 0; frame < PRACTICE_FREE_PAGE; ++frame) step_flow(*flow, pads_of(*trace, frame));
    const ByteBlock& s = *flow->practice->s;
    t.expect(s.u8(PracticeMode::PAUSED) != 0 && s.u8(PracticeMode::MENU_DELAY) == 0, "the FREE page takes input");
    gd::PI32 idle{0, 0};
    gd::Int player = s.u8(PracticeMode::PLAYER);
    for (gd::Int pad : {0x4000, 0, 0x40, 0}) step_flow(*flow, player == 0 ? gd::PI32{pad, 0} : gd::PI32{0, pad});
    t.expect_equal(s.u8(PracticeMode::COMMAND_LIST), gd::Int(1), "COMMAND LIST turned on");
    gd::Ref<SimEvents::Event> shown;
    for (gd::Int i = 0; i < 5; ++i) {
        step_flow(*flow, idle);
        shown = flow->sim->events->first(SimEvents::MOVE_LIST);
    }
    t.expect(shown != nullptr, "the list is shown");
    if (shown) t.expect_equal(shown->a, gd::Int(1) << player, "the player's list (the dummy is not on CONTROLLER)");
    step_flow(*flow, player == 0 ? gd::PI32{PadState::START, 0} : gd::PI32{0, PadState::START});
    t.expect_equal(s.u8(PracticeMode::COMMAND_LIST), gd::Int(0), "Start turns it off");
    t.expect(s.u8(PracticeMode::MENU_DELAY) != 0, "the menu is inert again");
    flow->escape_pressed = true;
    step_flow(*flow, idle);
    t.expect(flow->escape_pressed, "Escape waits for the menu");
    for (gd::Int i = 0; i < 6; ++i) step_flow(*flow, idle);
    t.expect(!flow->escape_pressed, "Escape taken");
    t.expect_equal(s.u8(PracticeMode::PAUSED), gd::Int(0), "Escape leaves the menu as OK");
}

// Start + Select on the arcade's character select back to the main menu: loading title.ovl stops
// the select music (a disc read pauses the XA stream, FUN_8006B834).
T3_TEST(game_flow, menu_exit_stops_music) {
    if (!t.require(FlowTrace::path("arcade"), "run tools/research/flow_trace.py first")) return;
    auto trace = FlowTrace::load_scenario("arcade");
    auto flow = imported_flow(t, trace->header["scenario"]["seed"].to_int());
    if (!flow) return;
    gd::Int music = -1;
    gd::Int frame = 0;
    while (flow->state != GameFlow::State::SELECT && frame < trace->frames) {
        step_flow(*flow, pads_of(*trace, frame));
        music = music_after(*flow->sim->events, music);
        frame += 1;
    }
    gd::PI32 idle{0, 0};
    for (gd::Int i = 0; i < 60; ++i) {
        step_flow(*flow, idle);
        music = music_after(*flow->sim->events, music);
    }
    t.expect_equal(flow->state, gd::Int(GameFlow::State::SELECT), "on the character select");
    t.expect_equal(music, CharacterSelect::MUSIC_SELECT, "the select music plays");
    for (const gd::PI32& pads : {gd::PI32{PadState::START, 0}, gd::PI32{0x900, 0}}) {
        step_flow(*flow, pads);
        music = music_after(*flow->sim->events, music);
    }
    for (gd::Int i = 0; i < 120; ++i) {
        if (flow->state == GameFlow::State::MENU) break;
        step_flow(*flow, idle);
        music = music_after(*flow->sim->events, music);
    }
    t.expect_equal(flow->state, gd::Int(GameFlow::State::MENU), "back on the main menu");
    t.expect_equal(music, gd::Int(-1), "the music stopped");
}

// Arcade: the VS screen after the character select loads the fighters' pictures from the disc,
// which stops the select music (FUN_8006B834).
T3_TEST(game_flow, vs_pictures_stop_music) {
    if (!t.require(FlowTrace::path("arcade"), "run tools/research/flow_trace.py first")) return;
    auto trace = FlowTrace::load_scenario("arcade");
    auto flow = imported_flow(t, trace->header["scenario"]["seed"].to_int());
    if (!flow) return;
    gd::Int music = -1;
    bool selected = false;
    for (gd::Int frame = 0; frame < trace->frames; ++frame) {
        step_flow(*flow, pads_of(*trace, frame));
        music = music_after(*flow->sim->events, music);
        selected = selected || (flow->state == GameFlow::State::SELECT && music == CharacterSelect::MUSIC_SELECT);
        if (selected && flow->state == GameFlow::State::VS && flow->sub >= 2) break;
    }
    t.expect(selected, "the select music played");
    t.expect_equal(flow->state, gd::Int(GameFlow::State::VS), "on the VS screen");
    t.expect_equal(music, gd::Int(-1), "the VS screen's loads stopped the music");
}

// FUN_80036854: a fight reads its stage from the disc, which stops the music, only when another
// stage is in memory; a mode start empties the cache; Tekken Ball reads its stage every time.
T3_TEST(game_flow, stage_read_stops_music) {
    auto flow = imported_flow(t);
    if (!flow) return;
    FightState& fight = *flow->fight;
    gd::PI32 chars{0, 1};
    gd::PI32 costumes{0, 0};
    gd::PI32 cpu{0, 1};
    fight.mode = 1;
    fight.stage = 3;
    struct Case {
        gd::Int cached;
        bool stops;
        const char* what;
    };
    for (const Case& c : {Case{-1, true, "nothing in memory"}, Case{3, false, "the same stage"}, Case{5, true, "another stage"}}) {
        fight.stage_cached = c.cached;
        flow->sim->events->clear();
        flow->sim->load_fighters(chars, costumes, cpu);
        t.expect(flow->sim->events->has(SimEvents::MUSIC_STOP) == c.stops, c.what);
        t.expect_equal(fight.stage_cached, gd::Int(3), std::string(c.what) + ": the stage is noted");
    }
    ModeStart::start(flow.get(), 1, 0);
    t.expect_equal(fight.stage_cached, gd::Int(-1), "a mode start empties the cache");
    fight.mode = 7;
    fight.stage_cached = 3;
    flow->sim->events->clear();
    flow->sim->load_fighters(chars, costumes, cpu);
    t.expect(flow->sim->events->has(SimEvents::MUSIC_STOP), "Tekken Ball reads its stage every time");
}
