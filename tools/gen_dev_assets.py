#!/usr/bin/env python3
"""Generates the placeholder art in base/: dev textures, a sky cubemap and a test crate model.

Everything is procedural so the repo stays small and the art is reproducible:
    python3 tools/gen_dev_assets.py
"""
import json
import math
import os
import struct
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "base")


def write_png(path, w, h, pixels):
    """pixels: bytes/bytearray of RGB rows, top row first."""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    raw = b"".join(b"\x00" + bytes(pixels[y * w * 3:(y + 1) * w * 3]) for y in range(h))

    def chunk(t, c):
        return struct.pack(">I", len(c)) + t + c + struct.pack(">I", zlib.crc32(t + c) & 0xFFFFFFFF)

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    data += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


def clamp8(v):
    return max(0, min(255, int(round(v))))


# --------------------------------------------------------------------------------------
# Dev textures: 256x256 covers 256x256 world units at scale 1 (like Source's dev/ set).

def dev_texture(base, size=256, label_cells=4):
    px = bytearray(size * size * 3)
    br, bg, bb = base
    for y in range(size):
        for x in range(size):
            f = 1.0
            if x % 16 == 0 or y % 16 == 0:
                f *= 0.90
            if x % 64 == 0 or y % 64 == 0 or x % 64 == 63 or y % 64 == 63:
                f *= 0.72
            if x < 2 or y < 2 or x >= size - 2 or y >= size - 2:
                f *= 0.60
            # Subtle checker every 64px so motion is easy to see.
            if ((x // 64) + (y // 64)) % 2 == 0:
                f *= 0.94
            i = (y * size + x) * 3
            px[i:i + 3] = bytes((clamp8(br * f), clamp8(bg * f), clamp8(bb * f)))
    return size, size, px


def crate_texture(size=256):
    px = bytearray(size * size * 3)
    for y in range(size):
        for x in range(size):
            # Planks
            plank = (y // 32) % 2
            grain = 0.9 + 0.1 * math.sin(x * 0.15 + plank * 3 + math.sin(y * 0.4) * 2)
            r, g, b = 150 * grain, 105 * grain, 60 * grain
            # Frame + diagonal brace
            edge = min(x, y, size - 1 - x, size - 1 - y)
            if edge < 20 or abs(x - y) < 14:
                r, g, b = r * 0.75, g * 0.72, b * 0.7
            if edge < 3 or abs(edge - 20) < 2:
                r, g, b = r * 0.5, g * 0.5, b * 0.5
            i = (y * size + x) * 3
            px[i:i + 3] = bytes((clamp8(r), clamp8(g), clamp8(b)))
    return size, size, px


# --------------------------------------------------------------------------------------
# Sky cubemap. Faces use the OpenGL cubemap convention; the engine samples with
# gl = (world.x, world.z, -world.y), so world = (gl.x, -gl.z, gl.y).

SUN = (0.35, 0.25, 0.9)


def sky_color(d):
    x, y, z = d
    l = math.sqrt(x * x + y * y + z * z)
    x, y, z = x / l, y / l, z / l
    horizon = (0.80, 0.86, 0.92)
    zenith = (0.28, 0.48, 0.82)
    ground = (0.36, 0.34, 0.32)
    if z >= 0:
        t = math.pow(z, 0.5)
        c = [horizon[i] * (1 - t) + zenith[i] * t for i in range(3)]
    else:
        t = min(1.0, -z * 4)
        c = [horizon[i] * (1 - t) + ground[i] * t for i in range(3)]
    sl = math.sqrt(sum(s * s for s in SUN))
    sd = max(0.0, (x * SUN[0] + y * SUN[1] + z * SUN[2]) / sl)
    sun = math.pow(sd, 800) * 4 + math.pow(sd, 12) * 0.25
    return [min(1.0, c[i] + sun * (1.0, 0.92, 0.75)[i]) for i in range(3)]


def gl_face_dir(face, u, v):
    # u, v in [-1, 1]; standard GL cubemap face orientation.
    return {
        "px": (1, -v, -u),
        "nx": (-1, -v, u),
        "py": (u, 1, v),
        "ny": (u, -1, -v),
        "pz": (u, -v, 1),
        "nz": (-u, -v, -1),
    }[face]


def sky_face(face, size=128):
    px = bytearray(size * size * 3)
    for y in range(size):
        for x in range(size):
            u = (x + 0.5) / size * 2 - 1
            v = (y + 0.5) / size * 2 - 1
            gx, gy, gz = gl_face_dir(face, u, v)
            world = (gx, -gz, gy)
            c = sky_color(world)
            i = (y * size + x) * 3
            # Stored as sRGB.
            px[i:i + 3] = bytes(clamp8(math.pow(ch, 1 / 2.2) * 255) for ch in c)
    return size, size, px


# --------------------------------------------------------------------------------------
# Test model: a 32x32x32 unit crate as binary glTF (.glb), origin at the bottom center.

def crate_glb(path, half=16.0):
    positions, normals, uvs, indices = [], [], [], []
    faces = [
        ((0, 0, 1), (1, 0, 0), (0, 1, 0)),
        ((0, 0, -1), (1, 0, 0), (0, -1, 0)),
        ((1, 0, 0), (0, 1, 0), (0, 0, 1)),
        ((-1, 0, 0), (0, -1, 0), (0, 0, 1)),
        ((0, 1, 0), (-1, 0, 0), (0, 0, 1)),
        ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
    ]
    for n, uax, vax in faces:
        base = len(positions)
        for (su, sv) in ((-1, -1), (1, -1), (1, 1), (-1, 1)):
            p = [n[i] * half + uax[i] * su * half + vax[i] * sv * half for i in range(3)]
            p[2] += half  # origin at bottom
            positions.append(p)
            normals.append(list(n))
            uvs.append([(su + 1) / 2, 1 - (sv + 1) / 2])
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]

    def pack(fmt, rows):
        return b"".join(struct.pack(fmt, *r) for r in rows)

    pos_b = pack("<3f", positions)
    nrm_b = pack("<3f", normals)
    uv_b = pack("<2f", uvs)
    idx_b = struct.pack("<%dH" % len(indices), *indices)
    idx_b += b"\x00" * ((4 - len(idx_b) % 4) % 4)
    blob = pos_b + nrm_b + uv_b + idx_b
    offs = [0, len(pos_b), len(pos_b) + len(nrm_b), len(pos_b) + len(nrm_b) + len(uv_b)]
    gltf = {
        "asset": {"version": "2.0", "generator": "k gen_dev_assets.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "crate"}],
        "materials": [{"name": "models/props/crate", "pbrMetallicRoughness": {"baseColorFactor": [1, 1, 1, 1]}}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                    "indices": 3, "material": 0}]}],
        "buffers": [{"byteLength": len(blob)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": offs[0], "byteLength": len(pos_b), "target": 34962},
            {"buffer": 0, "byteOffset": offs[1], "byteLength": len(nrm_b), "target": 34962},
            {"buffer": 0, "byteOffset": offs[2], "byteLength": len(uv_b), "target": 34962},
            {"buffer": 0, "byteOffset": offs[3], "byteLength": len(indices) * 2, "target": 34963},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": len(positions), "type": "VEC3",
             "min": [-half, -half, 0], "max": [half, half, 2 * half]},
            {"bufferView": 1, "componentType": 5126, "count": len(normals), "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": len(uvs), "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": len(indices), "type": "SCALAR"},
        ],
    }
    js = json.dumps(gltf, separators=(",", ":")).encode()
    js += b" " * ((4 - len(js) % 4) % 4)
    out = struct.pack("<III", 0x46546C67, 2, 12 + 8 + len(js) + 8 + len(blob))
    out += struct.pack("<II", len(js), 0x4E4F534A) + js
    out += struct.pack("<II", len(blob), 0x004E4942) + blob
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(out)


