#include "audio/audio.h"

#include "core/cvar.h"
#include "core/filesystem.h"
#include "core/keyvalues.h"
#include "core/log.h"

#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <list>
#include <random>
#include <unordered_map>

namespace k {
namespace {

ConVar volume("volume", "0.8", FCVAR_ARCHIVE, "Master volume (0-1).", 0.0f, 1.0f);
ConVar snd_mute("snd_mute", "0", FCVAR_ARCHIVE, "Mute all sound.");
ConVar snd_show("snd_show", "0", FCVAR_NONE, "Log every sound as it plays.");

} // namespace

float soundLevelToDistance(float soundLevel) {
    if (soundLevel <= 0) return 0; // everywhere
    // 60 dB carries ~500 units, every +7 dB doubles it (close to Source's falloff feel).
    return 500.0f * std::pow(2.0f, (soundLevel - 60.0f) / 7.0f);
}

std::vector<SoundScript> parseSoundScripts(const std::string& text) {
    std::vector<SoundScript> out;
    std::string error;
    auto kv = KeyValues::parse(text, &error);
    if (!kv) {
        log::warn("sounds.txt: {}", error);
        return out;
    }
    for (auto& entry : kv->children()) {
        if (!entry.isSection()) continue;
        SoundScript s;
        s.name = entry.name();
        s.volume = entry.getFloat("volume", 1.0f);
        std::string pitch = entry.getString("pitch", "100");
        float a = 100, b = 100;
        int n = std::sscanf(pitch.c_str(), "%f,%f", &a, &b);
        s.pitchMin = a;
        s.pitchMax = n == 2 ? b : a;
        s.soundLevel = entry.getFloat("soundlevel", 75);
        s.loop = entry.getBool("loop");
        if (auto* w = entry.find("wave"); w && !w->isSection()) s.waves.push_back(w->value());
        if (auto* r = entry.find("rndwave"))
            for (auto& w : r->children())
                if (!w.isSection()) s.waves.push_back(w.value());
        if (s.waves.empty()) {
            log::warn("sounds.txt: '{}' has no wave", s.name);
            continue;
        }
        out.push_back(std::move(s));
    }
    return out;
}

struct AudioSystem::Impl {
    ma_engine engine{};
    bool initialized = false;
    bool device = false;
    std::unordered_map<std::string, SoundScript> scripts;
    struct Playing {
        std::unique_ptr<ma_sound> sound;
        std::string name;
        int entity = 0;
    };
    std::list<Playing> playing;
    std::mt19937 rng{1234};
};

AudioSystem::AudioSystem() : impl_(std::make_unique<Impl>()) {}
AudioSystem::~AudioSystem() { shutdown(); }

bool AudioSystem::init(bool useDevice) {
    ma_engine_config cfg = ma_engine_config_init();
    if (useDevice && ma_engine_init(&cfg, &impl_->engine) == MA_SUCCESS) {
        impl_->device = true;
    } else {
        // No audio device: keep the engine (mixing, timing) without output.
        cfg.noDevice = MA_TRUE;
        cfg.channels = 2;
        cfg.sampleRate = 44100;
        if (ma_engine_init(&cfg, &impl_->engine) != MA_SUCCESS) {
            log::warn("Audio: couldn't initialize");
            return false;
        }
        if (useDevice) log::info("Audio: no output device, running silently");
    }
    impl_->initialized = true;
    ma_engine_listener_set_world_up(&impl_->engine, 0, 0, 0, 1);
    loadScripts();
    return true;
}

void AudioSystem::shutdown() {
    if (!impl_->initialized) return;
    stopAll();
    ma_engine_uninit(&impl_->engine);
    impl_->initialized = false;
}

bool AudioSystem::hasDevice() const { return impl_->device; }

void AudioSystem::loadScripts() {
    impl_->scripts.clear();
    auto text = fs::readText("scripts/sounds.txt");
    if (!text) {
        log::warn("Audio: scripts/sounds.txt not found");
        return;
    }
    for (auto& s : parseSoundScripts(*text)) impl_->scripts[s.name] = s;
    log::debug("Audio: {} sound scripts", impl_->scripts.size());
}

const SoundScript* AudioSystem::findScript(const std::string& name) const {
    auto it = impl_->scripts.find(name);
    return it == impl_->scripts.end() ? nullptr : &it->second;
}

bool AudioSystem::play(const std::string& name, const Vec3& origin, int entity, float vol) {
    if (!impl_->initialized) return false;
    const SoundScript* script = findScript(name);
    if (!script) {
        log::warn("Audio: unknown sound '{}'", name);
        return false;
    }
    std::uniform_int_distribution<size_t> pickWave(0, script->waves.size() - 1);
    std::string rel = "sound/" + script->waves[pickWave(impl_->rng)];
    auto path = fs::resolve(rel);
    if (!path) {
        log::warn("Audio: missing file '{}' for '{}'", rel, name);
        return false;
    }
    if (script->loop) stop(name, entity); // one instance of a loop per entity

    auto sound = std::make_unique<ma_sound>();
    ma_uint32 flags = MA_SOUND_FLAG_DECODE; // decode up front: sounds are short
    if (ma_sound_init_from_file(&impl_->engine, path->string().c_str(), flags, nullptr, nullptr, sound.get()) != MA_SUCCESS) {
        log::warn("Audio: couldn't load '{}'", path->string());
        return false;
    }
    std::uniform_real_distribution<float> pickPitch(script->pitchMin, script->pitchMax);
    ma_sound_set_pitch(sound.get(), pickPitch(impl_->rng) / 100.0f);
    ma_sound_set_volume(sound.get(), script->volume * vol);
    ma_sound_set_looping(sound.get(), script->loop ? MA_TRUE : MA_FALSE);
    float maxDist = soundLevelToDistance(script->soundLevel);
    if (maxDist <= 0) {
        ma_sound_set_spatialization_enabled(sound.get(), MA_FALSE);
    } else {
        ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_linear);
        ma_sound_set_min_distance(sound.get(), 64.0f);
        ma_sound_set_max_distance(sound.get(), maxDist);
        ma_sound_set_position(sound.get(), origin.x, origin.y, origin.z);
    }
    ma_sound_start(sound.get());
    if (snd_show.getBool()) log::info("sound: {} ({}) at {:.0f} {:.0f} {:.0f}", name, rel, origin.x, origin.y, origin.z);
    impl_->playing.push_back({std::move(sound), name, entity});
    return true;
}

