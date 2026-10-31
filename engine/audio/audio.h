#pragma once
// Client audio (miniaudio): 3D positional sounds driven by sound scripts.
//
// Sounds are played by script name ("Door.Move"), never by file, so the game can be
// re-voiced without code changes. scripts/sounds.txt maps names to files, volume, pitch
// range, how far they carry, and looping.
//
// If there is no audio device (servers, CI), mixing still runs without output.

#include "shared/kmath.h"

#include <memory>
#include <string>
#include <vector>

namespace k {

struct SoundScript {
    std::string name;
    std::vector<std::string> waves; // relative to sound/
    float volume = 1.0f;
    float pitchMin = 100, pitchMax = 100;
    float soundLevel = 75;          // dB; 0 = no attenuation
    bool loop = false;
};

class AudioSystem {
public:
    AudioSystem();
    ~AudioSystem();
    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;

    // useDevice = false mixes without opening an output device (headless runs, -nosound).
    bool init(bool useDevice = true);
    void shutdown();
    bool hasDevice() const;

    // Parse scripts/sounds.txt (can be called again to reload).
    void loadScripts();
    const SoundScript* findScript(const std::string& name) const;

    // Start a sound. `entity` > 0 attaches it so it can follow the entity and be stopped.
    // Returns false if the script or file is missing.
    bool play(const std::string& name, const Vec3& origin, int entity = 0, float volume = 1.0f);
    void stop(const std::string& name, int entity);
    void stopAll();

    // Per frame: listener pose, move entity-attached sounds, free finished ones.
    void update(const Vec3& listenerPos, const QAngle& listenerAngles,
                const std::vector<std::pair<int, Vec3>>& entityOrigins);

    size_t activeCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// dB sound level -> distance (units) at which a sound fades out.
float soundLevelToDistance(float soundLevel);
// Parse one script file's text (exposed for tests).
std::vector<SoundScript> parseSoundScripts(const std::string& text);

} // namespace k
