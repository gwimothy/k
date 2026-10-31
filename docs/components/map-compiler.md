# Map compiler (kbsp)

**Code:** `tools/kbsp/`. **Format:** [BSP format](bsp-format.md). **Editor setup:** [tools/trenchbroom](../../tools/trenchbroom/README.md)

`kbsp` turns a TrenchBroom `.map` (standard or Valve 220 format) into a `.kbsp` the engine
loads. Every `.map` in `base/maps/src/` is compiled automatically by the build.

```sh
./build/bin/kbsp -game base -o build/bin/base/maps/mymap.kbsp base/maps/src/mymap.map
```

| Option | |
|---|---|
| `-game <dir>` | Game directory (texture sizes come from `materials/*.png`) |
| `-o <file>` | Output (default `<game>/maps/<name>.kbsp`) |
| `-novis` | Skip visibility (everything visible) |
| `-nolight` | Skip lighting (fullbright) |
| `-luxel <n>` | Lightmap resolution in units per luxel (default 16, like Source) |
| `-threads <n>` | Worker threads (default: all cores) |

## Stages

| File | Stage |
|---|---|
| `map_file.cpp` | Parse entities and brushes; plane from 3 points, `(p0-p1)×(p2-p1)` points out of the brush |
| `brushes.cpp` | Each side's polygon = its plane clipped by every other side. Contents from textures |
| `tree.cpp` | Solid-leaf BSP from **structural** brushes: split on unused brush planes (axial first, fewest splits). A leaf is solid when a brush fragment is left in it |
| `portals.cpp` | Portals (openings) between leaves; flood fill from point entities. Reaching the void = **leak** (prints a path, skips vis and outside removal). Unreached leaves become solid |
| `faces.cpp` | Push every drawable brush side down the tree; pieces landing in solid leaves are dropped (hidden inside brushes or outside the map). Builds collision brush lists per leaf, adds axial bevel planes, writes everything to `BspData` |
| `vis.cpp` | PVS per leaf by flowing through portals (Quake's base vis / `vvis -fast`: conservative) |
| `light.cpp` | Lightmaps: point lights, sun (shadow rays must reach a sky brush), sky ambient (48 rays). HDR, packed into one atlas |

## Textures with special meaning

| Texture | |
|---|---|
| `tools/toolsskybox` | Solid, not drawn, sky shows through; the sun lights through it |
| `tools/toolsnodraw` | Solid, not drawn |
| `tools/toolsplayerclip`, `tools/toolsclip` | Blocks players only; not drawn, doesn't seal or block vis |
| `tools/toolstrigger` | Use on trigger brushes (not drawn) |

## Brush entities

- Brushes of `worldspawn` are structural: they cut the BSP and block visibility.
- `func_detail` brushes are drawn and solid but don't cut the BSP or block vis. Use them for
  small or complex shapes to keep compiles fast and vis tight.
- Any other brush entity (`func_door`, `trigger_once`...) becomes a submodel `*N` with its own
  faces and collision brushes, referenced by the entity's `model` key.

## Lights

| Entity | Keys |
|---|---|
| `light` | `_light` "r g b brightness" (sRGB colour 0-255). Falls off with distance squared; at 100 units it's about `brightness/255` |
| `light_environment` | `pitch` (-90 = overhead), `angles` (yaw), `_light` sun, `_ambient` sky. One per map |
| worldspawn | `_minlight` floor brightness (0-255) |

## Leaks

A leak means the void touches a reachable leaf. kbsp prints
`**** LEAK **** ... Path from the void toward an entity: (x y z) ...`. Follow the points to the
gap. The map still loads, but without vis or outside removal.

## Generated test maps

`tools/gen_test_maps.py` writes `base/maps/src/devroom.map`: the main hall, corridor, dark
room, stairs, ramp, door with button, elevator, triggers, physics crates and sounds. It's
plain Valve 220 text, so you can open it in TrenchBroom.
