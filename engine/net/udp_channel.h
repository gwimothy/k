#pragma once
// A Quake-style network channel over UDP.
//
// Every packet: sequence, ack of the peer's latest sequence, ack of the peer's reliable
// stream, then optionally the reliable block, then any number of unreliable messages.
// One reliable block is in flight at a time and is resent in every packet until acked, so
// reliable messages arrive once and in order. Unreliable messages ride along and are just
// dropped with their packet. Out-of-order packets are discarded.
//
// The channel doesn't own the socket: a server shares one socket across all its clients
// and routes incoming packets to channels by address.

#include "net/channel.h"
#include "net/socket.h"

#include <array>
#include <deque>
#include <functional>
#include <vector>

namespace k::net {

constexpr uint32_t kConnectionless = 0xFFFFFFFFu;
constexpr size_t kMaxPacket = 1400;
constexpr double kTimeout = 10.0;

// Out-of-band text packets for the handshake ("connect", "accept", "reject ...").
void sendConnectionless(UdpSocket& socket, const Address& to, const std::string& text);
// If `packet` is connectionless, returns its text.
std::optional<std::string> readConnectionless(const std::vector<uint8_t>& packet);

class UdpChannel final : public NetChannel {
public:
    // `sendFn` actually puts bytes on the wire (normally socket.sendTo(address, ...)).
    using SendFn = std::function<void(std::span<const uint8_t>)>;
    UdpChannel(SendFn sendFn, double now);

    void send(std::span<const uint8_t> message) override;
    void sendReliable(std::span<const uint8_t> message) override;
    std::optional<std::vector<uint8_t>> receive() override;
    void transmit(double now) override;
    NetStats stats() const override { return stats_; }
    bool timedOut(double now) const override { return now - lastReceived_ > kTimeout; }

    // Feed a packet that arrived from the peer.
    void processPacket(const std::vector<uint8_t>& packet, double now);

    uint32_t outgoingSequence() const { return outSeq_; }
    uint32_t incomingSequence() const { return inSeq_; }
    // Simulated network conditions for this channel (also driven by net_fakeloss/net_fakelag).
    float fakeLoss = 0;   // 0..1
    float fakeLagMs = 0;

private:
    void putOnWire(std::vector<uint8_t> packet, double now);

    SendFn sendFn_;
    // Outgoing
    uint32_t outSeq_ = 0;
    std::vector<std::vector<uint8_t>> unreliable_;
    std::deque<std::vector<uint8_t>> reliableQueue_;
    std::vector<uint8_t> reliableInFlight_; // framed messages
    uint32_t reliableInFlightSeq_ = 0;
    uint32_t outReliableSeq_ = 0;
    uint32_t peerReliableAck_ = 0;
    std::array<double, 64> sendTimes_{};
    struct Delayed {
        double release;
        std::vector<uint8_t> data;
    };
    std::deque<Delayed> delayed_;
    // Incoming
    uint32_t inSeq_ = 0;
    uint32_t inReliableSeq_ = 0;
    std::deque<std::vector<uint8_t>> received_;
    double lastReceived_;
    // Stats
    NetStats stats_;
    double statWindowStart_;
    size_t windowIn_ = 0, windowOut_ = 0;
    uint32_t windowPackets_ = 0, windowDropped_ = 0;
};

} // namespace k::net
