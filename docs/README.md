# Documentation

- [Architecture](ARCHITECTURE.md): how the pieces fit together
- [Roadmap](ROADMAP.md): what's done and what's next
- [Console reference](reference/cvars.md): every cvar, command and button (generated)

## Components

| Component | |
|---|---|
| [Console, cvars and commands](components/console.md) | Config files, adding cvars/commands |
| [Input and key bindings](components/input.md) | `bind`, buttons, UserCmds |
| [Filesystem and packed content](components/filesystem.md) | Search paths, `.kpk` archives |
| [Rendering](components/rendering.md) | Materials, textures, models, shaders, world rendering, effects |
| [Map compiler (kbsp)](components/map-compiler.md) | Compile stages, tool textures, lights, leaks |
| [The .kbsp format](components/bsp-format.md) | Lumps and queries (traces, PVS) |
| [Player movement](components/movement.md) | Source-style movement and its numbers |
| [Entities and I/O](components/entities.md) | Writing entities, I/O syntax, entity reference |
| [Physics](components/physics.md) | Jolt integration, props |
| [Audio](components/audio.md) | Sound scripts, 3D playback |
| [Networking](components/networking.md) | Net channel, snapshots, prediction, lag compensation |
| [Gameplay](components/gameplay.md) | Health, damage, HUD, menu |
| [Tools and testing](components/tools-and-testing.md) | Generators, unit tests, smoke test, CI |

Map editing setup: [tools/trenchbroom/README.md](../tools/trenchbroom/README.md).
