#include "net/bytebuffer.h"
#include "net/channel.h"
#include "shared/protocol.h"
#include "shared/usercmd.h"

#include <doctest/doctest.h>

using namespace k;

TEST_CASE("ByteWriter/ByteReader round trip") {
    net::ByteWriter w;
    w.write<uint8_t>(7);
    w.write<int32_t>(-123456);
    w.write(3.5f);
    w.writeString("hello");
    net::ByteReader r(w.data());
    CHECK(r.read<uint8_t>() == 7);
    CHECK(r.read<int32_t>() == -123456);
    CHECK(r.read<float>() == 3.5f);
    CHECK(r.readString() == "hello");
    CHECK(r.atEnd());
    CHECK_FALSE(r.overflowed());
}

TEST_CASE("ByteReader flags overflow instead of reading past the end") {
    uint8_t data[2] = {1, 2};
    net::ByteReader r(data);
    CHECK(r.read<uint32_t>() == 0);
    CHECK(r.overflowed());
    CHECK(r.read<uint8_t>() == 0);
}

TEST_CASE("UserCmd and PlayerState serialize losslessly") {
    UserCmd c;
    c.commandNumber = 99;
    c.tickCount = 1234;
    c.viewAngles = {10, -45, 0};
    c.forwardMove = 450;
    c.sideMove = -450;
    c.buttons = IN_JUMP | IN_ATTACK;
    proto::PlayerState s;
    s.origin = {1, 2, 3};
    s.velocity = {4, 5, 6};

    net::ByteWriter w;
    c.write(w);
    s.write(w);
    net::ByteReader r(w.data());
    CHECK(UserCmd::read(r) == c);
    auto s2 = proto::PlayerState::read(r);
    CHECK(s2.origin == s.origin);
    CHECK(s2.velocity == s.velocity);
    CHECK(r.atEnd());
}

TEST_CASE("Loopback channel delivers messages in order to the other side") {
    auto [client, server] = net::LoopbackChannel::createPair();
    uint8_t a[] = {1}, b[] = {2, 3};
    client->send(a);
    client->send(b);
    CHECK_FALSE(client->receive().has_value());
    auto m1 = server->receive();
    auto m2 = server->receive();
    REQUIRE(m1);
    REQUIRE(m2);
    CHECK(m1->size() == 1);
    CHECK((*m2)[1] == 3);
    CHECK_FALSE(server->receive().has_value());
}
