#include "net/socket.h"

#include "core/log.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <cstring>

namespace k::net {
namespace {

#ifdef _WIN32
struct WinsockInit {
    WinsockInit() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
    ~WinsockInit() { WSACleanup(); }
};
void ensureInit() { static WinsockInit init; }
void closeHandle(intptr_t h) { closesocket(static_cast<SOCKET>(h)); }
#else
void ensureInit() {}
void closeHandle(intptr_t h) { ::close(static_cast<int>(h)); }
#endif

sockaddr_in toSockaddr(const Address& a) {
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(a.ip);
    sa.sin_port = htons(a.port);
    return sa;
}

} // namespace

std::string Address::toString() const {
    return std::to_string((ip >> 24) & 255) + "." + std::to_string((ip >> 16) & 255) + "." +
           std::to_string((ip >> 8) & 255) + "." + std::to_string(ip & 255) + ":" + std::to_string(port);
}

std::optional<Address> resolveAddress(const std::string& text, uint16_t defaultPort) {
    ensureInit();
    std::string host = text;
    uint16_t port = defaultPort;
    if (auto colon = text.rfind(':'); colon != std::string::npos) {
        host = text.substr(0, colon);
        int p = std::atoi(text.c_str() + colon + 1);
        if (p <= 0 || p > 65535) return std::nullopt;
        port = static_cast<uint16_t>(p);
    }
    if (host.empty() || host == "localhost") host = "127.0.0.1";
    addrinfo hints{}, *result = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || !result) return std::nullopt;
    Address a;
    a.ip = ntohl(reinterpret_cast<sockaddr_in*>(result->ai_addr)->sin_addr.s_addr);
    a.port = port;
    freeaddrinfo(result);
    return a;
}

UdpSocket::~UdpSocket() { close(); }

bool UdpSocket::open(uint16_t port, bool loopbackOnly) {
    ensureInit();
    close();
    auto s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) {
#else
    if (s < 0) {
#endif
        log::error("net: couldn't create a UDP socket");
        return false;
    }
    handle_ = static_cast<intptr_t>(s);
    sockaddr_in sa{};
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(loopbackOnly ? INADDR_LOOPBACK : INADDR_ANY);
    sa.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
        log::warn("net: couldn't bind UDP port {} (in use?)", port);
        close();
        return false;
    }
#ifdef _WIN32
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
#else
    fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK);
#endif
    return true;
}

void UdpSocket::close() {
    if (handle_ != kInvalid) closeHandle(handle_);
    handle_ = kInvalid;
}

uint16_t UdpSocket::localPort() const {
    if (handle_ == kInvalid) return 0;
    sockaddr_in sa{};
    socklen_t len = sizeof(sa);
    getsockname(static_cast<decltype(::socket(0, 0, 0))>(handle_), reinterpret_cast<sockaddr*>(&sa), &len);
    return ntohs(sa.sin_port);
}

bool UdpSocket::sendTo(const Address& to, std::span<const uint8_t> data) {
    if (handle_ == kInvalid) return false;
    sockaddr_in sa = toSockaddr(to);
    auto sent = ::sendto(static_cast<decltype(::socket(0, 0, 0))>(handle_), reinterpret_cast<const char*>(data.data()),
                         static_cast<int>(data.size()), 0, reinterpret_cast<sockaddr*>(&sa), sizeof(sa));
    return sent == static_cast<decltype(sent)>(data.size());
}

std::optional<std::vector<uint8_t>> UdpSocket::receiveFrom(Address& from) {
    if (handle_ == kInvalid) return std::nullopt;
    uint8_t buf[2048];
    sockaddr_in sa{};
    socklen_t len = sizeof(sa);
    auto n = ::recvfrom(static_cast<decltype(::socket(0, 0, 0))>(handle_), reinterpret_cast<char*>(buf), sizeof(buf), 0,
                        reinterpret_cast<sockaddr*>(&sa), &len);
    if (n <= 0) return std::nullopt;
    from.ip = ntohl(sa.sin_addr.s_addr);
    from.port = ntohs(sa.sin_port);
    return std::vector<uint8_t>(buf, buf + n);
}

} // namespace k::net
