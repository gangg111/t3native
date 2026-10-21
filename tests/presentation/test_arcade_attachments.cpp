// remake/tests/presentation/test_arcade_attachments.gd: ArcadeAttachments against the arcade's
// FUN_8019b6d8 run in the CPU harness on random fighter states (`work/traces/arcade_attachments.bin`,
// tools/research/arcade_attachment_cases.py).
#include <cmath>
#include <cstdio>

#include "content/character_model.h"
#include "core/fighter/joint_frame.h"
#include "core/fighter/pose_solver.h"
#include "core/math/fx.h"
#include "gd/io.h"
#include "presentation/fighter/arcade_attachments.h"
#include "presentation/fighter/arcade_wind.h"
#include "presentation/fighter/blinker.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

const std::string FILE_NAME = "arcade_attachments.bin";   // FILE
constexpr gd::Int RECORD = 16;
constexpr gd::Int ROWS = 24;

// Traces.s16s
gd::PI32 s16s(const gd::PBytes& data, gd::Int pos, gd::Int count) {
    gd::PI32 out;
    for (gd::Int k = 0; k < count; ++k) out.append(data.decode_s16(pos + 2 * k));
    return out;
}

gd::PI32 _s32s(const gd::PBytes& data, gd::Int pos, gd::Int count) {
    gd::PI32 out;
    for (gd::Int k = 0; k < count; ++k) out.append(data.decode_s32(pos + 4 * k));
    return out;
}

gd::Ref<JointFrame> _frame(const gd::PBytes& data, gd::Int pos) {
    return gd::make<JointFrame>(s16s(data, pos, 9), _s32s(data, pos + 18, 3));
}

std::string hex(gd::Int v) {
    char b[24];
    std::snprintf(b, sizeof b, "%llx", static_cast<long long>(v));
    return b;
}

}  // namespace

T3_TEST(arcade_attachments, matches_the_arcade) {
    if (!t.require(t3test::trace(FILE_NAME), "run tools/research/arcade_attachment_cases.py first")) return;
    if (!t.require(t3test::imported(ArcadeAttachments::TABLES_FILE), "convert with --arcade first")) return;
    ArcadeAttachments::assets_root = t3test::paths().imported;
    gd::PBytes data = gd::read_file(t3test::trace(FILE_NAME));
    gd::Int count = data.decode_u32(8);
    gd::Int pos = 12;
    gd::Int failed = 0;
    for (gd::Int n = 0; n < count; ++n) {
        gd::Int slot = data.decode_u8(pos);
        gd::Int i = data.decode_u8(pos + 1);
        bool started = data.decode_u8(pos + 2) != 0;
        gd::Int parent = data.decode_u8(pos + 3);
        pos += 4;
        gd::PI32 angles = s16s(data, pos, 3);
        pos += 6;
        gd::PI32 record = s16s(data, pos, RECORD);
        pos += 2 * RECORD;
        gd::PI32 wind = s16s(data, pos, 3);
        pos += 6;
        gd::PI32 tip = _s32s(data, pos, 3);
        gd::PI32 target = _s32s(data, pos + 12, 2);
        gd::PI32 offset = _s32s(data, pos + 20, 3);
        pos += 32;
        gd::Arr<gd::Ref<JointFrame>> joints;
        joints.resize(CharacterModel::JOINT_COUNT);
        for (gd::Int row = 0; row < ROWS; ++row) {
            gd::PI32 rot = s16s(data, pos, 9);
            joints[ArcadeAttachments::joint_of_row(row)] = gd::make<JointFrame>(rot, _s32s(data, pos + 18, 3));
            pos += 30;
        }
        gd::PI32 want = s16s(data, pos, 9);
        gd::PI32 want_tip = _s32s(data, pos + 18, 3);
        gd::PI32 want_target = _s32s(data, pos + 30, 2);
        pos += 38;
        auto model = gd::make<CharacterModel>();
        model->costume_slot = slot;
        ArcadeAttachments a(model);
        a.wind = wind;
        a._tips[i] = tip;
        a._target_yaw = target[0];
        a._target_pitch = target[1];
        gd::PI32 got;
        bool rest_only = ArcadeAttachments::REST_ONLY_SLOTS.has(slot) && i < ArcadeAttachments::REST_ONLY_COUNT;
        if (rest_only) {
            got = Fx::identity();
        } else if (!started) {
            got = ArcadeAttachments::solver()->euler_to_matrix(angles[0], angles[1], angles[2]);
        } else {
            gd::Ref<JointFrame> own = joints[CharacterModel::ARCADE_JOINT + i];
            got = a.swing(slot, i, angles, record, own, joints[ArcadeAttachments::joint_of_row(parent)], offset, joints);
        }
        bool ok = got == want;
        if (started && !rest_only) {
            ok = ok && a._tips[i] == want_tip && gd::PI32{a._target_yaw, a._target_pitch} == want_target;
        }
        if (!ok) {
            failed += 1;
            if (failed <= 5) {
                t.expect(false, "case " + gd::str(n) + " (slot 0x" + hex(slot) + ", attachment " + gd::str(i) + ", mode " +
                                    gd::str(record[ArcadeAttachments::MODE]) + "): expected " + t3test::show(want) + " tip " +
                                    t3test::show(want_tip) + " target " + t3test::show(want_target) + ", got " +
                                    t3test::show(got) + " tip " + t3test::show(a._tips[i]) + " target " +
                                    t3test::show(gd::PI32{a._target_yaw, a._target_pitch}));
            }
        }
    }
    t.expect_equal(failed, gd::Int(0), "cases that differ of " + gd::str(count));
}

