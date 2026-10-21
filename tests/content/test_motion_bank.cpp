// remake/tests/content/test_motion_bank.gd: the converted motion banks: baked pose vectors equal
// the verified sampler's output in the traces for every frame.
// TODO: test_stance_slot_resolves arrives with MotionSet (core/fighter/motion_set.gd).
#include "content/motion_bank.h"
#include "support/test_suite.h"
#include "support/traces.h"

using namespace t3;

namespace {
const std::string BANK = "divmot00";
const std::string COMMON = "divmot99";

gd::Ref<MotionBank> bank_named(const std::string& name) { return MotionBank::load_named(t3test::imported("motion"), name); }

bool inputs_present(t3test::Context& t) {
    return t.require(t3test::imported("motion/" + BANK + ".json.gz")) &&
           t.require(t3test::trace("pose_" + BANK + ".bin"), "run tools/research/export_traces.py first");
}
}  // namespace

T3_TEST(motion_bank, baked_poses_match_traces) {
    if (!inputs_present(t)) return;
    for (const std::string& name : {BANK, COMMON}) {
        auto bank = bank_named(name);
        auto anims = t3test::read_pose(t3test::trace("pose_" + name + ".bin"));
        // The common bank also bakes the streams the other banks' rows use.
        t.expect(bank->anims.size() >= anims.size(), name + " has every traced animation");
        gd::Int mismatches = 0;
        for (const t3test::PoseAnim& anim : anims) {
            if (!t.expect(bank->has_anim(anim.offset), name + ": missing stream " + std::to_string(anim.offset))) continue;
            t.expect_equal(bank->frame_count(anim.offset), anim.poses.size(), name + " stream " + std::to_string(anim.offset) + " frames");
            for (gd::Int f = 0; f < anim.poses.size(); ++f)
                if (bank->pose(anim.offset, f) != anim.poses[f]) mismatches += 1;
        }
        t.expect_equal(mismatches, gd::Int(0), name + " pose mismatches");
    }
}

T3_TEST(motion_bank, frames_clamp_like_the_game) {
    if (!inputs_present(t)) return;
    auto bank = bank_named(BANK);
    gd::Int offset = bank->anims.keys()[0];
    gd::Int last = bank->frame_count(offset) - 1;
    t.expect(bank->pose(offset, -5) == bank->pose(offset, 0), "frame -5 clamps to 0");
    t.expect(bank->pose(offset, last + 10) == bank->pose(offset, last), "frame past the end clamps to the last");
}
