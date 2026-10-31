#include "net/udp_channel.h"

#include "core/cvar.h"
#include "core/log.h"
#include "net/bytebuffer.h"

#include <algorithm>
#include <random>

namespace k::net {
namespace {

ConVar net_fakeloss("net_fakeloss", "0", FCVAR_CHEAT, "Drop this percentage of outgoing packets (testing).", 0.0f, 100.0f);
ConVar net_fakelag("net_fakelag", "0", FCVAR_CHEAT, "Delay outgoing packets by this many ms (testing).", 0.0f, 1000.0f);

constexpr uint32_t kReliableBit = 0x80000000u;
constexpr size_t kMaxReliableBlock = 1000;

std::mt19937& rng() {
    thread_local std::mt19937 r{std::random_device{}()};
    return r;
}

} // namespace

void sendConnectionless(UdpSocket& socket, const Address& to, const std::string& text) {
    ByteWriter w;
    w.write(kConnectionless);
    w.writeString(text);
    socket.sendTo(to, w.data());
}

std::optional<std::string> readConnectionless(const std::vector<uint8_t>& packet) {
    ByteReader r(packet);
    if (r.read<uint32_t>() != kConnectionless) return std::nullopt;
    auto text = r.readString();
    if (r.overflowed()) return std::nullopt;
    return text;
}

UdpChannel::UdpChannel(SendFn sendFn, double now)
    : sendFn_(std::move(sendFn)), lastReceived_(now), statWindowStart_(now) {}

void UdpChannel::send(std::span<const uint8_t> message) { unreliable_.emplace_back(message.begin(), message.end()); }

void UdpChannel::sendReliable(std::span<const uint8_t> message) {
    reliableQueue_.emplace_back(message.begin(), message.end());
}

std::optional<std::vector<uint8_t>> UdpChannel::receive() {
    if (received_.empty()) return std::nullopt;
    auto m = std::move(received_.front());
    received_.pop_front();
    return m;
}

void UdpChannel::putOnWire(std::vector<uint8_t> packet, double now) {
    float loss = std::max(fakeLoss, net_fakeloss.getFloat() / 100.0f);
    float lag = std::max(fakeLagMs, net_fakelag.getFloat());
    windowOut_ += packet.size();
    if (loss > 0 && std::uniform_real_distribution<float>(0, 1)(rng()) < loss) return;
    if (lag > 0) {
        delayed_.push_back({now + lag / 1000.0, std::move(packet)});
        return;
    }
    sendFn_(packet);
}

void UdpChannel::transmit(double now) {
    // Release delayed packets whose time has come.
    while (!delayed_.empty() && delayed_.front().release <= now) {
        sendFn_(delayed_.front().data);
        delayed_.pop_front();
    }

    // Start a new reliable block when the previous one has been acknowledged.
    if (reliableInFlight_.empty() && !reliableQueue_.empty()) {
        ByteWriter block;
        while (!reliableQueue_.empty() && block.size() + reliableQueue_.front().size() + 2 <= kMaxReliableBlock) {
            block.write(static_cast<uint16_t>(reliableQueue_.front().size()));
            block.writeBytes(reliableQueue_.front());
            reliableQueue_.pop_front();
        }
        if (block.size() == 0 && !reliableQueue_.empty()) {
            log::warn("net: reliable message of {} bytes is too big, dropped", reliableQueue_.front().size());
            reliableQueue_.pop_front();
        }
        if (block.size() > 0) {
            reliableInFlight_ = block.take();
            reliableInFlightSeq_ = ++outReliableSeq_;
        }
    }

    ByteWriter w;
    uint32_t seq = ++outSeq_;
    bool withReliable = !reliableInFlight_.empty();
    w.write(seq | (withReliable ? kReliableBit : 0u));
    w.write(inSeq_);
    w.write(inReliableSeq_);
    if (withReliable) {
        w.write(reliableInFlightSeq_);
        w.write(static_cast<uint16_t>(reliableInFlight_.size()));
        w.writeBytes(reliableInFlight_);
    }
    for (auto& m : unreliable_) {
        if (w.size() + m.size() + 2 > kMaxPacket) {
            log::warn("net: packet full, dropping a {} byte unreliable message", m.size());
            continue;
        }
        w.write(static_cast<uint16_t>(m.size()));
        w.writeBytes(m);
    }
    unreliable_.clear();
    sendTimes_[seq % sendTimes_.size()] = now;
    putOnWire(w.take(), now);

    // Roll the stats window every second.
    double span = now - statWindowStart_;
    if (span >= 1.0) {
        stats_.inBytesPerSec = static_cast<float>(windowIn_ / span);
        stats_.outBytesPerSec = static_cast<float>(windowOut_ / span);
        if (windowPackets_ + windowDropped_ > 0)
            stats_.lossPercent = 100.0f * static_cast<float>(windowDropped_) / static_cast<float>(windowPackets_ + windowDropped_);
        windowIn_ = windowOut_ = 0;
        windowPackets_ = windowDropped_ = 0;
        statWindowStart_ = now;
    }
}

void UdpChannel::processPacket(const std::vector<uint8_t>& packet, double now) {
    ByteReader r(packet);
    uint32_t seqField = r.read<uint32_t>();
    uint32_t ack = r.read<uint32_t>();
    uint32_t reliableAck = r.read<uint32_t>();
    if (r.overflowed() || seqField == kConnectionless) return;
    uint32_t seq = seqField & ~kReliableBit;
    if (seq <= inSeq_) return; // duplicate or out of order
    if (seq > inSeq_ + 1) windowDropped_ += seq - inSeq_ - 1;
    ++windowPackets_;
    windowIn_ += packet.size();
    inSeq_ = seq;
    lastReceived_ = now;

    // Round trip time from the ack.
    if (ack > 0 && ack <= outSeq_ && outSeq_ - ack < sendTimes_.size()) {
        float rtt = static_cast<float>((now - sendTimes_[ack % sendTimes_.size()]) * 1000.0);
        stats_.rttMs = stats_.rttMs == 0 ? rtt : stats_.rttMs * 0.9f + rtt * 0.1f;
    }
    // The peer has our reliable block: free it.
    if (reliableAck >= reliableInFlightSeq_ && reliableInFlightSeq_ != 0) {
        reliableInFlight_.clear();
        reliableInFlightSeq_ = 0;
    }
    peerReliableAck_ = reliableAck;

    auto readMessages = [&](ByteReader& br, std::deque<std::vector<uint8_t>>& out, size_t limit) {
        size_t start = br.remaining();
        while (start - br.remaining() < limit && br.remaining() >= 2) {
            uint16_t len = br.read<uint16_t>();
            if (br.remaining() < len) return false;
            std::vector<uint8_t> m(len);
            for (auto& b : m) b = br.read<uint8_t>();
            out.push_back(std::move(m));
        }
        return true;
    };

    if (seqField & kReliableBit) {
        uint32_t rseq = r.read<uint32_t>();
        uint16_t len = r.read<uint16_t>();
        if (r.overflowed() || r.remaining() < len) return;
        std::vector<uint8_t> block(len);
        for (auto& b : block) b = r.read<uint8_t>();
        if (rseq == inReliableSeq_ + 1) {
            inReliableSeq_ = rseq;
            ByteReader br(block);
            readMessages(br, received_, block.size());
        }
    }
    readMessages(r, received_, r.remaining());
}

} // namespace k::net