// FUN_801e6acc / FUN_8019c738: still air off the helicopter stage; there, one of eight
// directions gusting with a period of 16 steps, normalised to about 4096.
T3_TEST(arcade_attachments, wind_blows_on_the_helicopter_stage_only) {
    if (!t.require(t3test::imported("tables/fighter.json"))) return;
    ArcadeAttachments::assets_root = t3test::paths().imported;
    RandomNumberGenerator rng;
    rng.set_seed(3);
    ArcadeWind wind;
    wind.start(0, rng);
    wind.step();
    t.expect(wind.vector == gd::PI32{0, 0, 0}, "still air on stage 0");
    wind.start(ArcadeWind::STAGE, rng);
    gd::Arr<gd::PI32> gusts;
    for (gd::Int i = 0; i < 32; ++i) {
        wind.step();
        gusts.append(wind.vector);
    }
    gd::PI32 v = gusts[0];
    double length = std::sqrt(static_cast<double>(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]));
    t.expect(std::fabs(length - 4096.0) < 64.0, "normalised: " + std::to_string(length));
    t.expect(v[0] != 0 || v[2] != 0, "blowing sideways");
    t.expect(gusts[16] == gusts[0], "a period of 16 steps");
}

// ArcadeAttachments.update on the converted models against whole runs of the arcade's loop
// (FUN_8019b6d8 then CompMatrix for every drawn attachment in row order, FUN_80198754) on a
// skeleton turning step by step (`work/traces/arcade_attachment_steps.bin`).
T3_TEST(arcade_attachments, runs_match_the_arcade) {
    const std::string RUNS = "arcade_attachment_steps.bin";
    if (!t.require(t3test::trace(RUNS), "run tools/research/arcade_attachment_cases.py first")) return;
    if (!t.require(t3test::imported(ArcadeAttachments::TABLES_FILE), "convert with --arcade first")) return;
    ArcadeAttachments::assets_root = t3test::paths().imported;
    gd::PBytes data = gd::read_file(t3test::trace(RUNS));
    gd::Int count = data.decode_u32(8);
    gd::Int steps = data.decode_u32(12);
    gd::Int pos = 16;
    gd::Int failed = 0;
    gd::Int compared = 0;
    for (gd::Int n = 0; n < count; ++n) {
        gd::Int slot = data.decode_u8(pos);
        gd::PI32 wind = s16s(data, pos + 2, 3);
        pos += 8;
        std::string name = "characters/costume_" + std::string(slot < 10 ? "0" : "") + gd::str(slot);
        gd::Ref<CharacterModel> model = CharacterModel::load_from(t3test::imported(name));
        ArcadeAttachments a(model);
        a.wind = wind;
        for (gd::Int step = 0; step < steps; ++step) {
            gd::Arr<gd::Ref<JointFrame>> body;
            for (gd::Int k = 0; k < CharacterModel::PART_COUNT; ++k) {
                body.append(k < ArcadeAttachments::FIRST_ATTACHMENT_ROW ? _frame(data, pos) : gd::make<JointFrame>());
                if (k < ArcadeAttachments::FIRST_ATTACHMENT_ROW) pos += 30;
            }
            gd::Arr<gd::Ref<JointFrame>> all = a.update(body);
            for (gd::Int i = 0; i < CharacterModel::JOINT_COUNT - CharacterModel::ARCADE_JOINT; ++i) {
                gd::Ref<JointFrame> want = _frame(data, pos);
                pos += 30;
                gd::Int joint = CharacterModel::ARCADE_JOINT + i;
                bool any = false;
                for (const auto& at : model->arcade_attachments) {
                    if (at->joint == joint) any = true;
                }
                if (!any) continue;
                compared += 1;
                if (all[joint]->rot != want->rot || all[joint]->t != want->t) {
                    failed += 1;
                    if (failed <= 5) {
                        t.expect(false, "run " + gd::str(n) + " (slot " + gd::str(slot) + ") step " + gd::str(step) + " joint " +
                                            gd::str(joint) + ": expected " + t3test::show(want->rot) + " " +
                                            t3test::show(want->t) + ", got " + t3test::show(all[joint]->rot) + " " +
                                            t3test::show(all[joint]->t));
                    }
                }
            }
        }
    }
    t.expect(compared > 0, gd::str(compared) + " attachment joints compared");
    t.expect_equal(failed, gd::Int(0), "attachment joints that differ in " + gd::str(count) + " runs of " + gd::str(steps) + " steps");
}
