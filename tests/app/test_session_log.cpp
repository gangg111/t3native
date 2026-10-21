// Native: save states by re-simulation (app/session_log.h): a flow stepped through another flow's
// logged inputs reaches the same state; the log survives its file form.
#include <vector>

#include "app/session_log.h"
#include "content/enbu_assets.h"
#include "content/fight_content.h"
#include "content/flow_data.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/rule_set.h"
#include "core/game/game_flow.h"
#include "core/game/game_progress.h"
#include "dev/record_trace.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

struct Shared {
    gd::Ref<FightContent> content;
    gd::Ref<FlowData> data;
    gd::Ref<EnbuAssets> enbu;
};

// A flow on the converted assets with the attract demonstrations (as the app makes it).
gd::Ref<GameFlow> new_flow(gd::Int seed) {
    static Shared s;
    if (!s.content) {
        s.content = FightContent::load_from(t3test::paths().imported);
        s.data = FlowData::load_from(t3test::imported("tables/flow.json"));
        s.enbu = EnbuAssets::load_from(t3test::paths().imported);
    }
    gd::Ref<GameFlow> flow = gd::make<GameFlow>(s.content, s.data, RuleSet::original(), seed);
    gd::Ref<EnbuAssets> enbu = s.enbu;
    flow->performance_factory = [enbu](gd::Int number) { return enbu->performance(number); };
    return flow;
}

// One flow step as the app logs it.
void logged_step(GameFlow& flow, SessionLog& log, const gd::PI32& pads) {
    flow.fight->state_items = true;
    log.steps.push_back(SessionLog::capture(flow, pads));
    flow.step(pads);
}

// The trace's frames as the harness plays them (its movies end on their first frame), logged.
void play(GameFlow& flow, SessionLog& log, const FlowTrace& trace, gd::Int frames) {
    for (gd::Int frame = 0; frame < frames; ++frame) {
        gd::PI32 pads{trace.pad(frame, 0), trace.pad(frame, 1)};
        logged_step(flow, log, pads);
        while (flow.movie_blocking) {
            flow.movie_result = 1;
            logged_step(flow, log, pads);
        }
        if (flow.movie_started >= 0) flow.movie_result = 1;
    }
}

}  // namespace

T3_TEST(session_log, file_form) {
    SessionLog log;
    log.seed = 0x1234567890LL;
    log.movies = true;
    log.fixes = true;
    log.progress = {1, 2, 3};
    log.check = 0xFEDCBA9876543210ULL;
    log.steps.push_back(SessionLog::Step{0x4000, 0x40, SessionLog::ESCAPE | SessionLog::ONE_PAD, -1});
    log.events.push_back(SessionLog::Event{7, SessionLog::CARD_READ, true, -5, {9, 8}});
    SessionLog back;
    // source: by hand: every field written comes back
    t.expect(back.parse(log.serialize()), "parsed");
    t.expect(back.seed == log.seed && back.movies && back.fixes && back.check == log.check, "the header");
    t.expect(back.progress == log.progress, "the progress");
    t.expect(back.steps.size() == 1 && back.steps[0].pad0 == 0x4000 && back.steps[0].pad1 == 0x40 &&
                 back.steps[0].flags == (SessionLog::ESCAPE | SessionLog::ONE_PAD) && back.steps[0].movie_result == -1,
             "the step");
    t.expect(back.events.size() == 1 && back.events[0].step == 7 && back.events[0].kind == SessionLog::CARD_READ &&
                 back.events[0].inside && back.events[0].value == -5 && back.events[0].bytes == std::vector<uint8_t>{9, 8},
             "the event");
    std::vector<uint8_t> cut = log.serialize();
    cut.pop_back();
    t.expect(!back.parse(cut), "a cut file is refused");
    t.expect(back.seed == log.seed, "and leaves the log as it was");
}

// The VS MODE scenario's 3000 frames (into the fight), logged on one flow and replayed on a new one.
T3_TEST(session_log, replay_reaches_the_same_state) {
    if (!t.require(t3test::imported("tables/flow.json"))) return;
    if (!t.require(FlowTrace::path("vs_mode"), "run tools/research/flow_trace.py first")) return;
    auto trace = FlowTrace::load_scenario("vs_mode");
    gd::Int seed = trace->header["scenario"]["seed"].to_int();
    gd::Ref<GameFlow> live = new_flow(seed);
    for (const gd::PI64& poke : trace->pokes()) live->progress->put8(poke[0] - GameProgress::BASE, poke[1]);
    SessionLog log;
    log.seed = seed;
    log.progress = SessionLog::bytes_of(live->progress->bytes);
    play(*live, log, *trace, 3000);
    log.check = SessionLog::checksum(*live);
    t.expect(live->state == GameFlow::State::FIGHT, "the logged session ends in a fight");

    gd::Ref<GameFlow> again = new_flow(log.seed);
    SessionLog::put_bytes(again->progress->bytes, log.progress);
    for (size_t i = 0; i + 1 < log.steps.size(); ++i) {
        SessionLog::prepare(*again, log.steps[i]);
        again->step(SessionLog::pads(log.steps[i]));
    }
    // source: the checksum must tell a state one step short from the saved one
    t.expect(SessionLog::checksum(*again) != log.check, "one step short: another checksum");
    SessionLog::prepare(*again, log.steps.back());
    again->step(SessionLog::pads(log.steps.back()));
    t.expect_equal(SessionLog::checksum(*again), log.check, "the replay's checksum is the saved one");
    t.expect_equal(again->state, live->state, "the same flow state");
}
