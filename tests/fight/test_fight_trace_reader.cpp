// The fight trace reader alone (no simulation): the body inflates with its Adler-32, every frame
// indexes inside the body, and the frame count and field table match the header.
#include "support/record_trace.h"
#include "support/test_suite.h"

T3_TEST(fight_trace_reader, paul_law_indexes) {
    if (!t.require(t3test::FightTrace::path("paul_law"), "run tools/research/export_traces.py --fight first")) return;
    auto trace = t3test::FightTrace::load_scenario("paul_law");
    if (!t.expect(trace != nullptr, "paul_law does not load")) return;
    t.expect(trace->frames > 0, "no frames");
    t.expect(trace->fighter_size > 0, "no fighter size");
    t.expect(!trace->field_types.empty(), "fighter_fields.tsv not read");
    gd::Int last = trace->frames - 1;
    t.expect(trace->record(last, 1) + trace->fighter_size <= trace->body.size(), "last record outside the body");
    t.messages.push_back("frames " + gd::str(trace->frames) + ", fighter_size " + gd::str(trace->fighter_size) +
                         ", fields " + gd::str(static_cast<gd::Int>(trace->field_types.size())) + ", body " +
                         gd::str(trace->body.size()) + " B, ranges " + gd::str(trace->ranges.size()) +
                         ", pads at frame 100: " + gd::str(trace->pad(100, 0)) + " " + gd::str(trace->pad(100, 1)));
}
