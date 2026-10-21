// remake/tests/fight/test_fight_traces.gd: whole matches against the game's own fight code
// (`work/traces/fight_<scenario>.bin`, tools/research/fight_harness.py): the same pads frame by
// frame, then every fighter field, the round state, the camera and the sound, vibration and music
// calls compared with the game's records; in the `cpu_` matches also the AI records of the CPU
// fighters. The `fixes_` matches run the game with the Gameplay fixes patched in and the
// simulation with RuleSet::with_gameplay_fixes().
#include "content/fight_content.h"
#include "content/json_file.h"
#include "core/ai/cpu_opponent.h"
#include "core/camera/backdrop_turn.h"
#include "core/camera/camera_director.h"
#include "core/camera/camera_view.h"
#include "core/fight/fight_setup.h"
#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fight/rule_set.h"
#include "core/fighter/fighter_state.h"
#include "core/math/c_rand.h"
#include "core/sim_events.h"
#include "support/fight_compare.h"
#include "support/test_suite.h"

using namespace t3;

namespace {

constexpr size_t MAX_REPORTED = 40;

// Round-flow and camera globals compared each frame (address, size; negative: signed).
struct Global {
    const char* name;
    gd::Int address, size;
};
const Global GLOBALS[] = {
    {"round state", 0x80097350LL, -2}, {"round counter", 0x80097354LL, -4}, {"round frame", 0x80095884LL, -4},
    {"timer", 0x800AE094LL, -4}, {"camera phase", 0x80095890LL, -4}, {"camera pitch", 0x800A8A88LL, -4},
    {"camera yaw", 0x800A8A8CLL, -4}, {"camera x", 0x800A8A9CLL, -4}, {"camera y", 0x800A8AA0LL, -4},
    {"camera z", 0x800A8AA4LL, -4}, {"rand", 0x800A3E80LL, 4}, {"AI generator", 0x800AE168LL, 4},
    {"display buffer", 0x800AE3C4LL, 4}, {"bar 0 health", 0x800980F4LL, -4}, {"bar 0 target", 0x800980F8LL, -2},
    {"bar 0 drawn", 0x800980FALL, -2}, {"bar 0 recent", 0x800980FCLL, -2}, {"bar 1 health", 0x80098104LL, -4},
    {"bar 1 target", 0x80098108LL, -2}, {"bar 1 drawn", 0x8009810ALL, -2}, {"bar 1 recent", 0x8009810CLL, -2},
    {"backdrop angle", 0x800A9650LL, 4}, {"backdrop prev yaw", 0x800A9658LL, 4}, {"backdrop yaw", 0x800A9664LL, 4},
    {"look point x", 0x800AE0D8LL, -4}, {"look point z", 0x800AE0E0LL, -4},
};

gd::Int global_of(const FightSimulation& sim, const std::string& name) {
    const FightState& fight = *sim.fight;
    const CameraView& view = *sim.camera->view;
    if (name == "round state") return fight.round_state;
    if (name == "round counter") return fight.round_counter;
    if (name == "round frame") return fight.round_frame;
    if (name == "timer") return fight.timer;
    if (name == "camera phase") return fight.camera_phase;
    if (name == "camera pitch") return view.pitch;
    if (name == "camera yaw") return view.yaw;
    if (name == "camera x") return view.x;
    if (name == "camera y") return view.y;
    if (name == "camera z") return view.z;
    if (name == "rand") return fight.rng->state;
    if (name == "AI generator") return fight.ai_rng;
    if (name == "display buffer") return fight.display_buffer;
    if (name == "backdrop angle") return fight.backdrop->angle;
    if (name == "backdrop prev yaw") return fight.backdrop->prev_yaw;
    if (name == "backdrop yaw") return fight.backdrop->yaw;
    if (name == "look point x") return fight.backdrop->point_x;
    if (name == "look point z") return fight.backdrop->point_z;
    if (name.rfind("bar ", 0) == 0) {
        const gd::PI32& bar = fight.hud_bars[name[4] - '0'];
        static const char* parts[] = {"health", "target", "drawn", "recent"};
        std::string part = name.substr(6);
        for (gd::Int k = 0; k < 4; ++k) if (part == parts[k]) return bar[k];
    }
    return 0;
}

gd::Ref<FightSetup> setup_of(const t3test::FightTrace& trace) {
    const gd::Json& s = trace.header["scenario"];
    auto setup = gd::make<FightSetup>();
    setup->chars = JsonFile::ints(s["chars"]);
    setup->costumes = JsonFile::ints(s["costumes"]);
    setup->stage = JsonFile::number(s["stage"]);
    setup->seed = JsonFile::number(s["seed"]);
    setup->round_time = JsonFile::number(s["round_time"]);
    setup->rounds = JsonFile::number(s["rounds"]);
    setup->chip_damage = s["chip"].boolean();
    const gd::Json& cpu = s.get("cpu");
    if (cpu.is_array()) {
        setup->cpu = JsonFile::ints(cpu);
        setup->ai_difficulty = JsonFile::number(s["difficulty"]);
        setup->ai_level = JsonFile::number(s["level"]);
        setup->attract = s["attract"].boolean();
    }
    const gd::Json& tables = s.get("key_tables");
    if (tables.is_array())
        for (gd::Int p = 0; p < 2; ++p) setup->key_tables[p] = JsonFile::ints(tables[p]);
    return setup;
}

gd::Ref<FightContent>& content() {
    static gd::Ref<FightContent> c;
    return c;
}

void compare(t3test::Context& t, const std::string& scenario, const t3test::FightTrace& trace, FightSimulation& sim) {
    t3test::FighterCompare fighters(trace);
    bool with_ai = t3test::AiCompare::applies(trace);
    t3test::AiCompare ai_compare(trace);
    gd::Int globals = 0;
    for (const Global& g : GLOBALS) if (trace.has_ram(g.address, gd::absi(g.size))) ++globals;
    t.messages.push_back(gd::str(trace.frames) + " frames, " + gd::str(static_cast<gd::Int>(fighters.fields.size())) +
                         " fighter fields + hands + 24 joints, " + gd::str(globals) + " globals, AI " + (with_ai ? "yes" : "no") +
                         ", unmapped trace fields " + gd::str(static_cast<gd::Int>(fighters.unmapped.size())));
    for (gd::Int frame = 0; frame < trace.frames; ++frame) {
        t.expect_equal(sim.main_state, trace.sub_state(frame), scenario + " frame " + gd::str(frame) + " FightMain state");
        sim.step(gd::PI32{trace.pad(frame, 0), trace.pad(frame, 1)});
        gd::Arr<gd::Ref<MotionBank>> banks{sim.fight->fighters[0]->bank, sim.fight->fighters[1]->bank, content()->common};
        std::vector<std::string> lines;
        for (gd::Int i = 0; i < 2; ++i)
            for (const std::string& d : fighters.differences(*sim.fight->fighters[i], frame, i, banks))
                lines.push_back("fighter " + gd::str(i) + " " + d);
        if (with_ai)
            for (gd::Int slot = 0; slot < 2; ++slot)
                for (const std::string& d : ai_compare.differences(*sim.ai, slot, frame, banks)) lines.push_back(d);
        for (const Global& g : GLOBALS) {
            if (!trace.has_ram(g.address, gd::absi(g.size))) continue;   // recorded before the harness kept it
            gd::Int expected = trace.ram(frame, g.address, g.size);
            gd::Int actual = global_of(sim, g.name);
            if (t3test::RecordTrace::width(actual, g.size) != expected)
                lines.push_back(std::string(g.name) + ": " + gd::str(actual) + " != " + gd::str(expected));
        }
        std::vector<std::string> ours = t3test::FightCalls::of_events(*sim.events);
        std::vector<std::string> theirs = t3test::FightCalls::of_trace(trace, frame);
        if (ours != theirs) {
            std::string a, b;
            for (const std::string& s : ours) a += "\"" + s + "\" ";
            for (const std::string& s : theirs) b += "\"" + s + "\" ";
            lines.push_back("calls: [" + a + "] != [" + b + "]");
        }
        if (!lines.empty()) {
            std::string shown;
            for (size_t k = 0; k < lines.size() && k < MAX_REPORTED; ++k) shown += "\n    " + lines[k];
            t.expect(false, scenario + " frame " + gd::str(frame) + " (FightMain " + gd::str(trace.sub_state(frame)) +
                                ", round state " + gd::str(sim.fight->round_state) + "):" + shown);
            return;
        }
    }
}

void run(t3test::Context& t, const std::string& scenario) {
    if (!t.require(t3test::FightTrace::path(scenario), "run tools/research/export_traces.py --fight first")) return;
    if (!t.require(t3test::imported("tables/fight.json"))) return;
    auto trace = t3test::FightTrace::load_scenario(scenario);
    if (!t.expect(trace != nullptr, scenario + " does not load")) return;
    if (!content()) content() = FightContent::load_from(t3test::paths().imported);
    const gd::Json& fixes_flag = trace->header["scenario"].get("gameplay_fixes");
    bool fixes = fixes_flag.is_bool() && fixes_flag.boolean();
    auto rules = fixes ? RuleSet::with_gameplay_fixes() : RuleSet::original();
    auto sim = gd::make<FightSimulation>(content(), setup_of(*trace), rules);
    compare(t, scenario, *trace, *sim);
}

}  // namespace