def main():
    devs = {
        "dev_floor": (130, 130, 136),
        "dev_wall": (190, 190, 184),
        "dev_ceiling": (150, 150, 158),
        "dev_orange": (230, 133, 46),
        "dev_blue": (64, 115, 204),
        "dev_green": (90, 166, 90),
    }
    for name, color in devs.items():
        write_png(os.path.join(ROOT, "materials", "dev", name + ".png"), *dev_texture(color))
    write_png(os.path.join(ROOT, "materials", "models", "props", "crate.png"), *crate_texture())
    # Tool textures: only shown in the editor (the compiler never draws these faces).
    tools = {
        "toolsskybox": (120, 170, 230),
        "toolsnodraw": (220, 200, 40),
        "toolsplayerclip": (170, 80, 200),
        "toolsclip": (200, 60, 60),
    }
    for name, color in tools.items():
        write_png(os.path.join(ROOT, "materials", "tools", name + ".png"), *dev_texture(color, size=64))
    for face in ("px", "nx", "py", "ny", "pz", "nz"):
        write_png(os.path.join(ROOT, "materials", "skybox", "sky_day_" + face + ".png"), *sky_face(face))
    crate_glb(os.path.join(ROOT, "models", "props", "crate.glb"))
    print("Generated assets in", os.path.normpath(ROOT))


if __name__ == "__main__":
    main()
