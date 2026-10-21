#include "net/net_session.h"

#include <algorithm>

#include "platform/log.h"

namespace t3::net {

namespace {

void put8(std::vector<uint8_t>& o, uint8_t v) { o.push_back(v); }
void put32(std::vector<uint8_t>& o, uint32_t v) {
    for (int i = 0; i < 4; ++i) o.push_back(static_cast<uint8_t>(v >> (8 * i)));
}
void put64(std::vector<uint8_t>& o, uint64_t v) {
    put32(o, static_cast<uint32_t>(v));
    put32(o, static_cast<uint32_t>(v >> 32));
}
void put_blob(std::vector<uint8_t>& o, const std::vector<uint8_t>& b) {
    put32(o, static_cast<uint32_t>(b.size()));
    o.insert(o.end(), b.begin(), b.end());
}
void put_text(std::vector<uint8_t>& o, const std::string& s) {
    size_t n = std::min<size_t>(s.size(), 255);
    put8(o, static_cast<uint8_t>(n));
    o.insert(o.end(), s.begin(), s.begin() + static_cast<long>(n));
}

struct Reader {
    const std::vector<uint8_t>& d;
    size_t at = 0;
    bool ok = true;
    bool has(size_t n) {
        if (at + n > d.size()) ok = false;
        return ok;
    }
    uint8_t u8() { return has(1) ? d[at++] : 0; }
    uint32_t u32() {
        if (!has(4)) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= static_cast<uint32_t>(d[at + i]) << (8 * i);
        at += 4;
        return v;
    }
    uint64_t u64() {
        uint64_t lo = u32();
        return lo | (static_cast<uint64_t>(u32()) << 32);
    }
    std::vector<uint8_t> bytes(size_t n) {
        if (!has(n)) return {};
        std::vector<uint8_t> b(d.begin() + static_cast<long>(at), d.begin() + static_cast<long>(at + n));
        at += n;
        return b;
    }
    std::vector<uint8_t> blob() { return bytes(u32()); }
    std::string text() {
        std::vector<uint8_t> b = bytes(u8());
        return std::string(b.begin(), b.end());
    }
};

}  // namespace

bool NetSession::host(const Identity& me, const Start& start, double now) {
    close(Reason::NONE);
    me_ = me;
    start_ = start;
    host_ = true;
    reason_ = Reason::NONE;
    if (!socket_.open(PORT)) {
        _end(Reason::NO_PORT);
        return false;
    }
    // A session number the stray packets of an earlier session do not carry.
    session_ = static_cast<uint32_t>(static_cast<uint64_t>(now * 1e6)) | 1u;
    heard_ = now;
    phase_ = Phase::HOSTING;
    return true;
}

bool NetSession::search(const Identity& me, double now) {
    close(Reason::NONE);
    me_ = me;
    host_ = false;
    reason_ = Reason::NONE;
    if (!socket_.open(0)) {
        _end(Reason::NO_PORT);
        return false;
    }
    offers_.clear();
    last_search_ = -1;
    heard_ = now;
    phase_ = Phase::SEARCHING;
    return true;
}

void NetSession::join(const Address& host, const GuestProgress& mine, double now) {
    if (phase_ != Phase::SEARCHING && phase_ != Phase::JOINING) return;
    peer_ = host;
    mine_ = mine;
    heard_ = now;
    last_welcome_ = -1;
    phase_ = Phase::JOINING;
}

void NetSession::close(Reason why) {
    if (socket_.is_open() && peer_.valid() && (phase_ == Phase::PLAYING || phase_ == Phase::JOINING)) {
        std::vector<uint8_t> body;
        put8(body, static_cast<uint8_t>(why));
        for (int i = 0; i < 3; ++i) _send(BYE, body, peer_);
        Log::info("net: BYE sent (" + std::to_string(static_cast<int>(why)) + ") to " + peer_.text(true));
    }
    socket_.close();
    if (phase_ != Phase::IDLE && phase_ != Phase::ENDED) {
        phase_ = Phase::ENDED;
        if (reason_ == Reason::NONE) reason_ = why;
    }
    if (why == Reason::NONE) phase_ = Phase::IDLE;
    peer_ = Address{};
    peer_name_.clear();
    offers_.clear();
    pads_.clear();
    pads_first_ = 0;
    guest_pads_.clear();
    stream_.clear();
    stream_base_ = acked_ = sent_until_ = 0;
    received_.clear();
    received_end_ = 0;
    ahead_.clear();
}

void NetSession::_end(Reason why) {
    Phase was = phase_;
    reason_ = why;
    close(why);
    phase_ = Phase::ENDED;
    reason_ = why;
    (void)was;
}

void NetSession::_send(Type type, const std::vector<uint8_t>& body, const Address& to) {
    std::vector<uint8_t> p;
    put32(p, MAGIC);
    put8(p, VERSION);
    put8(p, type);
    put32(p, session_);
    p.insert(p.end(), body.begin(), body.end());
    socket_.send(to, p);
}

void NetSession::poll(double now) {
    if (!socket_.is_open()) return;
    Address from;
    std::vector<uint8_t> packet;
    while (socket_.is_open() && socket_.receive(from, packet)) _receive(from, packet, now);
    if (!socket_.is_open()) return;
    switch (phase_) {
        case Phase::SEARCHING:
            if (last_search_ < 0 || now - last_search_ >= SEARCH_EVERY) {
                last_search_ = now;
                for (const Address& a : broadcast_targets(PORT)) _send(DISCOVER, {}, a);
            }
            offers_.erase(std::remove_if(offers_.begin(), offers_.end(), [&](const Offer& o) { return now - o.seen > OFFER_LIFE; }),
                          offers_.end());
            break;
        case Phase::JOINING:
            if (last_welcome_ < 0 || now - last_welcome_ >= WELCOME_EVERY) {
                last_welcome_ = now;
                std::vector<uint8_t> body;
                put_text(body, me_.build);
                put64(body, me_.data);
                put_text(body, me_.name);
                put64(body, mine_.unlocked);
                put64(body, mine_.costumes);
                _send(JOIN, body, peer_);
            }
            if (now - heard_ > TIMEOUT) _end(Reason::TIMEOUT);
            break;
        case Phase::PLAYING:
            if (host_) {
                // Everything the guest has not acknowledged, again each poll (a LAN's loss is rare
                // and the stream small); an empty chunk keeps the guest hearing from the host.
                uint64_t end = stream_base_ + stream_.size();
                uint64_t at = acked_;
                int chunks = 0;
                do {
                    size_t n = static_cast<size_t>(std::min<uint64_t>(CHUNK, end - at));
                    std::vector<uint8_t> body;
                    put64(body, at);
                    put32(body, static_cast<uint32_t>(n));
                    size_t from_index = static_cast<size_t>(at - stream_base_);
                    body.insert(body.end(), stream_.begin() + static_cast<long>(from_index),
                                stream_.begin() + static_cast<long>(from_index + n));
                    _send(STREAM, body, peer_);
                    at += n;
                    chunks += 1;
                } while (at < end && chunks < CHUNKS_PER_POLL);
            } else {
                std::vector<uint8_t> body;
                uint32_t count = static_cast<uint32_t>(std::min<size_t>(pads_.size(), PADS_SENT));
                uint32_t first = pads_first_ + static_cast<uint32_t>(pads_.size()) - count;
                put64(body, received_end_);
                put32(body, first);
                put8(body, static_cast<uint8_t>(count));
                for (uint32_t i = 0; i < count; ++i) put32(body, pads_[pads_.size() - count + i]);
                _send(PADS, body, peer_);
            }
            if (now - heard_ > TIMEOUT) _end(Reason::TIMEOUT);
            break;
        default:
            break;
    }
}

void NetSession::_receive(const Address& from, const std::vector<uint8_t>& packet, double now) {
    Reader r{packet};
    if (r.u32() != MAGIC || r.u8() != VERSION || !r.ok) return;
    uint8_t type = r.u8();
    uint32_t session = r.u32();
    if (!r.ok) return;
    switch (type) {
        case DISCOVER:
            if (host_ && phase_ == Phase::HOSTING) {
                std::vector<uint8_t> body;
                put_text(body, me_.name);
                _send(OFFER, body, from);
            }
            break;
        case OFFER:
            if (!host_ && phase_ == Phase::SEARCHING) {
                std::string name = r.text();
                if (!r.ok) return;
                auto it = std::find_if(offers_.begin(), offers_.end(), [&](const Offer& o) { return o.id == session; });
                if (it == offers_.end()) offers_.push_back(Offer{from, name, now, session});
                else it->name = name, it->seen = now;
            }
            break;
        case JOIN: {
            if (!host_) return;
            std::string build = r.text();
            uint64_t data = r.u64();
            std::string name = r.text();
            GuestProgress g;
            g.unlocked = r.u64();
            g.costumes = r.u64();
            if (!r.ok) return;
            if (phase_ == Phase::PLAYING && from != peer_) {
                std::vector<uint8_t> body{static_cast<uint8_t>(Reason::BUSY)};
                _send(REJECT, body, from);
                return;
            }
            if (phase_ != Phase::HOSTING && phase_ != Phase::PLAYING) return;
            Reason refuse = build != me_.build ? Reason::VERSION : (data != me_.data ? Reason::DATA : Reason::NONE);
            if (refuse != Reason::NONE) {
                std::vector<uint8_t> body{static_cast<uint8_t>(refuse)};
                _send(REJECT, body, from);
                return;
            }
            if (phase_ == Phase::HOSTING) {
                peer_ = from;
                peer_name_ = name;
                start_.guest = g;
                heard_ = now;
                phase_ = Phase::PLAYING;
            }
            std::vector<uint8_t> body;
            put64(body, static_cast<uint64_t>(start_.seed));
            put8(body, start_.movies ? 1 : 0);
            put8(body, start_.fixes ? 1 : 0);
            put8(body, start_.english ? 1 : 0);
            put8(body, start_.delay);
            put_blob(body, start_.progress);
            put64(body, start_.guest.unlocked);
            put64(body, start_.guest.costumes);
            put_text(body, me_.name);
            _send(WELCOME, body, from);
            break;
        }
        case WELCOME:
            if (!host_ && from == peer_ && phase_ == Phase::JOINING) {
                Start s;
                s.seed = static_cast<int64_t>(r.u64());
                s.movies = r.u8() != 0;
                s.fixes = r.u8() != 0;
                s.english = r.u8() != 0;
                s.delay = r.u8();
                s.progress = r.blob();
                s.guest.unlocked = r.u64();
                s.guest.costumes = r.u64();
                std::string name = r.text();
                if (!r.ok) return;
                start_ = std::move(s);
                peer_name_ = name;
                session_ = session;
                heard_ = now;
                phase_ = Phase::PLAYING;
            }
            break;
        case REJECT:
            if (!host_ && from == peer_ && phase_ == Phase::JOINING) {
                uint8_t why = r.u8();
                _end(r.ok ? static_cast<Reason>(why) : Reason::LEFT);
            }
            break;
        case PADS: {
            if (!host_ || phase_ != Phase::PLAYING || from != peer_ || session != session_) return;
            uint64_t ack = r.u64();
            uint32_t first = r.u32();
            uint8_t count = r.u8();
            std::vector<uint32_t> pads;
            for (int i = 0; i < count; ++i) pads.push_back(r.u32());
            if (!r.ok) return;
            heard_ = now;
            for (int i = 0; i < count; ++i) guest_pads_.emplace(first + static_cast<uint32_t>(i), pads[static_cast<size_t>(i)]);
            uint64_t end = stream_base_ + stream_.size();
            if (ack > acked_ && ack <= end) {
                acked_ = ack;
                // The guest has it all up to here: the stream keeps only what may be sent again.
                size_t drop = static_cast<size_t>(acked_ - stream_base_);
                if (drop > 64 * 1024) {
                    stream_.erase(stream_.begin(), stream_.begin() + static_cast<long>(drop));
                    stream_base_ = acked_;
                }
            }
            break;
        }
        case STREAM: {
            if (host_ || phase_ != Phase::PLAYING || from != peer_ || session != session_) return;
            uint64_t offset = r.u64();
            uint32_t n = r.u32();
            std::vector<uint8_t> bytes = r.bytes(n);
            if (!r.ok) return;
            heard_ = now;
            if (n == 0) return;
            if (offset > received_end_) {
                if (ahead_.size() < 256) ahead_.emplace(offset, std::move(bytes));
                return;
            }
            auto take = [&](uint64_t at, const std::vector<uint8_t>& b) {
                if (at + b.size() <= received_end_) return;
                size_t skip = static_cast<size_t>(received_end_ - at);
                received_.insert(received_.end(), b.begin() + static_cast<long>(skip), b.end());
                received_end_ = at + b.size();
            };
            take(offset, bytes);
            for (auto it = ahead_.begin(); it != ahead_.end() && it->first <= received_end_;) {
                take(it->first, it->second);
                it = ahead_.erase(it);
            }
            break;
        }
        case BYE:
            if (from == peer_ && session == session_ && (phase_ == Phase::PLAYING || phase_ == Phase::JOINING)) {
                Log::info("net: BYE from " + from.text(true));
                _end(Reason::LEFT);
            }
            break;
        default:
            break;
    }
}

void NetSession::send_pad(uint32_t step, uint32_t pad) {
    if (pads_.empty() && pads_first_ == 0) pads_first_ = step;   // the first pad (after the delay's steps)
    if (step != pads_first_ + pads_.size()) return;
    pads_.push_back(pad);
    while (pads_.size() > 2 * PADS_SENT) {
        pads_.pop_front();
        pads_first_ += 1;
    }
}

bool NetSession::take_record(std::vector<uint8_t>& record) {
    if (received_.size() < 4) return false;
    uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n |= static_cast<uint32_t>(received_[static_cast<size_t>(i)]) << (8 * i);
    if (received_.size() < 4 + static_cast<size_t>(n)) return false;
    record.assign(received_.begin() + 4, received_.begin() + 4 + static_cast<long>(n));
    received_.erase(received_.begin(), received_.begin() + 4 + static_cast<long>(n));
    return true;
}

bool NetSession::guest_pad(uint32_t step, uint32_t& pad) const {
    auto it = guest_pads_.find(step);
    if (it == guest_pads_.end()) return false;
    pad = it->second;
    return true;
}

void NetSession::put_record(const std::vector<uint8_t>& record) {
    put32(stream_, static_cast<uint32_t>(record.size()));
    stream_.insert(stream_.end(), record.begin(), record.end());
    // The guest's pads already played are not needed again.
    while (guest_pads_.size() > 4 * PADS_SENT) guest_pads_.erase(guest_pads_.begin());
}

}  // namespace t3::net
