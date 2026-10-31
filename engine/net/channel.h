#pragma once
// A NetChannel moves whole messages between client and server.
//
// Like Source, single-player still runs a local server: the client and server in the same
// process talk through a LoopbackChannel pair. A UDP implementation slots in behind the
// same interface in Phase 7.

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace k::net {

class NetChannel {
public:
    virtual ~NetChannel() = default;
    virtual void send(std::span<const uint8_t> message) = 0;
    virtual std::optional<std::vector<uint8_t>> receive() = 0;
};

// Two queues shared by both ends of a loopback connection.
struct LoopbackPipe {
    std::deque<std::vector<uint8_t>> toServer;
    std::deque<std::vector<uint8_t>> toClient;
};

class LoopbackChannel final : public NetChannel {
public:
    enum class Side { Client, Server };

    LoopbackChannel(std::shared_ptr<LoopbackPipe> pipe, Side side) : pipe_(std::move(pipe)), side_(side) {}

    void send(std::span<const uint8_t> message) override {
        outgoing().emplace_back(message.begin(), message.end());
    }

    std::optional<std::vector<uint8_t>> receive() override {
        auto& q = incoming();
        if (q.empty()) return std::nullopt;
        auto msg = std::move(q.front());
        q.pop_front();
        return msg;
    }

    // Creates a connected client/server pair.
    static std::pair<std::unique_ptr<LoopbackChannel>, std::unique_ptr<LoopbackChannel>> createPair() {
        auto pipe = std::make_shared<LoopbackPipe>();
        return {std::make_unique<LoopbackChannel>(pipe, Side::Client),
                std::make_unique<LoopbackChannel>(pipe, Side::Server)};
    }

private:
    std::deque<std::vector<uint8_t>>& outgoing() { return side_ == Side::Client ? pipe_->toServer : pipe_->toClient; }
    std::deque<std::vector<uint8_t>>& incoming() { return side_ == Side::Client ? pipe_->toClient : pipe_->toServer; }

    std::shared_ptr<LoopbackPipe> pipe_;
    Side side_;
};

} // namespace k::net
