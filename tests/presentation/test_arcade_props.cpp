// remake/tests/presentation/test_arcade_props.gd: ArcadeProps against the arcade's own prop
// routines: tools/research/verify_arcade_props.py ran stage 3's carousel and stage 11's helicopter
// (every mode, two rounds) in the CPU harness under random camera turns and recorded every frame's
// state (work/traces/arcade_props.json and .bin).
#include <cstdio>

#include "content/json_file.h"
#include "gd/io.h"
#include "gd/json.h"
#include "platform/pad_state.h"
#include "presentation/stage/arcade_props.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

const std::string TRACE = "arcade_props.json";
const std::string FRAMES = "arcade_props.bin";

std::string hex(gd::Int v) {
    char b[24];
    std::snprintf(b, sizeof b, "%llx", static_cast<long long>(v));
    return b;
}

}  // namespace

T3_TEST(arcade_props, props_follow_the_arcade_frame_for_frame) {
    if (!t.require(t3test::trace(TRACE), "run tools/research/verify_arcade_props.py --export first")) return;
    gd::Json trace = JsonFile::read(t3test::trace(TRACE));
    gd::Int words = JsonFile::number(trace.get("frame_words"));
    gd::PBytes frames = gd::read_file(t3test::trace(FRAMES));
    gd::PI32 states;
    // The .bin holds s16 words: unpack them from the 32-bit view.
    gd::Int count = frames.size() / 4;
    for (gd::Int i = 0; i < count; ++i) {
        gd::Int w = frames.decode_s32(4 * i);
        states.append(ArcadeProps::s16(w & 0xFFFF));
        states.append(ArcadeProps::s16((w >> 16) & 0xFFFF));
    }
    gd::PI32 sine = JsonFile::ints(trace.get("sine"));
    gd::Int at = 0;
    for (const gd::Json& case_data : trace.get("cases").items()) {
        gd::Int stage = JsonFile::number(case_data.get("stage"));
        ArcadeProps props(stage == 3 ? ArcadeProps::CAROUSEL : ArcadeProps::HELICOPTER, sine);
        gd::Int first_difference = -1;
        gd::Int frame = 0;
        for (const gd::Json& round_data : case_data.get("rounds").items()) {
            props.start(JsonFile::number(round_data.get("buttons")), JsonFile::number(round_data.get("counter")));
            for (gd::Int yaw : JsonFile::ints(round_data.get("yaws"))) {
                props.step(yaw);
                if (first_difference < 0 && props.words() != states.slice(at, at + words)) {
                    first_difference = frame;
                }
                at += words;
                frame += 1;
            }
        }
        t.expect(first_difference < 0, case_data.get("name").str() + ": equal on all " + gd::str(frame) +
                                           " frames (first difference at " + gd::str(first_difference) + ")");
    }
    t.expect_equal(at, states.size(), "every recorded frame compared");
}

// FUN_801E3DE0's mode buttons are the attack buttons in pose order: LP □ 0, RP △ 1, LK ✕ 2, RK ○ 3;
// none: the counter's low bits.
T3_TEST(arcade_props, helicopter_mode_from_held_buttons) {
    gd::Dict<gd::Int, gd::Int> expected;
    expected.set(PadState::SQUARE, 0);
    expected.set(PadState::TRIANGLE, 1);
    expected.set(PadState::CROSS, 2);
    expected.set(PadState::CIRCLE, 3);
    for (const auto& kv : expected) {
        gd::Int button = kv.first;
        ArcadeProps props(ArcadeProps::HELICOPTER);
        props.start(ArcadeProps::arcade_buttons(button), 1);
        t.expect_equal(props.mode, expected.at(button), "button 0x" + hex(button));
    }
    ArcadeProps plain(ArcadeProps::HELICOPTER);
    plain.start(ArcadeProps::arcade_buttons(PadState::START), 6);
    t.expect_equal(plain.mode, gd::Int(2), "no attack button: the counter's low bits");
}
