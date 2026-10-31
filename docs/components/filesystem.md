# Filesystem and packed content

**Code:** `engine/core/filesystem.*`, `tools/kpak/`

All game files are read through a virtual filesystem, so code asks for `"materials/dev/dev_wall.png"`
and never cares where it lives.

## Search paths

Search paths are checked in order; the first match wins. Each has an id:

| Id | Used for |
|---|---|
| `MOD` | Where writes go (`config.cfg`, screenshots) |
| `GAME` | Reading content |

At startup the host adds `<exe dir>/<game>` (default `base`, change with `-game <dir>`),
falling back to `./<game>`, then mounts every `*.kpk` in that directory **after** the loose
files, so loose files override packed ones. That makes modding and hot-fixing easy.

Paths are relative, use `/`, and may not contain `..` or be absolute (`fs::isSafeRelativePath`).

## API

```cpp
fs::readText("scripts/sounds.txt");       // std::optional<std::string>
fs::readBinary("models/props/crate.glb"); // std::optional<std::vector<uint8_t>>
fs::exists("cfg/autoexec.cfg");
fs::writeText("cfg/config.cfg", text, "MOD");
fs::resolve("shaders/world.vert");        // real path of a LOOSE file (nullopt inside archives)
```

Code that needs a real file path (like shader hot reload checking timestamps) uses `resolve`;
everything else should use `readBinary` so it also works from archives.

## .kpk archives

A simple uncompressed archive: `KPAK` magic, version, entry count, a directory of
`(path, offset, size)`, then file data. Lookups inside archives are case-insensitive.

```sh
./build/bin/kpak base build/release/base/content.kpk -x maps/src -x screenshots -x cfg/config.cfg
./build/bin/kpak -l build/release/base/content.kpk   # list
```

`-x prefix` skips paths that start with it. Every subsystem reads through the filesystem,
including audio (decoded from memory), so the whole game can ship as one `.kpk` plus the
executable.
