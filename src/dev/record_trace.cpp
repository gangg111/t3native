#include "dev/record_trace.h"

#include "core/math/fx.h"
#include "content/json_file.h"
#include "gd/io.h"
#include "platform/log.h"

namespace t3 {

const char* RecordTrace::FIELDS = "tools/ghidra/fighter_fields.tsv";

std::string TraceFiles::path(const std::string& file) { return gd::path_join(directory, file); }

std::string source_root() {
    // work/traces -> the repository root two levels up.
    std::string traces = TraceFiles::directory;
    for (int up = 0; up < 2; ++up) {
        size_t cut = traces.find_last_of("/\\");
        if (cut == std::string::npos) return ".";
        traces = traces.substr(0, cut);
    }
    return traces;
}

namespace {
gd::Int size_of(const std::string& type) {
    if (type == "u8" || type == "s8") return 1;
    if (type == "u16" || type == "s16") return 2;
    if (type == "u32" || type == "s32" || type == "void*") return 4;
    return 0;
}
}  // namespace

bool RecordTrace::_unpack(const gd::PBytes& data, const std::string& magic, gd::Int version, const std::string& fields_path) {
    if (data.size() < 12 || data.slice(0, 4).get_string_from_ascii() != magic || data.decode_u32(4) != version) return false;
    gd::Int head_size = data.decode_u32(8);
    header = gd::Json::parse(data.slice(12, 12 + head_size).get_string_from_utf8());
    gd::Int pos = 12 + head_size;
    gd::Int raw_size = data.decode_u32(pos);
    gd::Int packed_size = data.decode_u32(pos + 4);
    body = gd::unzlib(data.slice(pos + 8, pos + 8 + packed_size));
    if (body.size() != raw_size) return false;
    frames = header["frames"].to_int();
    fighter_size = header["fighter_size"].to_int();
    for (const gd::Json& k : header["call_kinds"].items()) call_kinds.push_back(k.str());
    _read_fields(fields_path);
    return true;
}

gd::Int RecordTrace::_index_ranges() {
    gd::Int offset = 0;
    for (const gd::Json& r : header["ranges"].items()) {
        gd::Int size = r[2].to_int();
        gd::Int address = r[1].to_int();
        ranges.append(gd::PI64{address & 0xFFFFFFFFLL, size, offset});
        offset += size;
    }
    return offset;
}

gd::Int RecordTrace::_calls_size(gd::Int pos, gd::Int count) const {
    gd::Int at = pos;
    for (gd::Int c = 0; c < count; ++c) at += 2 + 4 * body.decode_u8(at + 1);
    return at - pos;
}

std::vector<Call> RecordTrace::_calls_at(gd::Int pos, gd::Int count) const {
    std::vector<Call> out;
    for (gd::Int c = 0; c < count; ++c) {
        Call entry{call_kinds[static_cast<size_t>(body.decode_u8(pos))], {}};
        gd::Int args = body.decode_u8(pos + 1);
        for (gd::Int a = 0; a < args; ++a) entry.args.push_back(body.decode_s32(pos + 2 + 4 * a));
        out.push_back(entry);
        pos += 2 + 4 * args;
    }
    return out;
}

gd::Int RecordTrace::_range_of(gd::Int address, gd::Int size) const {
    address &= 0xFFFFFFFFLL;
    for (const gd::PI64& r : ranges)
        if (address >= r[0] && address + size <= r[0] + r[1]) return r[2] + address - r[0];
    return -1;
}

void RecordTrace::_read_fields(const std::string& path) {
    std::string tsv = gd::read_file(path).get_string_from_utf8();
    if (tsv.empty()) t3::Log::error("RecordTrace: cannot read " + path);
    size_t start = 0;
    while (start < tsv.size()) {
        size_t end = tsv.find('\n', start);
        if (end == std::string::npos) end = tsv.size();
        std::string line = tsv.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line.find_first_not_of(" \t") == std::string::npos) continue;
        std::vector<std::string> cols;
        size_t a = 0;
        while (true) {
            size_t b = line.find('\t', a);
            cols.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a));
            if (b == std::string::npos) break;
            a = b + 1;
        }
        if (cols.size() < 3 || size_of(cols[1]) == 0) continue;
        field_types.emplace_back(cols[2], cols[1]);
        _fields.set(cols[2], gd::Vector2i(std::stoll(cols[0], nullptr, 16), signed_size(cols[1])));
    }
}

gd::Int RecordTrace::signed_size(const std::string& type) {
    gd::Int size = size_of(type);
    return type.rfind("s", 0) == 0 ? -size : size;
}

gd::Int RecordTrace::width(gd::Int v, gd::Int size) {
    switch (size) {
        case 1: return v & 0xFF;
        case -1: return t3::Fx::s8(v);
        case 2: return v & 0xFFFF;
        case -2: return t3::Fx::s16(v);
        case 4: return v & 0xFFFFFFFFLL;
        default: return t3::Fx::w32(v);
    }
}

gd::Int RecordTrace::field(gd::Int frame, gd::Int index, const std::string& name) const {
    gd::Vector2i f = _fields.at(name);
    return read(record(frame, index) + f.x, f.y);
}

gd::Int RecordTrace::read(gd::Int pos, gd::Int size) const {
    switch (size) {
        case 1: return body.decode_u8(pos);
        case -1: return body.decode_s8(pos);
        case 2: return body.decode_u16(pos);
        case -2: return body.decode_s16(pos);
        case 4: return body.decode_u32(pos);
        default: return body.decode_s32(pos);
    }
}

