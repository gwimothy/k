# The .kbsp format

**Code:** `engine/shared/bspfile.*`, `engine/shared/bsp_trace.*`

Shared by the compiler, server and renderer. Little-endian, versioned (`kVersion`; loading a
mismatch says "recompile the map"). Layout: `KBSP` magic, version, lump count, a directory of
`(offset, size)`, then the lumps, each padded to 4 bytes.

| Lump | Contents |
|---|---|
| entities | Text: `{ "key" "value" ... }` per entity (parse with `bsp::parseEntities`) |
| planes | `{normal, dist}`; pairs `i` and `i^1` are opposites |
| nodes | `{plane, children[2], mins, maxs}`; child ≥ 0 = node, < 0 = leaf `-1-child` |
| leaves | `{contents, mins, maxs, leaf faces, leaf brushes, visOffset}` |
| vertices | `{position, normal, uv, lightmapUV}` |
| indices | Triangles (uint32) |
| faces | `{firstIndex, numIndices, material, plane, flags, lightmap rect}` |
| leaffaces / leafbrushes | Index lists referenced by leaves |
| brushes / brushsides | Convex collision brushes (with bevel planes) |
| models | Model 0 = world; others = brush entities `{bounds, faces, brushes}` |
| materials | Material name per face index |
| visibility | Per-leaf bitsets, zero bytes run-length encoded |
| lightmap | Atlas width, height, RGB half floats (linear HDR) |

`bsp::read` validates every index, so a corrupt or hostile file is rejected instead of crashing.

Contents flags: `CONTENTS_SOLID`, `CONTENTS_SKY`, `CONTENTS_DETAIL`, `CONTENTS_PLAYERCLIP`,
`CONTENTS_OUTSIDE`.

## Queries

```cpp
int leaf = bsp.findLeaf(point);
uint32_t c = bsp.pointContents(point);
bsp.decompressVis(leaf, visibleLeaves);        // one byte per leaf
auto ent = bsp.findEntity("info_player_start");

auto tr = bsp::traceLine(bsp, start, end);     // ray vs solid leaves
auto tb = bsp::traceBox(bsp, start, end, mins, maxs);          // box vs brushes
auto tm = bsp::traceBoxModel(bsp, model, offset, start, end, mins, maxs); // vs one brush entity
// tr.fraction, tr.endPos, tr.normal, tr.contents, tr.startSolid, tr.allSolid, tr.entity
```

Box traces stop 1/32 unit short of surfaces (Source's `DIST_EPSILON`), and a box exactly
touching a surface counts as outside it, so something resting on the floor isn't stuck.
