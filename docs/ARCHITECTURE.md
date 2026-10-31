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
| `k_core` | `core/`, `shared/` movement, header-only `net/` | glm |
| `k_assets` | KeyValues, mesh building, glTF parsing (no GPU) | k_core, cgltf |
| `k_engine` | platform, render, client, server, ui, host | k_assets, SDL3, glad, imgui, stb |
| `k` | launcher executable | k_engine |
| `k_tests` | unit tests | k_assets (no window or GPU needed) |

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