void AudioSystem::stop(const std::string& name, int entity) {
    for (auto it = impl_->playing.begin(); it != impl_->playing.end();) {
        if (it->entity == entity && it->name == name) {
            ma_sound_uninit(it->sound.get());
            it = impl_->playing.erase(it);
        } else {
            ++it;
        }
    }
}

void AudioSystem::stopAll() {
    for (auto& p : impl_->playing) ma_sound_uninit(p.sound.get());
    impl_->playing.clear();
}

void AudioSystem::update(const Vec3& pos, const QAngle& angles, const std::vector<std::pair<int, Vec3>>& entityOrigins) {
    if (!impl_->initialized) return;
    ma_engine_set_volume(&impl_->engine, snd_mute.getBool() ? 0.0f : volume.getFloat());
    Vec3 forward;
    angleVectors(angles, &forward);
    ma_engine_listener_set_position(&impl_->engine, 0, pos.x, pos.y, pos.z);
    ma_engine_listener_set_direction(&impl_->engine, 0, forward.x, forward.y, forward.z);

    for (auto it = impl_->playing.begin(); it != impl_->playing.end();) {
        if (it->entity > 0) {
            for (auto& [idx, origin] : entityOrigins)
                if (idx == it->entity) ma_sound_set_position(it->sound.get(), origin.x, origin.y, origin.z);
        }
        if (ma_sound_at_end(it->sound.get()) && !ma_sound_is_looping(it->sound.get())) {
            ma_sound_uninit(it->sound.get());
            it = impl_->playing.erase(it);
        } else {
            ++it;
        }
    }
}

size_t AudioSystem::activeCount() const { return impl_->playing.size(); }

} // namespace k
