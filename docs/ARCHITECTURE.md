# Architecture

## Goals

Borrow what made Source 1 good to build games and mods on:

- **Offline-compiled maps.** Levels are made in an editor (TrenchBroom) and compiled into a
  BSP with precomputed visibility and baked lighting, so runtime work is small.
- **Client/server everywhere.** The server is authoritative. Single-player runs a local server
  over a loopback channel, so multiplayer is not a retrofit.
- **Designer-driven logic.** Entities talk through named inputs and outputs wired in the editor.
- **Everything is a cvar or a command.** Settings, debugging and scripting all go through the console.
- **Engine and game kept apart.** Game code sits behind interfaces, later in its own library.

## Layers

```
┌──────────────────────── Game code (Phase 5: separate module) ───────────────────────┐
│  Entities · Player movement · Weapons · AI · Game rules · Entity I/O                │
└────────────────▲───────────────────────────────────────────────▲────────────────────┘
                 │ interfaces                                    │
┌────────────────┴──────────────────── Engine ───────────────────┴────────────────────┐
│  Server  ◄──── NetChannel (loopback now, UDP later) ────►  Client                   │
│   tick loop, UserCmd processing,                            input → UserCmd,        │
│   snapshots                                                 interpolation, view     │
│                                                                                     │
│  Renderer (GL 4.5) · Console UI (ImGui) · Audio (Phase 6) · Physics (Phase 6)       │
├─────────────────────────────────────────────────────────────────────────────────────┤
│  Core: log · cvars/commands · command buffer · filesystem · math · serialization    │
└─────────────────────────────────────────────────────────────────────────────────────┘
   Platform: SDL3 (window, input, GL context)        Tools (Phase 3): kbsp · kvis · klight
```

Libraries in the build:

| Target | Contents | Depends on |
|---|---|---|
| `k_core` | `core/` | glm |
| `k_net` | UDP sockets, net channel, protocol deltas | k_core |
| `k_assets` | KeyValues, mesh building, glTF parsing (no GPU) | k_core, cgltf |
| `k_game` | entities, I/O, physics, game rules | k_assets, Jolt |
| `k_audio` | sound scripts, playback | k_assets, miniaudio |
| `k_engine` | platform, render, client, server, ui, host | k_game, k_audio, SDL3, glad, imgui, stb |
| `k` | launcher executable | k_engine |
| `k_tests` | unit tests | k_assets (no window or GPU needed) |

## Map pipeline

```
TrenchBroom .map ──kbsp──► .kbsp
  brushes.cpp   map brushes → convex brushes (planes + side polygons)
  tree.cpp      structural brushes → solid-leaf BSP (split on brush planes, axial first)
  portals.cpp   portals between leaves; flood from entities → leak check, outside filled
  faces.cpp     drawable sides pushed down the tree; pieces in solid leaves are dropped
  vis.cpp       per-leaf PVS from portal flow
  light.cpp     lightmaps: point lights, sun + sky (shadow rays through the BSP)
```

A `.kbsp` file is a set of lumps (`engine/shared/bspfile.h`): planes, nodes, leaves, faces,
vertices, brushes (for collision), models, materials, compressed vis, the lightmap atlas, and
a text entity lump. The server uses it for entities (and collision in Phase 4). The renderer
finds the camera's leaf, decompresses its PVS, frustum-culls leaf bounds, and batches visible
faces by material into `glMultiDrawElements` calls.

## Entities

The game library (`game/`) owns every entity; the engine's `Server` only handles timing,
the client connection and snapshots, and calls the game through `IGameServer`.

- Entities subclass `BaseEntity` and register with `LINK_ENTITY_TO_CLASS(classname, Class)`.
  Map key/values go to `keyValue()`, then `spawn()`, then `activate()` once all exist.
- Outputs are `Output` members declared by name. Map keys such as
  `"OnPressed" "door1,Open,,0,-1"` (or `"OnPressed#2"` for more) add connections. Firing an
  output queues events; the queue delivers each to `acceptInput()` on the targets when its
  delay is up.
- Movement collides with the world and with solid brush entities (`GameWorld` implements
  `TraceWorld`). Movers move through `tryPush`, which carries or pushes the player and
  reports `blocked()` if it can't.
- Networked entities go in every snapshot as `EntityState` (model index, origin, angles,
  effects). The client interpolates them, and the renderer draws brush models with an
  offset and props with a transform.

## Networking

```
client                                        server
  input → UserCmd ──(unreliable, last 3)──►     run each new command (shared movement)
  predict: server state + unacked commands      game frame, physics
  interpolate entities cl_interp in the past ◄─ snapshot: player state + entity delta
  ack newest snapshot tick in each Move         (vs the client's last acked snapshot)
```

- `net/udp_channel`: every packet carries a sequence, an ack, a reliable ack, the in-flight
  reliable block (resent until acked), and unreliable messages. The single-player client uses
  a loopback channel with the same interface.
- The server keeps the last 32 snapshots per client. Each new one is delta-encoded against
  the newest snapshot that client acknowledged; if the client didn't receive the baseline, it
  ignores the snapshot and the server falls back to an older ack.
