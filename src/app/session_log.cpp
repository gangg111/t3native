#include "app/session_log.h"

#include "core/fight/fight_simulation.h"
#include "core/fight/fight_state.h"
#include "core/fighter/fighter_state.h"
#include "core/game/character_select.h"
#include "core/game/game_flow.h"
#include "core/game/game_progress.h"
#include "core/game/mode_region.h"
#include "core/game/practice_mode.h"
#include "core/game/ranking_screen.h"
#include "core/game/result_screens.h"
#include "core/math/c_rand.h"

namespace t3 {

SessionLog::Step SessionLog::capture(const GameFlow& flow, const gd::PI32& pads) {
    Step s;
    s.pad0 = static_cast<uint32_t>(gd::Int(pads[0]));
    s.pad1 = static_cast<uint32_t>(gd::Int(pads[1]));
    if (flow.escape_pressed) s.flags |= ESCAPE;
    if (flow.sim->pads->connected != 3) s.flags |= ONE_PAD;
    s.movie_result = static_cast<int8_t>(flow.movie_result);
    return s;
}

void SessionLog::prepare(GameFlow& flow, const Step& step) {
    flow.escape_pressed = (step.flags & ESCAPE) != 0;
    flow.sim->pads->connected = (step.flags & ONE_PAD) != 0 ? 1 : 3;
    flow.movie_result = step.movie_result;
    flow.fight->state_items = true;
}

std::vector<uint8_t> SessionLog::bytes_of(const gd::PBytes& bytes) {
    std::vector<uint8_t> out(static_cast<size_t>(bytes.size()));
    for (size_t i = 0; i < out.size(); ++i) out[i] = static_cast<uint8_t>(bytes.decode_u8(static_cast<gd::Int>(i)));
    return out;
}

void SessionLog::put_bytes(const gd::PBytes& into, const std::vector<uint8_t>& bytes) {
    gd::Int n = std::min(into.size(), static_cast<gd::Int>(bytes.size()));
    for (gd::Int i = 0; i < n; ++i) into.encode_u8(i, bytes[static_cast<size_t>(i)]);
}

uint64_t SessionLog::checksum(const GameFlow& flow) {
    uint64_t h = 0xCBF29CE484222325ULL;   // FNV-1a
    auto mix = [&](uint64_t v) {
        for (int i = 0; i < 8; ++i) {
            h ^= (v >> (8 * i)) & 0xFF;
            h *= 0x100000001B3ULL;
        }
    };
    auto block = [&](const gd::Ref<ByteBlock>& b) {
        if (b == nullptr) return mix(0);
        gd::Int n = b->bytes.size();
        mix(static_cast<uint64_t>(n));
        for (gd::Int i = 0; i < n; ++i) mix(static_cast<uint64_t>(b->bytes.decode_u8(i)));
    };
    mix(static_cast<uint64_t>(flow.state));
    mix(static_cast<uint64_t>(flow.sub));
    block(flow.progress);
    block(flow.region);
    block(flow.select->ctx);
    block(flow.select->cells);
    block(flow.ranking->table);
    block(flow.ranking->name);
    block(flow.ranking->backdrop);
    block(flow.results->state);
    block(flow.results->banner);
    block(flow.practice->s);
    const FightState& f = *flow.fight;
    mix(static_cast<uint64_t>(f.rng->state));
    mix(static_cast<uint64_t>(f.camera_rng));
    mix(static_cast<uint64_t>(f.frame_rng));
    mix(static_cast<uint64_t>(f.ai_rng));
    mix(static_cast<uint64_t>(f.vblank));
    for (gd::Int i = 0; i < f.fighters.size(); ++i) {
        const gd::Ref<FighterState>& p = f.fighters[i];
        if (p == nullptr) continue;
        mix(static_cast<uint64_t>(p->pos_x));
        mix(static_cast<uint64_t>(p->pos_y));
        mix(static_cast<uint64_t>(p->pos_z));
        mix(static_cast<uint64_t>(p->health));
    }
    return h;
}

namespace {

void put32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put64(std::vector<uint8_t>& out, uint64_t v) {
    put32(out, static_cast<uint32_t>(v));
    put32(out, static_cast<uint32_t>(v >> 32));
}
void put_blob(std::vector<uint8_t>& out, const std::vector<uint8_t>& b) {
    put32(out, static_cast<uint32_t>(b.size()));
    out.insert(out.end(), b.begin(), b.end());
}

struct Reader {
    const std::vector<uint8_t>& d;
    size_t at = 0;
    bool ok = true;
    uint32_t u32() {
        if (at + 4 > d.size()) return ok = false, 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(d[at + i]) << (8 * i);
        at += 4;
        return v;
    }
    uint64_t u64() {
        uint64_t lo = u32();
        return lo | (static_cast<uint64_t>(u32()) << 32);
    }
    uint8_t u8() {
        if (at + 1 > d.size()) return ok = false, 0;
        return d[at++];
    }
    std::vector<uint8_t> blob() {
        uint32_t n = u32();
        if (!ok || at + n > d.size()) return ok = false, std::vector<uint8_t>();
        std::vector<uint8_t> b(d.begin() + static_cast<long>(at), d.begin() + static_cast<long>(at + n));
        at += n;
        return b;
    }
};

}  // namespace

std::vector<uint8_t> SessionLog::serialize() const {
    std::vector<uint8_t> out;
    put32(out, MAGIC);
    put32(out, VERSION);
    put64(out, static_cast<uint64_t>(seed));
    out.push_back(movies ? 1 : 0);
    out.push_back(fixes ? 1 : 0);
    put64(out, check);
    put_blob(out, progress);
    put32(out, static_cast<uint32_t>(steps.size()));
    for (const Step& s : steps) {
        put32(out, s.pad0);
        put32(out, s.pad1);
        out.push_back(s.flags);
        out.push_back(static_cast<uint8_t>(s.movie_result));
    }
    put32(out, static_cast<uint32_t>(events.size()));
    for (const Event& e : events) {
        put32(out, e.step);
        out.push_back(e.kind);
        out.push_back(e.inside ? 1 : 0);
        put32(out, static_cast<uint32_t>(e.value));
        put_blob(out, e.bytes);
    }
    return out;
}

bool SessionLog::parse(const std::vector<uint8_t>& data) {
    Reader r{data};
    if (r.u32() != MAGIC || r.u32() != VERSION || !r.ok) return false;
    SessionLog log;
    log.seed = static_cast<int64_t>(r.u64());
    log.movies = r.u8() != 0;
    log.fixes = r.u8() != 0;
    log.check = r.u64();
    log.progress = r.blob();
    uint32_t n = r.u32();
    if (!r.ok || static_cast<size_t>(n) * 10 > data.size()) return false;
    log.steps.resize(n);
    for (Step& s : log.steps) {
        s.pad0 = r.u32();
        s.pad1 = r.u32();
        s.flags = r.u8();
        s.movie_result = static_cast<int8_t>(r.u8());
    }
    uint32_t m = r.u32();
    if (!r.ok || static_cast<size_t>(m) * 14 > data.size()) return false;
    log.events.resize(m);
    for (Event& e : log.events) {
        e.step = r.u32();
        e.kind = r.u8();
        e.inside = r.u8() != 0;
        e.value = static_cast<int32_t>(r.u32());
        e.bytes = r.blob();
    }
    if (!r.ok || r.at != data.size()) return false;
    *this = std::move(log);
    return true;
}

}  // namespace t3