#define FIGHT_TRACE(name) \
    T3_TEST(fight_traces, name) { run(t, #name); }

FIGHT_TRACE(paul_law)
FIGHT_TRACE(lei_king)
FIGHT_TRACE(yoshimitsu_nina)
FIGHT_TRACE(hwoarang_xiaoyu)
FIGHT_TRACE(eddy_jin)
FIGHT_TRACE(julia_kuma)
FIGHT_TRACE(bryan_heihachi)
FIGHT_TRACE(ogre_mokujin)
FIGHT_TRACE(jack_gon)
FIGHT_TRACE(anna_doctorb)
FIGHT_TRACE(trueogre_tiger)
FIGHT_TRACE(panda_anna)
FIGHT_TRACE(xiaoyu_king_chip)
FIGHT_TRACE(draw)
FIGHT_TRACE(perfect)
FIGHT_TRACE(long_match)
FIGHT_TRACE(fixes_paul_law)
FIGHT_TRACE(fixes_lei_king)
FIGHT_TRACE(fixes_yoshimitsu_nina)
FIGHT_TRACE(fixes_hwoarang_xiaoyu)
FIGHT_TRACE(fixes_eddy_jin)
FIGHT_TRACE(fixes_julia_kuma)
FIGHT_TRACE(fixes_bryan_heihachi)
FIGHT_TRACE(fixes_ogre_mokujin)
FIGHT_TRACE(fixes_jack_gon)
FIGHT_TRACE(fixes_anna_doctorb)
FIGHT_TRACE(fixes_trueogre_tiger)
FIGHT_TRACE(fixes_panda_anna)
FIGHT_TRACE(fixes_xiaoyu_king_chip)
FIGHT_TRACE(fixes_draw)
FIGHT_TRACE(fixes_perfect)
FIGHT_TRACE(fixes_long_match)
FIGHT_TRACE(cpu_paul_king)
FIGHT_TRACE(cpu_yoshimitsu_hwoarang)
FIGHT_TRACE(cpu_ogre_jin)
FIGHT_TRACE(cpu_gon_nina)
FIGHT_TRACE(cpu_king_law)
FIGHT_TRACE(cpu_trueogre_xiaoyu)
FIGHT_TRACE(cpu_spam_eddy)
FIGHT_TRACE(cpu_attract)
FIGHT_TRACE(cpu_vs_cpu)
FIGHT_TRACE(cpu_julia_heihachi)
