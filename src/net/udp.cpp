#include "net/udp.h"

#include <cstdio>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace t3::net {

namespace {

#ifdef _WIN32
using Fd = SOCKET;
bool winsock_ready() {
    static bool ready = [] {
        WSADATA data;
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return ready;
}
#else
using Fd = int;
#endif

sockaddr_in to_sockaddr(const Address& a) {
    sockaddr_in s{};
    s.sin_family = AF_INET;
    s.sin_port = htons(a.port);
    s.sin_addr.s_addr = htonl(a.ip);
    return s;
}

// The subnet broadcast of an address with this mask (host byte order).
uint32_t broadcast_of(uint32_t ip, uint32_t mask) { return (ip & mask) | ~mask; }

}  // namespace

std::string Address::text(bool with_port) const {
    char b[32];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u", (ip >> 24) & 0xFF, (ip >> 16) & 0xFF, (ip >> 8) & 0xFF, ip & 0xFF);
    std::string s = b;
    if (with_port) s += ":" + std::to_string(port);
    return s;
}

bool Address::parse(const std::string& s, uint16_t port, Address& out) {
    unsigned a = 0, b = 0, c = 0, d = 0;
    char tail = 0;
    if (std::sscanf(s.c_str(), "%u.%u.%u.%u%c", &a, &b, &c, &d, &tail) != 4) return false;
    if (a > 255 || b > 255 || c > 255 || d > 255) return false;
    out.ip = (a << 24) | (b << 16) | (c << 8) | d;
    out.port = port;
    return out.ip != 0;
}

bool UdpSocket::open(uint16_t port) {
    close();
#ifdef _WIN32
    if (!winsock_ready()) return false;
#endif
    Fd fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (fd == INVALID_SOCKET) return false;
    u_long nonblocking = 1;
    ioctlsocket(fd, FIONBIO, &nonblocking);
    BOOL yes = TRUE;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&yes), sizeof yes);
    // A datagram to a closed port must not break the socket's next reads (Windows reports it there).
    DWORD no_reset = FALSE, unused = 0;
    WSAIoctl(fd, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &no_reset, sizeof no_reset, nullptr, 0, &unused, nullptr, nullptr);
#else
    if (fd < 0) return false;
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof yes);
#endif
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_port = htons(port);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof local) != 0) {
#ifdef _WIN32
        closesocket(fd);
#else
        ::close(fd);
#endif
        return false;
    }
    socklen_t len = sizeof local;
    getsockname(fd, reinterpret_cast<sockaddr*>(&local), &len);
    port_ = ntohs(local.sin_port);
    fd_ = static_cast<long long>(fd);
    return true;
}

void UdpSocket::close() {
    if (fd_ < 0) return;
#ifdef _WIN32
    closesocket(static_cast<Fd>(fd_));
#else
    ::close(static_cast<Fd>(fd_));
#endif
    fd_ = -1;
    port_ = 0;
}

bool UdpSocket::send(const Address& to, const uint8_t* data, size_t size) {
    if (fd_ < 0 || !to.valid()) return false;
    sockaddr_in s = to_sockaddr(to);
    auto sent = sendto(static_cast<Fd>(fd_), reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
                       reinterpret_cast<sockaddr*>(&s), sizeof s);
    return sent == static_cast<decltype(sent)>(size);
}

bool UdpSocket::receive(Address& from, std::vector<uint8_t>& out) {
    if (fd_ < 0) return false;
    uint8_t buffer[2048];
    sockaddr_in s{};
    socklen_t len = sizeof s;
    auto got = recvfrom(static_cast<Fd>(fd_), reinterpret_cast<char*>(buffer), sizeof buffer, 0, reinterpret_cast<sockaddr*>(&s), &len);
    if (got <= 0) return false;
    from.ip = ntohl(s.sin_addr.s_addr);
    from.port = ntohs(s.sin_port);
    out.assign(buffer, buffer + got);
    return true;
}

std::vector<Address> local_addresses() {
    std::vector<Address> found;
#ifdef _WIN32
    if (!winsock_ready()) return found;
    ULONG size = 16 * 1024;
    std::vector<uint8_t> buffer(size);
    auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
    if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, list, &size) !=
        NO_ERROR) {
        return found;
    }
    for (IP_ADAPTER_ADDRESSES* a = list; a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
            auto* s = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
            if (s->sin_family != AF_INET) continue;
            uint32_t ip = ntohl(s->sin_addr.s_addr);
            if ((ip >> 24) == 127 || (ip >> 16) == 0xA9FE) continue;   // loopback, link-local (no DHCP)
            found.push_back(Address{ip, 0});
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return found;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK)) continue;
        uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr);
        if ((ip >> 16) == 0xA9FE) continue;
        // The mobile data's interface is not a local network (no broadcasts there).
        if (!(a->ifa_flags & IFF_BROADCAST)) continue;
        found.push_back(Address{ip, 0});
    }
    freeifaddrs(list);
#endif
    return found;
}

std::vector<Address> broadcast_targets(uint16_t port) {
    std::vector<Address> targets;
    auto add = [&](uint32_t ip) {
        Address a{ip, port};
        for (const Address& t : targets)
            if (t == a) return;
        targets.push_back(a);
    };
#ifdef _WIN32
    if (winsock_ready()) {
        ULONG size = 16 * 1024;
        std::vector<uint8_t> buffer(size);
        auto* list = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buffer.data());
        if (GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, list, &size) ==
            NO_ERROR) {
            for (IP_ADAPTER_ADDRESSES* a = list; a; a = a->Next) {
                if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;
                for (IP_ADAPTER_UNICAST_ADDRESS* u = a->FirstUnicastAddress; u; u = u->Next) {
                    auto* s = reinterpret_cast<sockaddr_in*>(u->Address.lpSockaddr);
                    if (s->sin_family != AF_INET || u->OnLinkPrefixLength == 0 || u->OnLinkPrefixLength > 30) continue;
                    uint32_t mask = 0xFFFFFFFFu << (32 - u->OnLinkPrefixLength);
                    add(broadcast_of(ntohl(s->sin_addr.s_addr), mask));
                }
            }
        }
    }
#else
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) == 0) {
        for (ifaddrs* a = list; a; a = a->ifa_next) {
            if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET || !a->ifa_netmask) continue;
            if (!(a->ifa_flags & IFF_UP) || (a->ifa_flags & IFF_LOOPBACK) || !(a->ifa_flags & IFF_BROADCAST)) continue;
            uint32_t ip = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_addr)->sin_addr.s_addr);
            uint32_t mask = ntohl(reinterpret_cast<sockaddr_in*>(a->ifa_netmask)->sin_addr.s_addr);
            add(broadcast_of(ip, mask));
        }
        freeifaddrs(list);
    }
#endif
    add(0xFFFFFFFFu);
    add(0x7F000001u);
    return targets;
}

}  // namespace t3::net
