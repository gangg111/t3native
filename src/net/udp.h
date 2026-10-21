#pragma once
// Native: UDP for the local network play (net/net_session.h): a non-blocking IPv4 socket read
// once a frame, the local network's addresses and where a search for games is broadcast.
// POSIX sockets on Android, Winsock on the development host.
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace t3::net {

struct Address {
    uint32_t ip = 0;     // host byte order
    uint16_t port = 0;
    bool operator==(const Address& o) const { return ip == o.ip && port == o.port; }
    bool operator!=(const Address& o) const { return !(*this == o); }
    bool valid() const { return ip != 0 && port != 0; }
    // "192.168.1.23" (and ":port" with `with_port`).
    std::string text(bool with_port = false) const;
    // A dotted address ("192.168.1.23"); false when it is not one.
    static bool parse(const std::string& s, uint16_t port, Address& out);
    static Address loopback(uint16_t port) { return Address{0x7F000001u, port}; }
};

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket() { close(); }
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    // Bound to `port` on every interface (0: one the system picks), broadcasts allowed. False (and
    // closed) when the port is taken or there are no sockets.
    bool open(uint16_t port);
    void close();
    bool is_open() const { return fd_ >= 0; }
    uint16_t port() const { return port_; }
    bool send(const Address& to, const uint8_t* data, size_t size);
    bool send(const Address& to, const std::vector<uint8_t>& data) { return send(to, data.data(), data.size()); }
    // One waiting datagram into `out` (its sender in `from`); false when none waits.
    bool receive(Address& from, std::vector<uint8_t>& out);

private:
    long long fd_ = -1;
    uint16_t port_ = 0;
};

// This device's IPv4 addresses on a local network (not the loopback), the first the one shown.
std::vector<Address> local_addresses();
// Whether the device is on a local network: Wi-Fi joined or its own hotspot on.
inline bool lan_available() { return !local_addresses().empty(); }
// Where a search is sent: each interface's subnet broadcast, the limited broadcast and the loopback
// (two copies of the game on one computer), all at `port`.
std::vector<Address> broadcast_targets(uint16_t port);

}  // namespace t3::net
