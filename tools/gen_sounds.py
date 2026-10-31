#!/usr/bin/env python3
"""Generates placeholder sound effects in base/sound/ (16-bit mono WAV, 22.05 kHz).

    python3 tools/gen_sounds.py

All procedural (filtered noise and decaying tones), so they're tiny and reproducible.
Replace them with real recordings any time: the sound script refers to them by name.
"""
import math
import os
import random
import struct
import wave

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "base", "sound")
RATE = 22050


def write(name, samples):
    path = os.path.join(ROOT, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    peak = max(1e-6, max(abs(s) for s in samples))
    scale = 0.9 / peak
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s * scale)) * 32767)) for s in samples))


def lowpass(samples, alpha):
    out, y = [], 0.0
    for s in samples:
        y += alpha * (s - y)
        out.append(y)
    return out


def noise(n, seed):
    rnd = random.Random(seed)
    return [rnd.uniform(-1, 1) for _ in range(n)]


def env(n, attack, decay):
    a = max(1, int(attack * RATE))
    return [min(1.0, i / a) * math.exp(-max(0, i - a) / (decay * RATE)) for i in range(n)]


def footstep(seed):
    n = int(0.12 * RATE)
    s = lowpass(noise(n, seed), 0.08 + 0.02 * (seed % 3))
    thump = [math.sin(2 * math.pi * (70 + seed * 7) * i / RATE) * 0.6 for i in range(n)]
    e = env(n, 0.003, 0.025)
    return [(a + b) * c for a, b, c in zip(s, thump, e)]


def main():
    for i in range(4):
        write("player/footstep%d.wav" % (i + 1), footstep(i + 1))
    n = int(0.25 * RATE)
    write("player/jump.wav", [x * e for x, e in zip(lowpass(noise(n, 9), 0.05), env(n, 0.02, 0.06))])
    n = int(0.2 * RATE)
    write("player/land.wav", [(x + math.sin(2 * math.pi * 55 * i / RATE)) * e
                              for i, (x, e) in enumerate(zip(lowpass(noise(n, 10), 0.06), env(n, 0.002, 0.05)))])
    n = int(0.08 * RATE)
    write("buttons/button1.wav", [(math.sin(2 * math.pi * 1400 * i / RATE) * 0.5 + math.sin(2 * math.pi * 2100 * i / RATE) * 0.3)
                                  * e for i, e in enumerate(env(n, 0.001, 0.012))])
    # Door: a seamless 1 s rumble loop (whole number of cycles) and a stop thud.
    n = RATE
    rumble = lowpass(noise(n, 11), 0.02)
    write("doors/door_move.wav", [r * 0.7 + 0.3 * math.sin(2 * math.pi * 60 * i / RATE) for i, r in enumerate(rumble)])
    n = int(0.35 * RATE)
    write("doors/door_stop.wav", [(x * 0.6 + math.sin(2 * math.pi * 48 * i / RATE)) * e
                                  for i, (x, e) in enumerate(zip(lowpass(noise(n, 12), 0.05), env(n, 0.002, 0.08)))])
    for i, f in enumerate((310, 260)):
        n = int(0.25 * RATE)
        write("physics/wood_impact%d.wav" % (i + 1),
              [(math.sin(2 * math.pi * f * t / RATE) + math.sin(2 * math.pi * f * 2.7 * t / RATE) * 0.4 + x * 0.5) * e
               for t, (x, e) in enumerate(zip(lowpass(noise(n, 20 + i), 0.3), env(n, 0.001, 0.05)))])
    n = 2 * RATE
    write("ambient/hum_loop.wav", [0.5 * math.sin(2 * math.pi * 60 * i / RATE) + 0.25 * math.sin(2 * math.pi * 120 * i / RATE) + x * 0.15
                                   for i, x in enumerate(lowpass(noise(n, 30), 0.01))])
    print("Generated sounds in", os.path.normpath(ROOT))


if __name__ == "__main__":
    main()
