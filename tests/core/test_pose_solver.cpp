// remake/tests/core/test_pose_solver.gd: PoseSolver against the golden pose traces of the verified
// Python port (itself bit-exact with the game's PoseBuildMatrices in the CPU harness). Every frame
// of every animation of the banks is replayed with slots carried over, as while one move plays.
#include "core/fighter/pose_solver.h"
#include "support/test_suite.h"
#include "support/traces.h"

using namespace t3;

T3_TEST(pose_solver, matches_traces) {
    std::string tables = t3test::imported("tables/pose.json");
    if (!t.require(tables) || !t.require(t3test::trace("pose_divmot00.bin"), "run tools/research/export_traces.py first"))
        return;
    PoseSolver solver(PoseTables::load_from(tables));
    for (std::string bank : {"divmot00", "divmot99"}) {
        gd::Int frames = 0;
        gd::Int mismatches = 0;
        std::string first_mismatch;
        for (const t3test::PoseAnim& anim : t3test::read_pose(t3test::trace("pose_" + bank + ".bin"))) {
            gd::Arr<gd::PI32> slots;
            for (gd::Int i = 0; i < PoseSolver::SLOT_COUNT; ++i) slots.append(Fx::identity());
            for (gd::Int f = 0; f < anim.poses.size(); ++f) {
                solver.build(anim.poses[f], slots);
                frames += 1;
                const gd::Arr<gd::PI32>& expected = anim.slots[f];
                for (gd::Int s = 0; s < PoseSolver::SLOT_COUNT; ++s) {
                    if (slots[s] != expected[s]) {
                        mismatches += 1;
                        if (first_mismatch.empty())
                            first_mismatch = bank + " anim " + std::to_string(anim.offset) + " frame " + std::to_string(f) +
                                             " slot " + std::to_string(s) + ": " + t3test::show(slots[s]) +
                                             " != " + t3test::show(expected[s]);
                    }
                }
            }
        }
        t.expect(frames > 0, bank + ": no frames");
        t.expect(mismatches == 0, bank + ": " + std::to_string(frames) + " frames, " + std::to_string(mismatches) +
                                      " mismatches, first " + first_mismatch);
    }
}
