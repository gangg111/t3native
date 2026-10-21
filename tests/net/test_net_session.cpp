// Native: NETWORK MODE's connection (net/net_session.h) between two sessions of this process over
// the loopback: the guest finds the host, joins with its own characters, the start comes back, the
// guest's pads reach the host and the host's records (one larger than a packet) reach the guest in
// order; a different build is refused; BYE ends the other side.
#include <vector>

#include "net/net_session.h"
#include "support/test_suite.h"

using namespace t3::net;

namespace {

// Both sessions polled at 10 ms steps of a made-up clock until `done` (or `limit` seconds).
template <typename Done>
bool run(NetSession& a, NetSession& b, double& now, double limit, Done done) {
    for (double end = now + limit; now < end; now += 0.01) {
        a.poll(now);
        b.poll(now);
        if (done()) return true;
    }
    return false;
}

NetSession::Identity identity(const std::string& build, const std::string& name) {
    NetSession::Identity id;
    id.build = build;
    id.data = 0x1234;
    id.name = name;
    return id;
}

}  // namespace

T3_TEST(net_session, join_pads_and_records) {
    NetSession host, guest;
    double now = 0;
    NetSession::Start start;
    start.seed = 1234;              // source: chosen for the test
    start.movies = true;
    start.delay = 2;
    start.progress = {1, 2, 3};
    if (!t.expect(host.host(identity("b1", "HOST"), start, now), "host on the port")) return;
    t.expect(guest.search(identity("b1", "GUEST"), now), "search socket");
    t.expect(run(host, guest, now, 2.0, [&] { return !guest.offers().empty(); }), "the host is found");
    if (guest.offers().empty()) return;
    t.expect_equal(guest.offers()[0].name, std::string("HOST"), "offer name");
    t.expect_equal(static_cast<int>(guest.offers().size()), 1, "one game, however many addresses answered");

    guest.join(guest.offers()[0].address, NetSession::GuestProgress{0x5, 0x2}, now);
    t.expect(run(host, guest, now, 2.0,
                 [&] { return host.phase() == NetSession::Phase::PLAYING && guest.phase() == NetSession::Phase::PLAYING; }),
             "both playing");
    // source: the values set above, carried both ways
    t.expect_equal(static_cast<long long>(guest.start_info().seed), 1234LL, "seed");
    t.expect(guest.start_info().progress == std::vector<uint8_t>{1, 2, 3}, "progress bytes");
    t.expect(guest.start_info().movies, "movies");
    t.expect_equal(static_cast<int>(guest.start_info().delay), 2, "delay");
    t.expect_equal(static_cast<long long>(host.start_info().guest.unlocked), 5LL, "guest characters at the host");
    t.expect_equal(static_cast<long long>(guest.start_info().guest.costumes), 2LL, "guest costumes back");
    t.expect_equal(host.peer_name(), std::string("GUEST"), "host sees the guest's name");
    t.expect_equal(guest.peer_name(), std::string("HOST"), "guest sees the host's name");

    // The guest's pads for steps 2-11 (the first two steps are the delay's, without pads): 100 + the
    // step (source: chosen for the test).
    for (uint32_t s = 2; s < 12; ++s) guest.send_pad(s, 100 + s);
    uint32_t pad = 0;
    t.expect(run(host, guest, now, 1.0, [&] { return host.guest_pad(11, pad); }), "pads arrive");
    for (uint32_t s = 2; s < 12; ++s) {
        uint32_t got = 0;
        t.expect(host.guest_pad(s, got) && got == 100 + s, "pad of step " + std::to_string(s));
    }
    t.expect(!host.guest_pad(0, pad), "no pad for a delay step");

    // 40 records, record i = i bytes of value i, then one of 3000 bytes (3 packets' worth).
    std::vector<std::vector<uint8_t>> sent;
    for (int i = 0; i < 40; ++i) sent.push_back(std::vector<uint8_t>(static_cast<size_t>(i), static_cast<uint8_t>(i)));
    std::vector<uint8_t> big(3000);
    for (size_t i = 0; i < big.size(); ++i) big[i] = static_cast<uint8_t>(i * 7);
    sent.push_back(big);
    for (const auto& r : sent) host.put_record(r);
    std::vector<std::vector<uint8_t>> got_records;
    run(host, guest, now, 2.0, [&] {
        std::vector<uint8_t> r;
        while (guest.take_record(r)) got_records.push_back(r);
        return got_records.size() == sent.size();
    });
    t.expect_equal(static_cast<int>(got_records.size()), static_cast<int>(sent.size()), "records taken");
    t.expect(got_records == sent, "records whole and in order");

    guest.close();
    t.expect(run(host, guest, now, 1.0, [&] { return host.phase() == NetSession::Phase::ENDED; }), "BYE ends the host's session");
    t.expect(host.reason() == NetSession::Reason::LEFT, "the guest left");
    host.close();
}

T3_TEST(net_session, another_build_is_refused) {
    NetSession host, guest;
    double now = 0;
    if (!t.expect(host.host(identity("b1", "HOST"), NetSession::Start{}, now), "host on the port")) return;
    guest.search(identity("b2", "GUEST"), now);
    run(host, guest, now, 2.0, [&] { return !guest.offers().empty(); });
    if (!t.expect(!guest.offers().empty(), "the host is found")) return;
    guest.join(guest.offers()[0].address, NetSession::GuestProgress{}, now);
    t.expect(run(host, guest, now, 2.0, [&] { return guest.phase() == NetSession::Phase::ENDED; }), "refused");
    t.expect(guest.reason() == NetSession::Reason::VERSION, "for the build");
    t.expect(host.phase() == NetSession::Phase::HOSTING, "the host still waits");
    host.close();
}

T3_TEST(net_session, silence_ends_the_session) {
    NetSession host, guest;
    double now = 0;
    if (!t.expect(host.host(identity("b1", "HOST"), NetSession::Start{}, now), "host on the port")) return;
    guest.search(identity("b1", "GUEST"), now);
    run(host, guest, now, 2.0, [&] { return !guest.offers().empty(); });
    if (guest.offers().empty()) return;
    guest.join(guest.offers()[0].address, NetSession::GuestProgress{}, now);
    run(host, guest, now, 2.0, [&] { return host.phase() == NetSession::Phase::PLAYING && guest.phase() == NetSession::Phase::PLAYING; });
    // The guest goes quiet (never polled again); the host waits TIMEOUT (15 s, source: NetSession::TIMEOUT).
    double quiet = now;
    while (host.phase() == NetSession::Phase::PLAYING && now < quiet + 20) {
        host.poll(now);
        now += 0.1;
    }
    t.expect(host.reason() == NetSession::Reason::TIMEOUT, "timed out");
    t.expect(now - quiet >= 15.0 && now - quiet < 15.5, "after 15 s");
}
