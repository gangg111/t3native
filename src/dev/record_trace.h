#pragma once
// remake/dev/record_trace.gd + tests/support/fight_trace.gd: what fight and flow traces share (the
// file layout: magic, version, JSON header, deflated body), the engine calls and recorded RAM
// ranges of a frame, the raw fighter records of a frame, read by field name
// (`tools/ghidra/fighter_fields.tsv`) or offset, and the motion banks' epochs that turn the
// records' move-row pointers into rows (FighterCompare).
#include <string>
#include <vector>

#include "gd/json.h"

namespace t3 {

// One logged engine call: its kind name and arguments.
struct Call {
    std::string kind;
    std::vector<gd::Int> args;
};

class RecordTrace {
public:
    static constexpr gd::Int MOVE_ROW_BYTES = 56;
    static const char* FIELDS;   // relative to the source repository root

    gd::Json header;
    gd::Int frames = 0;
    gd::PBytes body;
    gd::Int fighter_size = 0;
    std::vector<std::string> call_kinds;
    gd::Arr<gd::PI64> ranges;                     // (address, size, offset in the frame's range block)
    std::vector<std::pair<std::string, std::string>> field_types;   // name -> type, in file order
    gd::Dict<std::string, gd::Vector2i> _fields;  // name -> (offset, size), negative size: signed

    virtual ~RecordTrace() = default;
    // Offset of fighter `index`'s record of a frame in `body`.
    virtual gd::Int record(gd::Int frame, gd::Int index) const = 0;

    bool _unpack(const gd::PBytes& data, const std::string& magic, gd::Int version, const std::string& fields_path);
    gd::Int _index_ranges();
    gd::Int _calls_size(gd::Int pos, gd::Int count) const;
    std::vector<Call> _calls_at(gd::Int pos, gd::Int count) const;
    gd::Int _range_of(gd::Int address, gd::Int size) const;

    static gd::Int signed_size(const std::string& type);
    static gd::Int width(gd::Int v, gd::Int size);
    gd::Int field_offset(const std::string& name) const { return _fields.at(name).x; }
    gd::Int field(gd::Int frame, gd::Int index, const std::string& name) const;
    gd::Int fighter_s32(gd::Int frame, gd::Int index, gd::Int offset) const { return body.decode_s32(record(frame, index) + offset); }
    gd::Int fighter_u32(gd::Int frame, gd::Int index, gd::Int offset) const { return body.decode_u32(record(frame, index) + offset); }
    gd::Int read(gd::Int pos, gd::Int size) const;

    gd::PI64 row_addresses(gd::Int frame, gd::Int bank_type, gd::Int index) const;
    gd::Vector2i row_of(gd::Int frame, gd::Int pointer) const;

private:
    void _read_fields(const std::string& path);
};

class FightTrace : public RecordTrace {
public:
    static constexpr const char* MAGIC = "T3FT";
    static constexpr gd::Int VERSION = 2;

    gd::PI32 _frame_starts;
    gd::PI32 _record_starts;   // where the fighter records of a frame begin

    static std::string path(const std::string& scenario);
    static gd::Ref<FightTrace> load_scenario(const std::string& scenario);

    gd::Int pad(gd::Int frame, gd::Int player) const { return body.decode_u16(_frame_starts[frame] + 2 * player); }
    gd::Int sub_state(gd::Int frame) const { return body.decode_s8(_frame_starts[frame] + 4); }
    std::vector<Call> calls(gd::Int frame) const {
        return _calls_at(_frame_starts[frame] + 6, body.decode_u8(_frame_starts[frame] + 5));
    }
    gd::Int record(gd::Int frame, gd::Int index) const override { return _record_starts[frame] + index * fighter_size; }
    gd::Int ram(gd::Int frame, gd::Int address, gd::Int size) const;
    bool has_ram(gd::Int address, gd::Int size) const { return _range_of(address, gd::absi(size)) >= 0; }

private:
    void _index();
};

// remake/dev/flow_trace.gd: a game flow trace (`tools/research/flow_trace.py`): the whole game
// run in the CPU harness from boot. Per frame: both pads, the game state and sub-state after the
// frame, the engine calls, the recorded RAM ranges, the blocks read through pointers (the Tekken
// Ball ball) and, in the fight state, the fighter records (a third one in Tekken Ball and Tekken
// Force). The header adds the motion banks' epochs and the progress pokes.
class FlowTrace : public RecordTrace {
public:
    static constexpr const char* MAGIC = "T3GF";
    static constexpr gd::Int VERSION = 2;

    gd::Dict<std::string, gd::Vector2i> pointed;   // name -> (offset in the pointed block, size)
    gd::PI32 _frame_starts;
    gd::PI32 _range_starts;
    gd::PI32 _pointed_starts;
    gd::PI32 _fighter_starts;   // -1 when the frame has no fighter records
    gd::PBytes _fighter_counts;

    static std::string path(const std::string& scenario);
    static gd::Ref<FlowTrace> load_scenario(const std::string& scenario);

    gd::Int pad(gd::Int frame, gd::Int player) const { return body.decode_u16(_frame_starts[frame] + 2 * player); }
    gd::Int state(gd::Int frame) const { return body.decode_u16(_frame_starts[frame] + 4); }
    gd::Int sub_state(gd::Int frame) const { return body.decode_u16(_frame_starts[frame] + 6); }
    std::vector<Call> calls(gd::Int frame) const {
        return _calls_at(_frame_starts[frame] + 9, body.decode_u8(_frame_starts[frame] + 8));
    }
    gd::Int ram(gd::Int frame, gd::Int address, gd::Int size) const;
    bool has_range(gd::Int address) const { return _range_of(address, 1) >= 0; }
    gd::PBytes block(gd::Int frame, gd::Int address, gd::Int size) const;
    bool has_fighters(gd::Int frame) const { return _fighter_starts[frame] >= 0; }
    gd::Int fighter_count(gd::Int frame) const { return _fighter_counts[frame]; }
    gd::Int record(gd::Int frame, gd::Int index) const override { return _fighter_starts[frame] + index * fighter_size; }
    gd::Int fighter(gd::Int frame, gd::Int index, gd::Int offset, gd::Int size) const { return read(record(frame, index) + offset, size); }
    bool has_pointed(const std::string& name) const { return pointed.has(name); }
    gd::Int pointed_value(gd::Int frame, const std::string& name, gd::Int offset, gd::Int size) const {
        gd::Vector2i p = pointed.at(name);
        return read(_pointed_starts[frame] + p.x + offset, size);
    }
    // The progress pokes of the scenario: (address, byte) pairs written after boot.
    gd::Arr<gd::PI64> pokes() const;

private:
    void _index();
};

// remake/dev/trace_files.gd: where the golden traces are (`work/traces/`, git-ignored), and the
// source repository root two levels up (tools/ghidra/fighter_fields.tsv lives there).
struct TraceFiles {
    static inline std::string directory = T3_WORKSPACE "/Tekken3Remake/work/traces";
    static std::string path(const std::string& file);
};
std::string source_root();

}  // namespace t3