gd::PI64 RecordTrace::row_addresses(gd::Int frame, gd::Int bank_type, gd::Int index) const {
    gd::PI64 out;
    for (const gd::Json& epoch : header["bank_epochs"].items()) {
        if (t3::JsonFile::number(epoch["frame"]) > frame) break;
        for (const gd::Json& bank : epoch["banks"].items())
            if (t3::JsonFile::number(bank["type"]) == bank_type)
                out.append(t3::JsonFile::number(bank["sections"][1]) + index * MOVE_ROW_BYTES);
    }
    return out;
}

gd::Vector2i RecordTrace::row_of(gd::Int frame, gd::Int pointer) const {
    if (pointer == 0) return gd::Vector2i(-1, -1);
    const std::vector<gd::Json>* banks = nullptr;
    for (const gd::Json& epoch : header["bank_epochs"].items()) {
        if (t3::JsonFile::number(epoch["frame"]) > frame) break;
        banks = &epoch["banks"].items();
    }
    if (banks)
        for (size_t b = 0; b < banks->size(); ++b) {
            const gd::Json& sections = (*banks)[b]["sections"];
            gd::Int rows = sections[1].to_int();
            gd::Int rows_end = sections[2].to_int();
            // The common bank has no move index: its section-2 pointer stays unrelocated.
            if (rows_end < rows) rows_end = sections[3].to_int();
            if (pointer >= rows && pointer < rows_end)
                return gd::Vector2i(static_cast<gd::Int>(b), (pointer - rows) / MOVE_ROW_BYTES);
        }
    return gd::Vector2i(-2, pointer);
}

std::string FightTrace::path(const std::string& scenario) { return TraceFiles::path("fight_" + scenario + ".bin"); }

gd::Ref<FightTrace> FightTrace::load_scenario(const std::string& scenario) {
    auto t = gd::make<FightTrace>();
    if (!t->_unpack(gd::read_file(path(scenario)), MAGIC, VERSION, gd::path_join(source_root(), FIELDS))) {
        t3::Log::error("FightTrace: " + scenario + " is not a version 2 fight trace");
        return nullptr;
    }
    t->_index();
    return t;
}

void FightTrace::_index() {
    gd::Int range_bytes = _index_ranges();
    gd::Int pos = 0;
    for (gd::Int f = 0; f < frames; ++f) {
        _frame_starts.append(pos);
        pos += 6 + _calls_size(pos + 6, body.decode_u8(pos + 5));
        _record_starts.append(pos);
        pos += 2 * fighter_size + range_bytes;
    }
}

std::string FlowTrace::path(const std::string& scenario) { return TraceFiles::path("flow_" + scenario + ".bin"); }

gd::Ref<FlowTrace> FlowTrace::load_scenario(const std::string& scenario) {
    auto t = gd::make<FlowTrace>();
    if (!t->_unpack(gd::read_file(path(scenario)), MAGIC, VERSION, gd::path_join(source_root(), FIELDS))) {
        t3::Log::error("FlowTrace: " + scenario + " is not a version 2 flow trace");
        return nullptr;
    }
    t->_index();
    return t;
}

void FlowTrace::_index() {
    gd::Int range_bytes = _index_ranges();
    gd::Int pointed_bytes = 0;
    for (const gd::Json& p : header.get("pointed").items()) {
        gd::Int size = p[2].to_int();
        pointed.set(p[0].str(), gd::Vector2i(pointed_bytes, size));
        pointed_bytes += size;
    }
    gd::Int pos = 0;
    for (gd::Int f = 0; f < frames; ++f) {
        _frame_starts.append(pos);
        pos += 9 + _calls_size(pos + 9, body.decode_u8(pos + 8));
        _range_starts.append(pos);
        pos += range_bytes;
        _pointed_starts.append(pos);
        pos += pointed_bytes;
        gd::Int fighters = body.decode_u8(pos);
        pos += 1;
        _fighter_starts.append(fighters != 0 ? pos : -1);
        _fighter_counts.append(fighters);
        pos += fighters * fighter_size;
    }
}

gd::Int FlowTrace::ram(gd::Int frame, gd::Int address, gd::Int size) const {
    gd::Int at = _range_of(address, gd::absi(size));
    if (at < 0) {
        t3::Log::error("FlowTrace: address not recorded");
        return 0;
    }
    return read(_range_starts[frame] + at, size);
}

gd::PBytes FlowTrace::block(gd::Int frame, gd::Int address, gd::Int size) const {
    gd::Int at = _range_of(address, size);
    if (at < 0) {
        t3::Log::error("FlowTrace: block not recorded");
        return gd::PBytes();
    }
    return body.slice(_range_starts[frame] + at, _range_starts[frame] + at + size);
}

gd::Arr<gd::PI64> FlowTrace::pokes() const {
    gd::Arr<gd::PI64> out;
    for (const gd::Json& p : header.get("pokes").items())
        out.append(gd::PI64{t3::JsonFile::number(p[0]), t3::JsonFile::number(p[1])});
    return out;
}

gd::Int FightTrace::ram(gd::Int frame, gd::Int address, gd::Int size) const {
    gd::Int at = _range_of(address, gd::absi(size));
    if (at < 0) {
        t3::Log::error("FightTrace: address not recorded");
        return 0;
    }
    return read(_record_starts[frame] + 2 * fighter_size + at, size);
}

}  // namespace t3
