#pragma once
// Minimal non-blocking UDP sockets (POSIX and Winsock) and IPv4 addresses.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace k::net {

struct Address {
    uint32_t ip = 0;   // host byte order
    uint16_t port = 0; // host byte order
    bool operator==(const Address&) const = default;
    std::string toString() const;
    bool isLoopback() const { return (ip >> 24) == 127; }
};

// "1.2.3.4", "1.2.3.4:27015", "localhost:27015", "myhost". Uses defaultPort if none given.
std::optional<Address> resolveAddress(const std::string& text, uint16_t defaultPort);

class UdpSocket {
public:
    UdpSocket() = default;
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    UdpSocket(UdpSocket&& o) noexcept : handle_(o.handle_) { o.handle_ = kInvalid; }
    ~UdpSocket();

    // Port 0 picks any free port. Returns false (and logs) on failure.
    bool open(uint16_t port = 0, bool loopbackOnly = false);
    void close();
    bool isOpen() const { return handle_ != kInvalid; }
    uint16_t localPort() const;

    bool sendTo(const Address& to, std::span<const uint8_t> data);
    // Non-blocking: returns nullopt when there's nothing to read.
    std::optional<std::vector<uint8_t>> receiveFrom(Address& from);

private:
    static constexpr intptr_t kInvalid = -1;
    intptr_t handle_ = kInvalid;
};

} // namespace k::net
