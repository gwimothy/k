#include "audio/audio.h"
#include "core/filesystem.h"
#include "game_world.h"
#include "test_room.h"

#include <doctest/doctest.h>

using namespace k;

TEST_CASE("audio: sound script parsing") {
    auto scripts = parseSoundScripts(R"(
"A.Single" { "wave" "a.wav" "volume" "0.5" "pitch" "90,110" "soundlevel" "80" "loop" "1" }
"B.Random" { "rndwave" { "wave" "b1.wav" "wave" "b2.wav" } }
"C.Broken" { "volume" "1" }
)");
    REQUIRE(scripts.size() == 2); // C has no wave
    CHECK(scripts[0].name == "A.Single");
    CHECK(scripts[0].volume == doctest::Approx(0.5f));
    CHECK(scripts[0].pitchMin == doctest::Approx(90.0f));
    CHECK(scripts[0].pitchMax == doctest::Approx(110.0f));
    CHECK(scripts[0].loop);
    CHECK(scripts[1].waves.size() == 2);
    CHECK(soundLevelToDistance(60) == doctest::Approx(500.0f));
    CHECK(soundLevelToDistance(74) == doctest::Approx(2000.0f));
    CHECK(soundLevelToDistance(0) == 0.0f);
}

TEST_CASE("audio: the shipped sound scripts all point at real files") {
    fs::clearSearchPaths();
    fs::addSearchPath(K_SOURCE_DIR "/base", "GAME");
    auto text = fs::readText("scripts/sounds.txt");
    REQUIRE(text);
    auto scripts = parseSoundScripts(*text);
    CHECK(scripts.size() >= 8);
    for (auto& s : scripts)
        for (auto& w : s.waves) CHECK_MESSAGE(fs::exists("sound/" + w), s.name << " -> " << w);

    // Playback works without an audio device (CI): loops keep playing until stopped.
    AudioSystem audio;
    REQUIRE(audio.init(false));
    CHECK(audio.play("Door.Move", {0, 0, 0}, 5));
    CHECK(audio.play("Door.Move", {0, 0, 0}, 5)); // replaces the loop on the same entity
    CHECK(audio.activeCount() == 1);
    CHECK_FALSE(audio.play("No.Such.Sound", {0, 0, 0}));
    audio.stop("Door.Move", 5);
    CHECK(audio.activeCount() == 0);
    fs::clearSearchPaths();
}

TEST_CASE("audio: the game emits footsteps, jumps and door sounds") {
    std::vector<std::string> sounds;
    game::EngineServices services;
    services.emitSound = [&](const std::string& name, int, const Vec3&, float, bool stop) {
        sounds.push_back(stop ? "stop:" + name : name);
    };
    game::GameWorld world(services);
    world.levelInit("test", std::make_shared<const bsp::BspData>(k::test::testRoom()));
    world.clientConnect(0, "tester");
    world.setPlayerOrigin(0, {0, -150, 0});
    UserCmd walk;
    walk.forwardMove = 450;
    for (int i = 0; i < 66; ++i) {
        world.runCommand(0, walk, 1.0f / 66);
        world.frame(1.0f / 66, 0);
    }
    auto count = [&](const std::string& n) { return std::count(sounds.begin(), sounds.end(), n); };
    CHECK(count("Player.Footstep") >= 2);
    UserCmd jump;
    jump.buttons = IN_JUMP;
    for (int i = 0; i < 70; ++i) {
        world.runCommand(0, jump, 1.0f / 66);
        world.frame(1.0f / 66, 0);
    }
    CHECK(count("Player.Jump") == 1);
    world.deliverInput("door1", "Open", "", nullptr, nullptr);
    for (int i = 0; i < 66; ++i) world.frame(1.0f / 66, 0);
    CHECK(count("Door.Move") == 1);
    CHECK(count("stop:Door.Move") == 1);
    CHECK(count("Door.Stop") == 1);
}
