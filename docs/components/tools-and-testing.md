# Tools and testing

## Tools

| Tool | |
|---|---|
| `kbsp` | Map compiler. See [map compiler](map-compiler.md) |
| `kpak` | Build/list `.kpk` archives. See [filesystem](filesystem.md) |
| `tools/gen_dev_assets.py` | Dev textures, tool textures, sky cubemap, crate and player models |
| `tools/gen_sounds.py` | Placeholder sound effects |
| `tools/gen_test_maps.py` | `base/maps/src/devroom.map` |
| `tools/trenchbroom/` | TrenchBroom game config and FGD |

The generators are procedural, so the repo stays small and the art is reproducible. Rerun
them after editing.

## Unit tests (`tests/`, doctest)

```sh
./build/bin/k_tests                 # all
./build/bin/k_tests -tc="movement*" # a subset
```

| File | Covers |
|---|---|
| `test_cvar`, `test_cmdbuffer`, `test_filesystem` | Console, buffer, aliases, exec, search paths, archives |
| `test_keyvalues`, `test_assets`, `test_png` | Parsers, mesh building, glTF, PNG |
| `test_bsp` | Compiles a room: contents, inside-only faces, traces, vis, lightmaps, file round trips, corruption |
| `test_movement` | Landing, speed, walls, sliding, jump height, steps, crouch, air strafing, noclip, box traces |
| `test_game` | I/O chains, delays, limits, wildcards, timers, counters, doors, triggers, +use, elevators |
| `test_physics` | Hulls, falling, pushing, pickup/throw, lift riding |
| `test_audio` | Sound scripts, shipped files, device-less playback, game sound events |
| `test_netchan`, `test_net` | Reliability under 30% loss, sockets, delta round trips, serialization |
| `test_multiplayer` | In-process server + UDP clients, prediction accuracy, passwords, lag compensation |
| `test_combat` | Fall damage, kills, kill feed, respawn, trigger_hurt |

`tests/test_room.h` compiles a small test map once (room, pillar, step, block, door, button,
trigger, lift) that most game tests share.

## Smoke test

```sh
scripts/smoke_test.sh build
```

1. Unit tests.
2. A dedicated server runs 132 ticks.
3. The real client under Xvfb (software GL via EGL) with scripted `+commands`: walks, presses
   the door button, rides the elevator, opens the console, takes screenshots; checks the
   welcome message, I/O, sounds and positions in the log.
4. A dedicated server and a remote client as **separate processes** over UDP: connect, move,
   status, disconnect.

## Scripting the engine

Any `+command` on the command line runs after startup, which makes the engine scriptable for
tests and demos:

```sh
./k -nosound +sv_cheats 1 +setpos 700 -700 40 +setang -8 135 +wait 10 +screenshot +wait +quit
```

Handy for this: `wait`, `setpos`/`setang`/`getpos`, `++forward`/`+-forward`, `screenshot`,
`host_maxticks` (quit after N ticks), `snd_show`, `developer_io`, `-dedicated`, `-nosound`.

## CI

`.github/workflows/ci.yml` runs the smoke test on Ubuntu (everything) and builds and runs
the unit tests and dedicated server on Windows.
