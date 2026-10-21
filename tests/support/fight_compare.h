#pragma once
// remake/tests/support/fighter_compare.gd, ai_compare.gd and fight_calls.gd: a FighterState and
// the CPU's AI records against a fight trace's records field by field, and the frame's engine
// calls against the SimEvents that stand for them.
#include <string>
#include <vector>

#include "content/motion_bank.h"
#include "record_trace.h"

namespace t3 {
class FighterState;
class CpuOpponent;
class SimEvents;
class MoveRow;
}  // namespace t3

namespace t3test {

class FighterCompare {
public:
    static constexpr gd::Int JOINTS = 0x8F4;
    static constexpr gd::Int JOINT_BYTES = 0x44;
    static constexpr gd::Int JOINT_T = 0x14;
    static constexpr gd::Int STALE_BANK = -3;

    struct Field {
        gd::Int offset;
        std::string type, name, property;
        size_t accessor;   // index into fighter_state_fields()
    };

    const RecordTrace* trace;
    std::vector<Field> fields;
    std::vector<std::string> unmapped;

    explicit FighterCompare(const RecordTrace& fight_trace);
    static std::string _snake(const std::string& name);
    std::vector<std::string> differences(const t3::FighterState& f, gd::Int frame, gd::Int index,
                                         const gd::Arr<gd::Ref<t3::MotionBank>>& banks) const;
    static gd::Vector2i _row_key(const gd::Ref<t3::MoveRow>& row, const gd::Arr<gd::Ref<t3::MotionBank>>& banks);
};

class AiCompare {
public:
    static constexpr gd::Int RECORDS = 0x8009F6C0LL;
    static constexpr gd::Int RECORD_SIZE = 0x330;
    static constexpr gd::Int MULTI = 0x8009F6A4LL;

    const FightTrace* trace;
    explicit AiCompare(const FightTrace& fight_trace) : trace(&fight_trace) {}
    static bool applies(const FightTrace& fight_trace) { return fight_trace.has_ram(RECORDS, 4); }
    std::vector<std::string> differences(const t3::CpuOpponent& ai, gd::Int slot, gd::Int frame,
                                         const gd::Arr<gd::Ref<t3::MotionBank>>& banks) const;

private:
    void _row(std::vector<std::string>& out, const gd::Ref<t3::MoveRow>& row, gd::Int pointer, gd::Int frame,
              const gd::Arr<gd::Ref<t3::MotionBank>>& banks, const std::string& name) const;
};

namespace FightCalls {
std::vector<std::string> of_trace(const FightTrace& trace, gd::Int frame);
std::vector<std::string> of_list(const std::vector<Call>& calls);
std::vector<std::string> of_events(const t3::SimEvents& events);
}  // namespace FightCalls

}  // namespace t3test
