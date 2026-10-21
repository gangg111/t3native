// The flow trace reader alone (no game flow): every scenario inflates, indexes inside its body,
// and reports its frame count, ranges and fighter frames.
#include "support/record_trace.h"
#include "support/test_suite.h"

namespace {
void check(t3test::Context& t, const std::string& scenario) {
    if (!t.require(t3test::FlowTrace::path(scenario), "run tools/research/flow_trace.py first")) return;
    auto trace = t3test::FlowTrace::load_scenario(scenario);
    if (!t.expect(trace != nullptr, scenario + " does not load")) return;
    gd::Int fight_frames = 0;
    std::string firsts;   // the first frame of each game state (where to take comparison screenshots)
    gd::Int last_state = -1;
    for (gd::Int f = 0; f < trace->frames; ++f) {
        if (trace->has_fighters(f)) ++fight_frames;
        gd::Int s = trace->state(f);
        if (s != last_state) {
            firsts += " " + gd::str(s) + "@" + gd::str(f);
            last_state = s;
        }
    }
    t.messages.push_back("states:" + firsts.substr(0, 400));
    gd::Int last = trace->frames - 1;
    gd::Int end = trace->has_fighters(last) ? trace->record(last, trace->fighter_count(last)) : 0;
    t.expect(end <= trace->body.size(), "last fighter record outside the body");
    t.messages.push_back(gd::str(trace->frames) + " frames, " + gd::str(fight_frames) + " with fighters, ranges " +
                         gd::str(trace->ranges.size()) + ", pokes " + gd::str(trace->pokes().size()) + ", state at the end " +
                         gd::str(trace->state(last)) + "/" + gd::str(trace->sub_state(last)));
}
}  // namespace

T3_TEST(flow_trace_reader, vs_mode) { check(t, "vs_mode"); }
T3_TEST(flow_trace_reader, ball) { check(t, "ball"); }
