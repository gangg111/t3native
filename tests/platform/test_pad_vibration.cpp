// remake/tests/platform/test_pad_vibration.gd: PadVibration against the game's own VibrationUpdate
// (`work/traces/vibration.bin`, tools/research/vibration_trace.py): every pattern and some pairs,
// frame by frame.
#include "content/fight_tables.h"
#include "dev/record_trace.h"
#include "gd/io.h"
#include "platform/pad_vibration.h"
#include "support/test_suite.h"

using namespace t3;

T3_TEST(pad_vibration, scripts_match_the_game) {
    std::string trace = TraceFiles::path("vibration.bin");
    std::string tables_dir = t3test::imported("tables");
    if (!t.require(trace, "run tools/research/export_traces.py first")) return;
    if (!t.require(gd::path_join(tables_dir, "fight.json"))) return;
    gd::Ref<FightTables> tables = FightTables::load_from(tables_dir);
    gd::PBytes data = gd::read_file(trace);
    t.expect_equal(data.slice(0, 4).get_string_from_ascii(), std::string("T3VB"), "trace magic");
    gd::Int cases = data.decode_u32(8);
    gd::Int pos = 12;
    for (gd::Int c = 0; c < cases; ++c) {
        gd::Int count = data.decode_u8(pos);
        gd::PBytes patterns = data.slice(pos + 1, pos + 1 + count);
        pos += 1 + count;
        gd::Int frames = data.decode_u16(pos);
        pos += 2;
        PadVibration pad(tables->vibration);
        for (gd::Int p : patterns) {
            pad.vibrate(0, p);
        }
        for (gd::Int frame = 0; frame < frames; ++frame) {
            pad.update();
            gd::Int small = data.decode_u8(pos);
            gd::Int large = data.decode_u8(pos + 1);
            pos += 2;
            if ((pad.small_on[0] ? 1 : 0) != small || pad.large_level[0] != large) {
                t.expect(false, "case " + gd::str(c) + " frame " + gd::str(frame) + ": small " +
                                    gd::str(gd::Int(pad.small_on[0])) + " large " + gd::str(gd::Int(pad.large_level[0])) +
                                    " != " + gd::str(small) + " " + gd::str(large));
                return;
            }
        }
    }
}
