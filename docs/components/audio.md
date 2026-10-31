# Audio

**Code:** `engine/audio/` (the `k_audio` library, miniaudio). Sounds: `base/sound/`, scripts:
`base/scripts/sounds.txt`.

Game code plays sounds by **script name**, never by file, so you can re-voice the game by
editing the script:

```
"Door.Move" { "wave" "doors/door_move.wav" "volume" "0.5" "soundlevel" "80" "loop" "1" }
"Player.Footstep"
{
    "volume" "0.45"  "pitch" "95,105"  "soundlevel" "70"
    "rndwave" { "wave" "player/footstep1.wav"  "wave" "player/footstep2.wav" }
}
```

| Key | |
|---|---|
| `wave` / `rndwave` | File(s) under `sound/` (WAV, FLAC, MP3); one picked at random |
| `volume` | 0-1 |
| `pitch` | Percent, or a `min,max` range |
| `soundlevel` | dB, how far it carries: 60 ≈ 500 units, +7 dB doubles. 0 = heard everywhere |
| `loop` | Loops until stopped; one instance per entity |

## Flow

Game code → `world().emitSound(name, entity, origin)` → server sends `Svc::Sound` → client's
`AudioSystem` plays it in 3D. Sounds attached to an entity follow it (a moving door's rumble);
the listener is the camera. Files are read through the filesystem and decoded from memory, so
they work from `.kpk` archives.

## What makes sound now

Footsteps (every ~0.3 s running), jump, landing, pain, death, door move/stop, button press,
physics impacts (sudden velocity changes), hitscan fire and hits, `ambient_generic`.

## Cvars and commands

`volume` (0.8), `snd_mute`, `snd_show 1` logs every sound (used by the tests), `play <name>`.
`-nosound` mixes without opening an audio device (servers, CI); this also happens
automatically when there's no device.

Placeholder sounds come from `tools/gen_sounds.py` (procedural, 22 kHz mono).