- Prediction runs the same `playerMove` as the server against the map and the brush
  entities in the latest snapshot. When a snapshot shows the server disagreed, the
  difference is added to a view offset that decays over `cl_smoothtime`.
- Hitscan rewinds other players to the tick the shooter was rendering (`UserCmd::tickCount`)
  using a one-second history of player positions.

## Physics and audio

- `game/physics` wraps Jolt and converts 1 unit = 0.0254 m at the boundary. The world is a
  static compound of convex brush hulls. Solid brush entities get kinematic bodies that follow
  their entity each tick. `prop_physics` are dynamic convex hulls of their model. Each server
  tick runs movers, then the physics step, then copies prop transforms back to the entities.
- Player-vs-prop collision treats props as boxes (their world bounds) in the same `TraceWorld`
  the movement code uses. Props the player walks into get nudged along.
- The game asks for sounds by script name through `EngineServices::emitSound`. The server
  sends `Svc::Sound`, and the client's `AudioSystem` (miniaudio) plays it in 3D, moving
  entity-attached sounds with the entity.

## Rendering

```
scene → HDR target (RGBA16F, MSAA) → resolve → postprocess (exposure, ACES, gamma) → window → ImGui
```

- Materials (`base/materials/*.kmt`) choose a shader (`LitGeneric`, `UnlitGeneric`) and set its
  parameters. Meshes keep one draw range per material.
- Textures are sRGB and lighting happens in linear space. Opaque geometry draws first, then the
  sky at the far plane, then translucent geometry, then debug lines.
- Shaders live in `base/shaders/` and recompile when their files change.

## Main loop

`Host::run` (in `engine/host/host.cpp`):

```
every frame:
    pump SDL events → key bindings queue commands
    run the command buffer
    accumulator += frame time × host_timescale
    while accumulator ≥ tick interval (1/66 s), at most 8 times:
        client.createMove()   input → UserCmd → channel
        server.tick()         read UserCmds, simulate, send snapshot
        client.readPackets()  keep the last two snapshots
    alpha = accumulator / tick interval
    render view interpolated between the two snapshots by alpha
    (mouse look uses the newest local angles, so it adds no latency)
```

The simulation runs at a fixed rate no matter the frame rate, which keeps physics and movement
deterministic. That is also what client-side prediction (Phase 7) needs.

## Console system

- `ConVar` / `ConCommand` objects declared at file scope register themselves in an intrusive
  list at static-init time, like Source. Lookups are case-insensitive.
- Flags: `FCVAR_ARCHIVE` (saved to `config.cfg`), `FCVAR_CHEAT` (needs `sv_cheats 1`),
  `FCVAR_REPLICATED`, `FCVAR_NOTIFY`, `FCVAR_DEVELOPMENT`.
- The command buffer takes text from the console, cfg files, bindings and the command line.
  `exec` inserts a file's commands at the front of the queue, `wait` pauses it for a frame.
- Startup order: `default.cfg` → `config.cfg` (written on exit) → `autoexec.cfg` → `+commands`
  from the command line.

## Input

`bind <key> <command>`. A binding that starts with `+` runs `+cmd <key>` on press and
`-cmd <key>` on release. `KButton` tracks which keys hold each button, so two keys bound to
`+forward` work the way they should. Each tick the client samples the buttons into a `UserCmd`
(move values, view angles, button bits) and sends it to the server.

## Conventions

- **Coordinates:** Z up, X forward, Y left. 1 unit ≈ 1 inch; the player is 72 units tall with
  eyes at 64.
- **Angles:** degrees, `QAngle{pitch, yaw, roll}`. Positive pitch looks down. `angleVectors`
  matches Source.
- **FOV:** horizontal degrees at 4:3, widened for wider screens ("Hor+"), the same as Source.
- **Code:** C++20, namespace `k`, files `snake_case`, types `PascalCase`, functions `camelCase`,
  cvars `lower_snake` with a subsystem prefix (`sv_`, `cl_`, `r_`, `mat_`, `host_`).

## Planned file formats

| Purpose | Source 1 | k |
|---|---|---|
| Map source | `.vmf` (Hammer) | `.map` (TrenchBroom / Quake format) |
| Compiled map | `.bsp` | `.kbsp` |
| Model | `.mdl` + `.vvd` + `.vtx` | glTF 2.0 source → `.kmdl` |
| Texture | `.vtf` | PNG/TGA source → `.ktx2` later |
| Material | `.vmt` | `.kmt` (same key/value text style) |
| Packed content | `.vpk` | `.kpk` |

## Testing

- `k_tests`: fast unit tests for everything in `k_core`.
- `scripts/smoke_test.sh`: unit tests, then a dedicated server run (`+host_maxticks`), then a
  windowed client under Xvfb that's driven by `+commands` (`++forward`, `+wait N`,
  `+screenshot`, `+quit`) and checked for screenshots. The scripted command line doubles as a
  general-purpose automation hook.
- In CI, Linux runs everything (software GL through Mesa llvmpipe over EGL) and Windows runs the
  unit tests and the dedicated server.
