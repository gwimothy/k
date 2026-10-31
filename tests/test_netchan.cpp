#include "core/time.h"
#include "net/socket.h"
#include "net/udp_channel.h"
#include "shared/protocol.h"

#include <doctest/doctest.h>

#include <deque>
#include <random>

using namespace k;

namespace {

// Two channels joined by an in-memory link that drops a share of packets.
struct LossyLink {
    std::mt19937 rng{42};
    float loss = 0;
    std::deque<std::vector<uint8_t>> toA, toB;
    std::unique_ptr<net::UdpChannel> a, b;
    LossyLink() {
        a = std::make_unique<net::UdpChannel>([this](std::span<const uint8_t> d) { deliver(toB, d); }, 0.0);
        b = std::make_unique<net::UdpChannel>([this](std::span<const uint8_t> d) { deliver(toA, d); }, 0.0);
    }
    void deliver(std::deque<std::vector<uint8_t>>& q, std::span<const uint8_t> d) {
        if (std::uniform_real_distribution<float>(0, 1)(rng) < loss) return;
        q.emplace_back(d.begin(), d.end());
    }
    void step(double now) {
        a->transmit(now);
        b->transmit(now);
        while (!toB.empty()) { b->processPacket(toB.front(), now); toB.pop_front(); }
        while (!toA.empty()) { a->processPacket(toA.front(), now); toA.pop_front(); }
    }
};

std::vector<uint8_t> msg(int n) { return {static_cast<uint8_t>(n), static_cast<uint8_t>(n >> 8)}; }

} // namespace

TEST_CASE("netchan: reliable messages arrive once and in order through 30% loss") {
    LossyLink link;
    link.loss = 0.3f;
    for (int i = 0; i < 200; ++i) link.a->sendReliable(msg(i));
    std::vector<int> got;
    for (int t = 0; t < 2000 && got.size() < 200; ++t) {
        link.a->send(msg(9999)); // unreliable noise alongside
        link.step(t * 0.015);
        while (auto m = link.b->receive()) {
            int v = (*m)[0] | ((*m)[1] << 8);
            if (v != 9999) got.push_back(v);
        }
    }
    REQUIRE(got.size() == 200);
    for (int i = 0; i < 200; ++i) CHECK(got[static_cast<size_t>(i)] == i);
}

TEST_CASE("netchan: unreliable messages are delivered when packets get through") {
    LossyLink link;
    link.a->send(msg(7));
    link.step(0.0);
    auto m = link.b->receive();
    REQUIRE(m);
    CHECK((*m)[0] == 7);
    CHECK(link.b->incomingSequence() == 1);
}

TEST_CASE("netchan: stats and timeouts") {
    LossyLink link;
    for (int t = 0; t < 150; ++t) link.step(t * 0.01); // past the 1 s stats window
    CHECK_FALSE(link.a->timedOut(1.5));
    CHECK(link.a->timedOut(1.5 + net::kTimeout + 1));
    CHECK(link.a->stats().outBytesPerSec > 0);
}

TEST_CASE("net: UDP sockets on localhost") {
    net::UdpSocket a, b;
    REQUIRE(a.open(0, true));
    REQUIRE(b.open(0, true));
    auto addrB = net::resolveAddress("127.0.0.1:" + std::to_string(b.localPort()), 0);
    REQUIRE(addrB);
    CHECK(addrB->isLoopback());
    net::sendConnectionless(a, *addrB, "hello there");
    net::Address from;
    std::optional<std::vector<uint8_t>> packet;
    double deadline = platformTime() + 2.0;
    while (!packet && platformTime() < deadline) packet = b.receiveFrom(from);
    REQUIRE(packet);
    CHECK(net::readConnectionless(*packet).value_or("") == "hello there");
    CHECK(from.port == a.localPort());
    CHECK_FALSE(net::resolveAddress("1.2.3.4:99999", 1));
}

TEST_CASE("protocol: entity deltas round trip") {
    std::mt19937 rng(7);
    std::vector<proto::EntityState> base;
    for (uint16_t i = 1; i < 40; i += 2) {
        proto::EntityState e;
        e.index = i;
        e.model = static_cast<uint16_t>(i % 5);
        e.origin = {float(i), 0, 0};
        base.push_back(e);
    }
    for (int round = 0; round < 50; ++round) {
        std::vector<proto::EntityState> cur;
        for (auto e : base) {
            int r = static_cast<int>(rng() % 4);
            if (r == 0) continue;                          // removed
            if (r == 1) e.origin.z += 1.0f + round;        // moved
            if (r == 2) e.effects ^= 1;                    // toggled
            cur.push_back(e);
        }
        proto::EntityState added;
        added.index = static_cast<uint16_t>(100 + round);
        added.model = 3;
        cur.push_back(added);
        net::ByteWriter w;
        proto::writeEntityDelta(w, base, cur);
        net::ByteReader r(w.data());
        std::vector<proto::EntityState> out;
        REQUIRE(proto::readEntityDelta(r, base, out));
        REQUIRE(out.size() == cur.size());
        for (size_t i = 0; i < out.size(); ++i) {
            CHECK(out[i].index == cur[i].index);
            CHECK(out[i].origin == cur[i].origin);
            CHECK(out[i].effects == cur[i].effects);
        }
        // An unchanged world costs almost nothing.
        net::ByteWriter same;
        proto::writeEntityDelta(same, cur, cur);
        CHECK(same.size() == 2);
    }
}
