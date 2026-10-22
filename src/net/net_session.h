#pragma once
// Native: NETWORK MODE's connection between two copies of the game on a local network.
//
// The host (player 1) runs the game; the guest (player 2) sends its pad for each step and runs the
// steps the host sends back. Both games are the same deterministic flow started the same way, so
// the host's log of each step (SessionLog: the pads, the step's other inputs, the callbacks'
// results) is all the guest needs: the guest's own pad comes back in it.
//
// One UDP port (PORT) on the host. A guest looking for games broadcasts DISCOVER; hosts answer
// OFFER. JOIN carries the guest's build and data (REJECT when they differ from the host's) and its
// own characters; WELCOME carries the session's start. Then the guest sends PADS every frame (its
// recent pads, so a lost packet costs nothing, and how much of the host's stream it has), and the
// host STREAM chunks of its step records from what the guest has, again each frame until acked.
// Either side ends with BYE; TIMEOUT seconds without a packet end it too.
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "net/udp.h"

namespace t3::net {

class NetSession {
public:
    static constexpr uint16_t PORT = 27030;
    static constexpr uint32_t MAGIC = 0x504E3354;     // "T3NP"
    static constexpr uint8_t VERSION = 1;
    static constexpr double TIMEOUT = 15.0;           // seconds without a packet from the other side
    static constexpr double OFFER_LIFE = 3.0;         // a game not heard of for this long leaves the list
    static constexpr double SEARCH_EVERY = 0.5;
    static constexpr double WELCOME_EVERY = 0.2;
    static constexpr int PADS_SENT = 32;              // the guest's pads each PADS packet repeats
    static constexpr size_t CHUNK = 1024;             // stream bytes a packet carries
    static constexpr int CHUNKS_PER_POLL = 8;

    enum class Phase { IDLE, HOSTING, SEARCHING, JOINING, PLAYING, ENDED };
    enum class Reason : uint8_t { NONE, LEFT, TIMEOUT, VERSION, DATA, BUSY, NO_PORT, DESYNC };
    enum Type : uint8_t { DISCOVER = 1, OFFER, JOIN, WELCOME, REJECT, PADS, STREAM, BYE };

    // What both sides must share, and who this side is.
    struct Identity {
        std::string build;          // the program's build
        uint64_t data = 0;          // the game data's fingerprint
        std::string name;           // the device's name, shown to the other side
    };
    // The guest's own characters (GameProgress.unlocked and start_costumes of its save).
    struct GuestProgress {
        uint64_t unlocked = 0, costumes = 0;
    };
    // The session's start, from the host.
    struct Start {
        int64_t seed = 0;
        bool movies = false;
        bool fixes = false;
        bool english = false;                // the game texts (the flow's tables follow them)
        uint8_t delay = 2;                   // steps a pad waits before it is played
        std::vector<uint8_t> progress;       // the host's progress block
        GuestProgress guest;
    };
    struct Offer {
        Address address;
        std::string name;
        double seen = 0;
        uint32_t id = 0;     // the host's session number: one game answering on two addresses is one
    };

    ~NetSession() { close(); }

    Phase phase() const { return phase_; }
    bool is_host() const { return host_; }
    Reason reason() const { return reason_; }
    const std::string& peer_name() const { return peer_name_; }
    const Start& start_info() const { return start_; }
    // The host's port was taken (another copy hosting) or there are no sockets.
    bool failed() const { return phase_ == Phase::ENDED && reason_ == Reason::NO_PORT; }

    // The host: waits for a guest on PORT; `start` is sent to the guest that joins (its own
    // characters filled in from its JOIN).
    bool host(const Identity& me, const Start& start, double now);
    // The guest: looks for games (offers()), then joins one.
    bool search(const Identity& me, double now);
    const std::vector<Offer>& offers() const { return offers_; }
    void join(const Address& host, const GuestProgress& mine, double now);
    // Reads every waiting packet, sends what is due, ends a silent session.
    void poll(double now);
    // Leaves (BYE to the other side) and closes the socket.
    void close(Reason why = Reason::LEFT);

    // ---- playing: the guest's side
    // The guest's pad for `step` (steps in order, from 0).
    void send_pad(uint32_t step, uint32_t pad);
    // The host's records come in order; one is taken when whole (false when none waits).
    bool take_record(std::vector<uint8_t>& record);

    // ---- playing: the host's side
    // The guest's pad for `step`, if it came.
    bool guest_pad(uint32_t step, uint32_t& pad) const;
    // The next step's record into the stream for the guest.
    void put_record(const std::vector<uint8_t>& record);
    // Seconds since the other side was last heard from (the stall shown while waiting).
    double silent_for(double now) const { return now - heard_; }

private:
    void _send(Type type, const std::vector<uint8_t>& body, const Address& to);
    void _receive(const Address& from, const std::vector<uint8_t>& packet, double now);
    void _end(Reason why);
    // Whether `what` from `from` is new to the log (each step of a connection is logged once).
    bool once(const char* what, const Address& from);

    UdpSocket socket_;
    Phase phase_ = Phase::IDLE;
    bool host_ = false;
    Reason reason_ = Reason::NONE;
    Identity me_;
    Start start_;
    uint32_t session_ = 0;
    Address peer_;
    std::string peer_name_;
    double heard_ = 0, last_search_ = -1, last_welcome_ = -1;
    std::vector<Offer> offers_;
    GuestProgress mine_;
    // The guest: its pads from `pads_first_`; the host: the guest's pads by step.
    uint32_t pads_first_ = 0;
    std::deque<uint32_t> pads_;
    std::map<uint32_t, uint32_t> guest_pads_;
    // The host's stream (records, each a u32 length and its bytes) from `stream_base_`, the part
    // the guest has (acked_); the guest's: the bytes in order and the pieces ahead of them.
    std::vector<uint8_t> stream_;
    uint64_t stream_base_ = 0, acked_ = 0, sent_until_ = 0;
    std::vector<uint8_t> received_;
    uint64_t received_end_ = 0;
    std::map<uint64_t, std::vector<uint8_t>> ahead_;
    std::set<std::string> logged_;
};

}  // namespace t3::net
